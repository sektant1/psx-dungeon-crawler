#!/usr/bin/env python3
"""Compare two PNGs, for the visual regression gate.

Why this exists rather than Pillow: Pillow is not installed here and pulling a
build dependency into the visual gate makes the gate the thing most likely to
break. numpy is present, and the images being compared come from exactly one
encoder -- stbi_write_png, 8-bit non-interlaced RGB or RGBA -- so the decoder
below only has to handle that. It is deliberately strict: anything it does not
recognise raises rather than guessing, because a silently mis-decoded image
would make the gate report a difference that is not there.

Usable as a library (`load_png`, `compare`) or a CLI:

    tools/image_diff.py golden.png candidate.png --tolerance 0
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def load_png(path: Path) -> np.ndarray:
    """Decode a PNG into an (h, w, channels) uint8 array."""
    raw = path.read_bytes()
    if not raw.startswith(PNG_MAGIC):
        raise ValueError(f"{path}: not a PNG")

    width = height = bit_depth = colour_type = None
    idat = bytearray()
    offset = len(PNG_MAGIC)
    while offset < len(raw):
        (length,) = struct.unpack(">I", raw[offset : offset + 4])
        kind = raw[offset + 4 : offset + 8]
        body = raw[offset + 8 : offset + 8 + length]
        offset += 12 + length  # length + type + data + crc

        if kind == b"IHDR":
            width, height, bit_depth, colour_type, comp, filt, interlace = (
                struct.unpack(">IIBBBBB", body)
            )
            if bit_depth != 8:
                raise ValueError(f"{path}: bit depth {bit_depth}, expected 8")
            if colour_type not in (2, 6):
                raise ValueError(
                    f"{path}: colour type {colour_type}, expected 2 (RGB) or 6 (RGBA)"
                )
            if interlace != 0:
                raise ValueError(f"{path}: interlaced PNGs are not supported")
            if comp != 0 or filt != 0:
                raise ValueError(f"{path}: unexpected compression/filter method")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break

    if width is None:
        raise ValueError(f"{path}: no IHDR chunk")

    channels = 3 if colour_type == 2 else 4
    data = zlib.decompress(bytes(idat))
    stride = width * channels

    # Undo the per-scanline filter. Each row is prefixed with its filter type,
    # and filters reference the pixel to the left (a), above (b) and
    # above-left (c) -- see the PNG spec, section 9.
    out = np.zeros((height, stride), dtype=np.uint8)
    previous = np.zeros(stride, dtype=np.uint8)
    pos = 0
    for row in range(height):
        filter_type = data[pos]
        pos += 1
        line = np.frombuffer(data, dtype=np.uint8, count=stride, offset=pos).copy()
        pos += stride

        if filter_type == 0:  # None
            pass
        elif filter_type == 1:  # Sub
            for i in range(channels, stride):
                line[i] = (int(line[i]) + int(line[i - channels])) & 0xFF
        elif filter_type == 2:  # Up
            line = ((line.astype(np.int32) + previous.astype(np.int32)) & 0xFF).astype(
                np.uint8
            )
        elif filter_type == 3:  # Average
            for i in range(stride):
                left = int(line[i - channels]) if i >= channels else 0
                line[i] = (int(line[i]) + ((left + int(previous[i])) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(stride):
                a = int(line[i - channels]) if i >= channels else 0
                b = int(previous[i])
                c = int(previous[i - channels]) if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (int(line[i]) + pred) & 0xFF
        else:
            raise ValueError(f"{path}: unknown filter type {filter_type}")

        out[row] = line
        previous = line

    return out.reshape(height, width, channels)


class Difference:
    def __init__(self, a: np.ndarray, b: np.ndarray):
        self.shape_a = a.shape
        self.shape_b = b.shape
        self.same_shape = a.shape == b.shape
        if not self.same_shape:
            self.max_channel = 255
            self.differing_pixels = -1
            self.total_pixels = -1
            self.differing_fraction = 1.0
            return

        delta = np.abs(a.astype(np.int16) - b.astype(np.int16))
        self.max_channel = int(delta.max()) if delta.size else 0
        per_pixel = delta.max(axis=2)
        self.differing_pixels = int((per_pixel > 0).sum())
        self.total_pixels = int(per_pixel.size)
        self.differing_fraction = (
            self.differing_pixels / self.total_pixels if self.total_pixels else 0.0
        )

    def identical(self) -> bool:
        return self.same_shape and self.max_channel == 0

    def describe(self) -> str:
        if not self.same_shape:
            return f"size changed: {self.shape_a} -> {self.shape_b}"
        if self.identical():
            return "identical"
        return (
            f"{self.differing_pixels}/{self.total_pixels} pixels differ "
            f"({self.differing_fraction * 100:.4f}%), "
            f"max channel delta {self.max_channel}"
        )

    def as_dict(self) -> dict[str, object]:
        return {
            "identical": self.identical(),
            "same_shape": self.same_shape,
            "max_channel_delta": self.max_channel,
            "differing_pixels": self.differing_pixels,
            "total_pixels": self.total_pixels,
            "differing_fraction": self.differing_fraction,
            "summary": self.describe(),
        }


def compare(golden: Path, candidate: Path) -> Difference:
    return Difference(load_png(golden), load_png(candidate))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("golden", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument(
        "--tolerance",
        type=int,
        default=0,
        help="largest per-channel delta treated as unchanged (default 0: exact)",
    )
    args = parser.parse_args()

    diff = compare(args.golden, args.candidate)
    print(diff.describe())
    if not diff.same_shape:
        return 1
    return 0 if diff.max_channel <= args.tolerance else 1


if __name__ == "__main__":
    raise SystemExit(main())
