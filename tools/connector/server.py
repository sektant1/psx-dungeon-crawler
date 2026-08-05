#!/usr/bin/env python3
"""Connector: the browser window onto the engine's debug channels.

Naughty Dog's Connector (Game Engine Architecture, 4th ed., 3.4.4) collects the
engine's named debug streams in Redis and lets a developer view and filter them
from any browser. This is that, for this engine.

The one design decision worth reading before the code: **this speaks Redis's
wire protocol itself**, so Redis is optional.

The engine only ever talks RESP. Point it at a real Redis and Connector
subscribes to that, which is the book's architecture and what you want when
several machines or several game instances report into one place. Point it at
Connector directly -- the default -- and this process accepts the same protocol
on the same port, implementing the four commands the sink actually uses. That
matters because requiring a Redis install to read a log is friction on Linux,
awkward on macOS and genuinely unpleasant on Windows, and the whole value of a
debug channel is that it is there when you need it.

Standard library only, on purpose. It has to run on the three platforms the
engine ships to, from a checkout, with no pip step.

    python3 tools/connector/server.py                 # embedded collector
    python3 tools/connector/server.py --redis         # subscribe to a real Redis
    python3 tools/connector/server.py --port 878
"""

from __future__ import annotations

import argparse
import collections
import json
import mimetypes
import os
import queue
import socket
import socketserver
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

UI_DIR = Path(__file__).resolve().parent / "ui"

# Per channel. The browser asks for this on connect; without scrollback, opening
# Connector mid-session shows an empty pane until the next line happens to
# arrive, which is useless exactly when you are chasing something intermittent.
HISTORY_PER_CHANNEL = 2000


def _watch_key(payload: str) -> tuple[str, str] | None:
    """(channel, name) for a watch record, or None for anything that scrolls.

    Samples are deliberately NOT watches here: a sample is a point on a graph
    and its value over time is the whole content, so it belongs in the ring
    where the history lives.
    """
    try:
        record = json.loads(payload)
    except (ValueError, TypeError):
        return None
    if not isinstance(record, dict) or record.get("kind") != "watch":
        return None
    name = record.get("name") or record.get("msg") or ""
    return (str(record.get("ch", "")), str(name))


class Hub:
    """Fan-out from the collector to every connected browser.

    One deque per channel for scrollback, plus a queue per live subscriber.
    A slow browser must never block the collector -- and through it the game --
    so a full subscriber queue drops its oldest and records the fact rather than
    applying back-pressure.
    """

    def __init__(self, history: int = HISTORY_PER_CHANNEL) -> None:
        self._lock = threading.Lock()
        self._history: dict[str, collections.deque] = {}
        # The latest value of every watch, which is NOT scrollback and must not
        # age out of one. A watch means "the current value of X"; the log ring
        # holds the last N records regardless of what they are, so a value the
        # engine republishes every few seconds -- code size, a slow counter --
        # is evicted by a busy channel long before anyone opens the browser, and
        # its pane is then empty for no reason the reader can see. Keyed by
        # channel and name, so it is bounded by how many distinct values exist
        # rather than by how often they arrive.
        self._watches: dict[tuple[str, str], str] = {}
        self._channels: set[str] = set()
        self._subscribers: list[queue.Queue] = []
        self._history_size = history
        self.dropped = 0
        self.received = 0

    def publish(self, channel: str, payload: str) -> None:
        with self._lock:
            self.received += 1
            self._channels.add(channel)
            key = _watch_key(payload)
            if key is not None:
                self._watches[key] = payload
            else:
                bucket = self._history.get(channel)
                if bucket is None:
                    bucket = collections.deque(maxlen=self._history_size)
                    self._history[channel] = bucket
                bucket.append(payload)
            subscribers = list(self._subscribers)
        for q in subscribers:
            try:
                q.put_nowait(payload)
            except queue.Full:
                # Drop the oldest for this browser only. Its tail resyncs on the
                # next record; every other browser is unaffected.
                try:
                    q.get_nowait()
                    q.put_nowait(payload)
                except (queue.Empty, queue.Full):
                    pass
                self.dropped += 1

    def subscribe(self) -> queue.Queue:
        q: queue.Queue = queue.Queue(maxsize=4096)
        with self._lock:
            self._subscribers.append(q)
        return q

    def unsubscribe(self, q: queue.Queue) -> None:
        with self._lock:
            if q in self._subscribers:
                self._subscribers.remove(q)

    def channels(self) -> list[str]:
        with self._lock:
            return sorted(self._channels)

    def history(self, channels: list[str] | None, limit: int) -> list[str]:
        with self._lock:
            wanted = channels or sorted(self._history)
            rows: list[str] = []
            for name in wanted:
                rows.extend(self._history.get(name, ()))
            watches = list(self._watches.values())
        # Chronological across channels. The payloads carry a monotonic `t`, so
        # this is a real ordering rather than an interleave of arrival order.
        def stamp(row: str) -> float:
            try:
                return float(json.loads(row).get("t", 0.0))
            except (ValueError, AttributeError):
                return 0.0

        rows.sort(key=stamp)
        # Trim the log first, then append every current watch: the limit is
        # about how much scrollback to send, and dropping a current value to
        # make room for an older log line gets that backwards.
        rows = rows[-limit:] if limit > 0 else rows
        return rows + watches

    def clear(self) -> None:
        with self._lock:
            self._history.clear()
            self._watches.clear()
            self._channels.clear()

    def stats(self) -> dict:
        with self._lock:
            return {
                "received": self.received,
                "dropped": self.dropped,
                "channels": len(self._channels),
                "subscribers": len(self._subscribers),
                "buffered": sum(len(d) for d in self._history.values()),
            }


# --------------------------------------------------------------------------
# RESP: the subset the engine's sink emits, and the replies it expects back.
# --------------------------------------------------------------------------
def read_resp_command(rfile) -> list[bytes] | None:
    """One inline or array command, or None at end of stream.

    Length-prefixed throughout, which is what makes it safe to carry a log line
    containing newlines and quotes without any escaping.
    """
    line = rfile.readline()
    if not line:
        return None
    line = line.strip()
    if not line:
        return []
    if not line.startswith(b"*"):
        return line.split()  # inline command, as redis-cli sends
    try:
        count = int(line[1:])
    except ValueError:
        return []
    args: list[bytes] = []
    for _ in range(max(count, 0)):
        header = rfile.readline().strip()
        if not header.startswith(b"$"):
            return args
        try:
            length = int(header[1:])
        except ValueError:
            return args
        payload = rfile.read(length) if length >= 0 else b""
        rfile.read(2)  # the trailing CRLF
        args.append(payload)
    return args


class CollectorHandler(socketserver.StreamRequestHandler):
    """The embedded Redis-compatible endpoint.

    Implements exactly what eng::telemetry's sink uses: PUBLISH, LPUSH, LTRIM,
    SADD, plus PING and a couple of niceties so `redis-cli` can poke it. Every
    reply is +OK or :N, because the sink counts replies and never reads values.
    """

    hub: Hub = None  # type: ignore[assignment]

    def handle(self) -> None:
        peer = self.client_address[0]
        print(f"[connector] engine connected from {peer}", flush=True)
        try:
            while True:
                args = read_resp_command(self.rfile)
                if args is None:
                    break
                if not args:
                    continue
                self._dispatch(args)
        except (ConnectionResetError, BrokenPipeError, OSError):
            pass
        finally:
            print(f"[connector] engine disconnected ({peer})", flush=True)

    def _dispatch(self, args: list[bytes]) -> None:
        name = args[0].upper()
        if name == b"PUBLISH" and len(args) >= 3:
            key = args[1].decode("utf-8", "replace")
            channel = key.split(":ch:", 1)[-1]
            self.hub.publish(channel, args[2].decode("utf-8", "replace"))
            self._reply(b":1\r\n")
        elif name in (b"LPUSH", b"RPUSH"):
            # The scrollback write. The hub already keeps history from the
            # PUBLISH, so this is acknowledged and dropped -- storing it twice
            # would double the memory for the same rows.
            self._reply(b":1\r\n")
        elif name in (b"LTRIM", b"SADD", b"SELECT", b"CLIENT", b"HSET"):
            self._reply(b"+OK\r\n")
        elif name == b"PING":
            self._reply(b"+PONG\r\n")
        elif name == b"QUIT":
            self._reply(b"+OK\r\n")
            raise ConnectionResetError
        else:
            self._reply(b"+OK\r\n")

    def _reply(self, data: bytes) -> None:
        try:
            self.wfile.write(data)
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            raise ConnectionResetError


class Collector(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


def redis_subscriber(hub: Hub, host: str, port: int, prefix: str) -> None:
    """Subscribe to a real Redis and feed the hub.

    Used with --redis, which is the book's arrangement: the engine writes to
    Redis, Redis is the collector, and any number of Connectors read it.
    """
    pattern = f"{prefix}:ch:*".encode()
    while True:
        try:
            with socket.create_connection((host, port), timeout=5) as sock:
                sock.settimeout(None)
                rfile = sock.makefile("rb")
                sock.sendall(
                    b"*2\r\n$10\r\nPSUBSCRIBE\r\n$%d\r\n%s\r\n"
                    % (len(pattern), pattern)
                )
                print(
                    f"[connector] subscribed to redis {host}:{port} "
                    f"({pattern.decode()})",
                    flush=True,
                )
                while True:
                    message = read_resp_reply(rfile)
                    if message is None:
                        break
                    # pmessage: [kind, pattern, channel, payload]
                    if (
                        isinstance(message, list)
                        and len(message) == 4
                        and message[0] == b"pmessage"
                    ):
                        key = message[2].decode("utf-8", "replace")
                        channel = key.split(":ch:", 1)[-1]
                        hub.publish(
                            channel, message[3].decode("utf-8", "replace")
                        )
        except OSError as error:
            print(f"[connector] redis unavailable ({error}); retrying", flush=True)
            time.sleep(2.0)


def read_resp_reply(rfile):
    """A full RESP reply, for the subscriber side."""
    line = rfile.readline()
    if not line:
        return None
    tag, body = line[:1], line[1:].strip()
    if tag in (b"+", b"-", b":"):
        return body
    if tag == b"$":
        length = int(body)
        if length < 0:
            return None
        payload = rfile.read(length)
        rfile.read(2)
        return payload
    if tag == b"*":
        count = int(body)
        return [read_resp_reply(rfile) for _ in range(max(count, 0))]
    return None


# --------------------------------------------------------------------------
# HTTP: the UI, the event stream, and a small JSON API.
# --------------------------------------------------------------------------
class ConnectorHTTP(BaseHTTPRequestHandler):
    hub: Hub = None  # type: ignore[assignment]
    server_version = "Connector/1.0"

    def log_message(self, fmt, *args):  # noqa: A003 - quiet by default
        pass

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler's name
        path = self.path.split("?", 1)[0]
        query = {}
        if "?" in self.path:
            for pair in self.path.split("?", 1)[1].split("&"):
                if "=" in pair:
                    k, v = pair.split("=", 1)
                    query[k] = v

        if path in ("/", "/index.html"):
            self._serve_file("index.html")
        elif path.startswith("/static/"):
            self._serve_file(path[len("/static/") :])
        elif path == "/api/channels":
            self._json({"channels": self.hub.channels()})
        elif path == "/api/stats":
            self._json(self.hub.stats())
        elif path == "/api/history":
            channels = [c for c in query.get("channels", "").split(",") if c]
            limit = int(query.get("limit", "1000"))
            self._json({"rows": self.hub.history(channels or None, limit)})
        elif path == "/api/stream":
            self._stream()
        else:
            self.send_error(404)

    def do_POST(self) -> None:  # noqa: N802
        if self.path.split("?", 1)[0] == "/api/clear":
            self.hub.clear()
            self._json({"ok": True})
        else:
            self.send_error(404)

    # -- Server-sent events, rather than websockets -------------------------
    # This stream is one-way and text: SSE is exactly that shape, it is in the
    # standard library on the server side and in every browser on the client
    # side, it reconnects on its own, and it needs no dependency. A websocket
    # would buy bidirectionality nothing here wants.
    def _stream(self) -> None:
        q = self.hub.subscribe()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("X-Accel-Buffering", "no")
        self.end_headers()
        try:
            while True:
                try:
                    payload = q.get(timeout=15.0)
                except queue.Empty:
                    # A comment frame. Without it an idle stream is
                    # indistinguishable from a dead one to every proxy between
                    # here and the browser.
                    self.wfile.write(b": keepalive\n\n")
                    self.wfile.flush()
                    continue
                self.wfile.write(b"data: " + payload.encode("utf-8") + b"\n\n")
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            self.hub.unsubscribe(q)

    def _json(self, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_file(self, relative: str) -> None:
        # Resolved and contained: this serves a directory to a browser, and
        # "../../etc/passwd" is the first thing anyone tries.
        target = (UI_DIR / relative).resolve()
        if not str(target).startswith(str(UI_DIR.resolve())) or not target.is_file():
            self.send_error(404)
            return
        kind = mimetypes.guess_type(target.name)[0] or "application/octet-stream"
        body = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8777, help="web UI port")
    parser.add_argument(
        "--collector-port",
        type=int,
        default=6379,
        help="port the engine connects to (Redis's, by default)",
    )
    parser.add_argument(
        "--redis",
        action="store_true",
        help="subscribe to a real Redis instead of collecting directly",
    )
    parser.add_argument("--redis-host", default="127.0.0.1")
    parser.add_argument("--redis-port", type=int, default=6379)
    parser.add_argument("--prefix", default="raven")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument(
        "--open", action="store_true", help="open a browser on start"
    )
    args = parser.parse_args()

    hub = Hub()

    if args.redis:
        threading.Thread(
            target=redis_subscriber,
            args=(hub, args.redis_host, args.redis_port, args.prefix),
            daemon=True,
        ).start()
    else:
        CollectorHandler.hub = hub
        try:
            collector = Collector((args.host, args.collector_port), CollectorHandler)
        except OSError as error:
            print(
                f"[connector] cannot listen on {args.host}:{args.collector_port}"
                f" ({error}).\n"
                f"            Something is already there -- if it is a real "
                f"Redis, re-run with --redis.",
                file=sys.stderr,
            )
            return 1
        threading.Thread(target=collector.serve_forever, daemon=True).start()
        print(
            f"[connector] collecting on {args.host}:{args.collector_port} "
            f"(RESP; no Redis needed)",
            flush=True,
        )

    ConnectorHTTP.hub = hub
    web = ThreadingHTTPServer((args.host, args.port), ConnectorHTTP)
    web.daemon_threads = True
    url = f"http://{args.host}:{args.port}/"
    print(f"[connector] web UI at {url}", flush=True)
    if args.open:
        import webbrowser

        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        web.serve_forever()
    except KeyboardInterrupt:
        print("\n[connector] bye", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
