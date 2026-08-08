#!/usr/bin/env python3
"""Read and write 8-bit PNGs exactly, with no image library.

The tools in here move pixel art around: atlasing two 256x256 hand textures
into one, and reading a heightmap into a terrain. Both need the bytes to come
out the other side unchanged, and both run on a machine with no Pillow.

Blender can load and save PNGs, but it converts to float and back through its
colour management on the way, which is a round trip that does not have to be
lossless and is invisible when it is not. PSX pixel art is flat indexed colour
where a single shifted value is a visible band, so this decodes and encodes the
format directly instead.

Supported: bit depth 8, colour types 0 (grey), 2 (RGB), 3 (palette), 4 (grey +
alpha) and 6 (RGBA), non-interlaced -- which is every PNG in assets/source.
Anything else raises, rather than returning something subtly wrong.
"""

from __future__ import annotations

import struct
import zlib

import numpy as np

_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def read(path: str) -> np.ndarray:
    """PNG -> an (h, w, 4) uint8 RGBA array."""
    with open(path, "rb") as stream:
        data = stream.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG: " + path)

    pos = 8
    header = None
    palette = None
    transparency = None
    idat = bytearray()
    while pos < len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        pos += 8
        chunk = data[pos:pos + length]
        pos += length + 4  # skip the CRC: zlib will catch a corrupt stream
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"PLTE":
            palette = np.frombuffer(chunk, np.uint8).reshape(-1, 3)
        elif kind == b"tRNS":
            transparency = np.frombuffer(chunk, np.uint8)
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break

    width, height, depth, colour, compression, filt, interlace = header
    if depth != 8 or interlace != 0 or colour not in _CHANNELS:
        raise ValueError("unsupported PNG (depth=%d colour=%d interlace=%d): %s"
                         % (depth, colour, interlace, path))

    channels = _CHANNELS[colour]
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    out = np.empty((height, stride), np.uint8)

    # Un-filter. Each scanline carries a filter byte and is predicted from the
    # pixel to its left and the line above, so this cannot be vectorised across
    # rows -- but within a row, filters 0-2 can be, and those are the common
    # ones by a wide margin.
    previous = np.zeros(stride, np.uint8)
    at = 0
    for y in range(height):
        method = raw[at]
        at += 1
        line = np.frombuffer(raw, np.uint8, stride, at).copy()
        at += stride
        if method == 0:
            pass
        elif method == 1:
            _unfilter_sub(line, channels)
        elif method == 2:
            line += previous
        elif method == 3:
            _unfilter_average(line, previous, channels)
        elif method == 4:
            _unfilter_paeth(line, previous, channels)
        else:
            raise ValueError("bad PNG filter %d in %s" % (method, path))
        out[y] = line
        previous = line

    pixels = out.reshape(height, width, channels)
    if colour == 3:
        if palette is None:
            raise ValueError("indexed PNG with no palette: " + path)
        index = pixels[:, :, 0]
        rgb = palette[index]
        alpha = np.full((height, width, 1), 255, np.uint8)
        if transparency is not None:
            table = np.full(len(palette), 255, np.uint8)
            table[: len(transparency)] = transparency
            alpha = table[index][:, :, None]
        return np.concatenate([rgb, alpha], axis=2)
    if colour == 0:
        grey = pixels
        return np.concatenate(
            [grey, grey, grey, np.full_like(grey, 255)], axis=2)
    if colour == 4:
        grey = pixels[:, :, :1]
        return np.concatenate([grey, grey, grey, pixels[:, :, 1:]], axis=2)
    if colour == 2:
        return np.concatenate(
            [pixels, np.full((height, width, 1), 255, np.uint8)], axis=2)
    return pixels


def _unfilter_sub(line: np.ndarray, channels: int) -> None:
    for i in range(channels, len(line)):
        line[i] = (int(line[i]) + int(line[i - channels])) & 0xFF


def _unfilter_average(line: np.ndarray, prev: np.ndarray, channels: int) -> None:
    for i in range(len(line)):
        left = int(line[i - channels]) if i >= channels else 0
        line[i] = (int(line[i]) + ((left + int(prev[i])) >> 1)) & 0xFF


def _unfilter_paeth(line: np.ndarray, prev: np.ndarray, channels: int) -> None:
    for i in range(len(line)):
        left = int(line[i - channels]) if i >= channels else 0
        up = int(prev[i])
        upleft = int(prev[i - channels]) if i >= channels else 0
        p = left + up - upleft
        pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
        if pa <= pb and pa <= pc:
            best = left
        elif pb <= pc:
            best = up
        else:
            best = upleft
        line[i] = (int(line[i]) + best) & 0xFF


def write(path: str, pixels: np.ndarray) -> None:
    """An (h, w, 3|4) uint8 array -> a PNG.

    Written unfiltered. These are 256-512 px atlases where the compression a
    filter would buy is a few kilobytes, and an unfiltered stream is one this
    file's own reader can be trusted with.
    """
    pixels = np.ascontiguousarray(pixels, np.uint8)
    height, width, channels = pixels.shape
    if channels not in (3, 4):
        raise ValueError("write() wants RGB or RGBA")
    colour = 2 if channels == 3 else 6

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += pixels[y].tobytes()

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR",
                      struct.pack(">IIBBBBB", width, height, 8, colour, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def atlas(images: list, cell: int = 0) -> np.ndarray:
    """Lay images out in one horizontal strip of equal cells.

    A strip rather than a grid: the UV remap is then `u' = (u + i) / n` with v
    untouched, which is one multiply-add per vertex and readable in the file
    that does it. A grid would save texture memory on a 256-pixel atlas that
    costs 512 KB either way.
    """
    if not images:
        raise ValueError("atlas() needs at least one image")
    size = cell or max(max(image.shape[0], image.shape[1]) for image in images)
    out = np.zeros((size, size * len(images), 4), np.uint8)
    for index, image in enumerate(images):
        if image.shape[0] != size or image.shape[1] != size:
            image = _nearest_resize(image, size, size)
        out[:, index * size:(index + 1) * size] = image
    return out


def _nearest_resize(image: np.ndarray, width: int, height: int) -> np.ndarray:
    """Nearest neighbour, because these are pixel art.

    Any interpolation here invents colours the palette does not contain, which
    is the one thing the shipped look cannot absorb.
    """
    ys = (np.arange(height) * image.shape[0] // height).clip(0, image.shape[0] - 1)
    xs = (np.arange(width) * image.shape[1] // width).clip(0, image.shape[1] - 1)
    return image[ys][:, xs]
