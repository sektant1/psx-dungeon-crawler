// Connector's client half.
//
// The whole job is: hold a bounded window of records, decide which of them the
// current filters admit, and keep the DOM in step without ever doing O(all
// records) work on a frame. A debug view that stutters under load is a debug
// view you stop opening precisely when the game is busy, which is when you
// needed it.
//
// Three decisions that follow from that:
//
//  * Records live in a ring, not the DOM. The DOM holds only what is visible-ish
//    (MAX_ROWS), and rows are recycled off the top.
//  * Incoming records are buffered and flushed on requestAnimationFrame, so a
//    burst of two thousand in one frame costs one layout, not two thousand.
//  * Re-filtering rebuilds from the ring rather than mutating rows in place --
//    it is rarer than arrival and much simpler to get right.

const MAX_RECORDS = 200000; // the ring; ~40 MB of strings at typical line length
const MAX_ROWS = 4000;      // DOM rows retained; well past a screenful

const state = {
  records: [],        // ring buffer of parsed records
  head: 0,            // next write index once the ring is full
  full: false,
  channels: new Map(),// name -> { on, count }
  levels: new Set(["info", "warn", "error"]),
  filter: null,       // {kind:'text'|'regex', value}
  follow: true,
  paused: false,
  frameFocus: null,
  pending: [],        // arrived since the last frame
  shown: 0,
  received: 0,
  rateWindow: [],
};

// Watches: name -> {value, history[], dirty}. Kept out of the log entirely.
const watches = new Map();
const WATCH_HISTORY = 90; // ~3 s at 30 Hz of arrivals; enough for a shape

const el = {
  log: document.getElementById("log"),
  channels: document.getElementById("channels"),
  filter: document.getElementById("filter"),
  levels: document.getElementById("levels"),
  follow: document.getElementById("follow"),
  pause: document.getElementById("pause"),
  clear: document.getElementById("clear"),
  wrap: document.getElementById("wrap"),
  conn: document.getElementById("conn"),
  count: document.getElementById("count"),
  shownCount: document.getElementById("shown"),
  rate: document.getElementById("rate"),
  pulse: document.getElementById("pulse"),
  empty: document.getElementById("empty"),
  watches: document.getElementById("watches"),
  frameBar: document.getElementById("frameBar"),
  frameNo: document.getElementById("frameNo"),
  frameClear: document.getElementById("frameClear"),
  export: document.getElementById("export"),
};

// A debug tool that fails silently is worse than no debug tool: you read an
// empty pane and conclude the engine is quiet. Anything thrown gets said out
// loud, in the status bar, where the connection state already lives.
function fail(what, error) {
  const message = `${what}: ${error && error.message ? error.message : error}`;
  const bar = document.getElementById("conn");
  if (bar) {
    bar.textContent = message;
    bar.classList.remove("live");
    bar.style.color = "var(--red)";
  }
  console.error("[connector]", what, error);
}
window.addEventListener("error", (e) => fail("script error", e.error || e.message));
window.addEventListener("unhandledrejection", (e) => fail("unhandled", e.reason));

// --- ring ------------------------------------------------------------------
function push(record) {
  if (state.records.length < MAX_RECORDS) {
    state.records.push(record);
  } else {
    state.records[state.head] = record;
    state.head = (state.head + 1) % MAX_RECORDS;
    state.full = true;
  }
}

function* inOrder() {
  if (!state.full) {
    yield* state.records;
    return;
  }
  for (let i = 0; i < MAX_RECORDS; i++)
    yield state.records[(state.head + i) % MAX_RECORDS];
}

// --- filtering -------------------------------------------------------------
function parseFilter(text) {
  const trimmed = text.trim();
  if (!trimmed) return null;
  // /.../ is a regex; anything else is a case-insensitive substring. Typing a
  // regex by accident is far more annoying than having to add two slashes.
  const match = /^\/(.*)\/([a-z]*)$/.exec(trimmed);
  if (match) {
    try {
      return { kind: "regex", value: new RegExp(match[1], match[2] || "i") };
    } catch {
      return { kind: "text", value: trimmed.toLowerCase() };
    }
  }
  return { kind: "text", value: trimmed.toLowerCase() };
}

function admits(record) {
  // A watch is a value, not a line: it belongs in the panel and must never
  // scroll, whatever the filters say.
  if (record.kind === "watch" || record.kind === "sample") return false;
  if (state.frameFocus !== null && record.frame !== state.frameFocus) return false;
  // An event is a marker and always shows: it is what you navigate BY.
  if (record.kind === "event") return true;
  if (!state.levels.has(record.lvl)) return false;
  const channel = state.channels.get(record.ch);
  if (channel && !channel.on) return false;
  const f = state.filter;
  if (!f) return true;
  if (f.kind === "regex") return f.value.test(record.msg) || f.value.test(record.ch);
  return (
    record.msg.toLowerCase().includes(f.value) ||
    record.ch.toLowerCase().includes(f.value)
  );
}

// --- rendering -------------------------------------------------------------
function highlight(text) {
  const f = state.filter;
  if (!f) return document.createTextNode(text);
  const fragment = document.createDocumentFragment();
  let pattern;
  if (f.kind === "regex") {
    pattern = new RegExp(f.value.source, f.value.flags.includes("g")
      ? f.value.flags : f.value.flags + "g");
  } else {
    pattern = new RegExp(f.value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "gi");
  }
  let last = 0;
  for (const m of text.matchAll(pattern)) {
    if (m.index === undefined) break;
    if (m.index > last) fragment.append(text.slice(last, m.index));
    const mark = document.createElement("mark");
    mark.textContent = m[0];
    fragment.append(mark);
    last = m.index + m[0].length;
    if (m[0].length === 0) break; // a zero-width match would spin forever
  }
  fragment.append(text.slice(last));
  return fragment;
}

function rowFor(record) {
  const row = document.createElement("div");
  row.className = "row " + record.lvl + (record.kind === "event" ? " event" : "");
  const t = document.createElement("span");
  t.className = "t";
  t.textContent = (record.t / 1000).toFixed(2);
  const frame = document.createElement("span");
  frame.className = "frame";
  frame.textContent = record.frame;
  frame.title = "show only this frame";
  frame.onclick = () => focusFrame(record.frame);
  const ch = document.createElement("span");
  ch.className = "ch";
  ch.textContent = record.ch;
  const msg = document.createElement("span");
  msg.className = "msg";
  // A numeric sample reads as "name = value"; a text record is just the text.
  msg.append(highlight(
    record.v === undefined ? record.msg : `${record.msg} = ${record.v}`));
  row.append(t, frame, ch, msg);
  return row;
}

function appendRows(records) {
  const atBottom =
    el.log.scrollHeight - el.log.scrollTop - el.log.clientHeight < 40;
  const fragment = document.createDocumentFragment();
  let added = 0;
  for (const record of records) {
    if (!admits(record)) continue;
    fragment.append(rowFor(record));
    added++;
  }
  if (!added) return;
  el.log.append(fragment);
  state.shown += added;

  // Recycle from the top. removeChild in a loop is faster here than any
  // clever alternative because the count is small and amortised.
  let excess = el.log.childElementCount - MAX_ROWS;
  while (excess-- > 0 && el.log.firstChild) el.log.removeChild(el.log.firstChild);

  if (state.follow && atBottom) el.log.scrollTop = el.log.scrollHeight;
  el.empty.style.display = "none";
}

function rebuild() {
  el.log.replaceChildren();
  state.shown = 0;
  const admitted = [];
  for (const record of inOrder()) if (admits(record)) admitted.push(record);
  // Only the tail can be on screen; building 200k rows to show 40 would hang
  // the tab for seconds.
  appendRows(admitted.slice(-MAX_ROWS));
  el.empty.style.display = el.log.childElementCount ? "none" : "";
  updateStatus();
}

// --- channels --------------------------------------------------------------
function noteWatch(record) {
  const key = `${record.ch}.${record.name ?? record.msg}`;
  let entry = watches.get(key);
  if (!entry) {
    entry = { history: [], value: "", numeric: false, channel: record.ch };
    watches.set(key, entry);
  }
  if (record.v !== undefined) {
    entry.numeric = true;
    entry.value = Math.abs(record.v) >= 1000
      ? record.v.toFixed(0)
      : record.v.toFixed(2);
    entry.history.push(record.v);
    if (entry.history.length > WATCH_HISTORY) entry.history.shift();
  } else {
    entry.value = record.msg;
  }
  entry.dirty = true;
}

// Redrawn on the animation frame like everything else, and only for entries
// that actually changed -- a watch panel that relayouts sixty times a second
// costs more than the game loop it is reporting on.
function flushWatches() {
  if (!watches.size) return;
  for (const [key, entry] of watches) {
    if (!entry.dirty) continue;
    entry.dirty = false;
    let node = entry.node;
    if (!node) {
      if (el.watches.querySelector(".empty")) el.watches.replaceChildren();
      node = document.createElement("div");
      node.className = "watch";
      const name = document.createElement("span");
      name.className = "wname";
      name.textContent = key;
      const value = document.createElement("span");
      value.className = "wval";
      const spark = document.createElement("canvas");
      spark.width = 136; spark.height = 32; // 2x for crispness on HiDPI
      node.append(name, spark, value);
      entry.node = node;
      entry.valueNode = value;
      entry.spark = spark;
      // Sorted insert, so the panel does not reshuffle as values arrive.
      const siblings = [...el.watches.children];
      const before = siblings.find((n) => n.firstChild.textContent > key);
      el.watches.insertBefore(node, before ?? null);
    }
    entry.valueNode.textContent = entry.value;
    // A channel switched off in the rail hides its watches as well as its
    // lines; otherwise the toggle does nothing visible for a watch-only
    // channel, which is exactly the case that made the rail confusing.
    const channel = state.channels.get(entry.channel);
    node.hidden = channel ? !channel.on : false;
    if (entry.numeric) drawSpark(entry);
  }
}

function drawSpark(entry) {
  const ctx = entry.spark.getContext("2d");
  const w = entry.spark.width, h = entry.spark.height;
  ctx.clearRect(0, 0, w, h);
  const points = entry.history;
  if (points.length < 2) return;
  let lo = Infinity, hi = -Infinity;
  for (const v of points) { if (v < lo) lo = v; if (v > hi) hi = v; }
  const span = hi - lo || 1;
  ctx.beginPath();
  points.forEach((v, i) => {
    const x = (i / (points.length - 1)) * w;
    const y = h - ((v - lo) / span) * (h - 3) - 1.5;
    i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
  });
  ctx.strokeStyle = "#d03a3a";
  ctx.lineWidth = 2;
  ctx.stroke();
}

// Counted separately, because they are not the same thing to a reader. A
// channel that only ever emits watches ("frame", "render") has no lines to
// show, and a rail that advertised "540" next to an empty pane was the UI
// lying about what a click would do.
function noteChannel(name, record) {
  let entry = state.channels.get(name);
  if (!entry) {
    entry = { on: true, lines: 0, watches: 0 };
    state.channels.set(name, entry);
    renderChannels();
  }
  const isValue = record.kind === "watch" || record.kind === "sample";
  if (isValue) {
    // Distinct watches, not arrivals: "render has 2 values" is the useful
    // number, where "render has sent 540 updates" is just uptime.
    entry.watches = new Set([
      ...(entry.names ?? []),
      record.name ?? record.msg,
    ]).size;
    entry.names = [...(entry.names ?? []), record.name ?? record.msg];
    if (entry.names.length > 64) entry.names = [...new Set(entry.names)];
  } else {
    entry.lines++;
  }
  entry.dirty = true;
}

let channelsDirty = false;
function renderChannels() {
  channelsDirty = true;
}
function flushChannels() {
  if (!channelsDirty) return;
  channelsDirty = false;
  const names = [...state.channels.keys()].sort();
  if (!names.length) return;
  el.channels.replaceChildren();
  for (const name of names) {
    const entry = state.channels.get(name);
    const row = document.createElement("div");
    row.className = "channel " + (entry.on ? "on" : "off");
    const label = document.createElement("span");
    label.className = "name";
    label.textContent = name;
    const count = document.createElement("span");
    count.className = "count";
    if (entry.lines > 0) {
      count.textContent = entry.lines > 9999 ? "9k+" : entry.lines;
    } else if (entry.watches > 0) {
      // A tilde, and dimmer: these are values in the panel above, not lines
      // waiting in the log. Toggling still works -- it hides the watches.
      count.textContent = "~" + entry.watches;
      count.classList.add("values");
      row.title = `${entry.watches} watched value(s); no log lines`;
    } else {
      count.textContent = "0";
    }
    row.append(label, count);
    row.onclick = (event) => {
      // Alt-click solos, because "just this one" is the common intent and
      // unticking eleven others by hand is not a workflow.
      if (event.altKey) {
        const solo = state.channels.get(name).on &&
          [...state.channels.values()].filter((c) => c.on).length === 1;
        for (const [key, value] of state.channels) value.on = solo || key === name;
      } else {
        entry.on = !entry.on;
      }
      renderChannels();
      flushChannels();
      for (const w of watches.values()) w.dirty = true; // re-evaluate visibility
      flushWatches();
      rebuild();
    };
    el.channels.append(row);
  }
}

function focusFrame(number) {
  state.frameFocus = number;
  el.frameBar.hidden = false;
  el.frameNo.textContent = number;
  rebuild();
}

// --- status ----------------------------------------------------------------
function updateStatus() {
  el.count.textContent = state.received.toLocaleString();
  el.shownCount.textContent = el.log.childElementCount.toLocaleString();
}

setInterval(() => {
  const now = performance.now();
  state.rateWindow = state.rateWindow.filter((t) => now - t < 1000);
  el.rate.textContent = `${state.rateWindow.length}/s`;
  el.pulse.classList.toggle("idle", state.rateWindow.length === 0);
}, 250);

// --- the stream ------------------------------------------------------------
function connect() {
  const source = new EventSource("/api/stream");
  source.onopen = () => {
    el.conn.textContent = "connected";
    el.conn.classList.add("live");
  };
  source.onerror = () => {
    el.conn.textContent = "reconnecting…";
    el.conn.classList.remove("live");
    // EventSource retries on its own; saying so is all this has to do.
  };
  source.onmessage = (event) => {
    let record;
    try {
      record = JSON.parse(event.data);
    } catch {
      return;
    }
    state.received++;
    state.rateWindow.push(performance.now());
    noteChannel(record.ch, record);
    if (record.kind === "watch" || record.kind === "sample") {
      noteWatch(record);
      return; // never enters the ring: it would evict real log lines
    }
    push(record);
    if (!state.paused) state.pending.push(record);
  };
}

function frame() {
  flushChannels();
  flushWatches();
  if (state.pending.length) {
    appendRows(state.pending);
    state.pending.length = 0;
    updateStatus();
  }
  requestAnimationFrame(frame);
}

// --- wiring ----------------------------------------------------------------
el.filter.addEventListener("input", () => {
  state.filter = parseFilter(el.filter.value);
  rebuild();
});
el.levels.addEventListener("click", (event) => {
  const button = event.target.closest("button");
  if (!button) return;
  const level = button.dataset.level;
  if (state.levels.has(level)) state.levels.delete(level);
  else state.levels.add(level);
  button.classList.toggle("on", state.levels.has(level));
  rebuild();
});
el.follow.onclick = () => {
  state.follow = !state.follow;
  el.follow.classList.toggle("on", state.follow);
  if (state.follow) el.log.scrollTop = el.log.scrollHeight;
};
el.pause.onclick = () => {
  state.paused = !state.paused;
  el.pause.classList.toggle("on", state.paused);
  el.pause.textContent = state.paused ? "resume" : "pause";
  // Records keep arriving into the ring while paused; resuming rebuilds so
  // nothing was actually missed, which is the difference between pause and
  // disconnect.
  if (!state.paused) rebuild();
};
el.wrap.onclick = () => {
  const on = el.wrap.classList.toggle("on");
  el.log.style.whiteSpace = on ? "pre-wrap" : "nowrap";
};
el.frameClear.onclick = () => {
  state.frameFocus = null;
  el.frameBar.hidden = true;
  rebuild();
};

// Export what is on screen, filters and all. A bug report is "here is the log
// around it", and making someone screenshot a terminal for that is why bug
// reports arrive without one.
el.export.onclick = () => {
  const lines = [...el.log.children].map((row) =>
    [...row.children].map((c) => c.textContent).join("  ")).join("\n");
  const blob = new Blob([lines], { type: "text/plain" });
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  const when = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
  link.download = `connector-${when}.log`;
  link.click();
  URL.revokeObjectURL(link.href);
};

el.clear.onclick = async () => {
  state.records = [];
  state.head = 0;
  state.full = false;
  state.received = 0;
  for (const entry of state.channels.values()) {
    entry.lines = 0;
    entry.watches = 0;
    entry.names = [];
  }
  watches.clear();
  el.watches.replaceChildren();
  el.log.replaceChildren();
  el.empty.style.display = "";
  renderChannels();
  updateStatus();
  try {
    await fetch("/api/clear", { method: "POST" });
  } catch { /* the server may be down; the local clear still stands */ }
};

// Scrolling up turns follow off, which is what every log viewer does and what
// everyone expects without being told.
el.log.addEventListener("scroll", () => {
  const atBottom =
    el.log.scrollHeight - el.log.scrollTop - el.log.clientHeight < 40;
  if (!atBottom && state.follow) {
    state.follow = false;
    el.follow.classList.remove("on");
  }
});

document.addEventListener("keydown", (event) => {
  if (event.target.tagName === "INPUT") {
    if (event.key === "Escape") { el.filter.value = ""; state.filter = null; rebuild(); }
    return;
  }
  if (event.key === "/") { event.preventDefault(); el.filter.focus(); }
  if (event.key === " ") { event.preventDefault(); el.pause.click(); }
  if (event.key === "f") el.follow.click();
});

// Scrollback first, so opening Connector mid-session shows how the run got
// here rather than an empty pane.
// ?nostream=1 loads the scrollback and stops there, with no live connection.
// Useful for reading a captured session without the tail moving under you --
// and it is what makes the UI screenshot-testable, because a page holding an
// open EventSource never reaches network-idle.
const params = new URLSearchParams(location.search);
const LIVE = params.get("nostream") !== "1";

(async function boot() {
  try {
    const response = await fetch("/api/history?limit=4000");
    const { rows } = await response.json();
    for (const row of rows) {
      try {
        const record = JSON.parse(row);
        state.received++;
        noteChannel(record.ch, record);
        if (record.kind === "watch" || record.kind === "sample") noteWatch(record);
        else push(record);
      } catch { /* a truncated row is not worth failing the boot over */ }
    }
    flushChannels();
    rebuild();
  } catch (error) {
    fail("history", error);
  }
  if (LIVE) {
    connect();
  } else {
    el.conn.textContent = "snapshot (no live stream)";
  }
  requestAnimationFrame(frame);
})();
