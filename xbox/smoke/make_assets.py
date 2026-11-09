#!/usr/bin/env python3
"""Generate copyright-free, minimal PNG assets required by the UWP manifest."""
from pathlib import Path
import struct
import zlib

OUT = Path(__file__).resolve().parent / "Assets"


def png(width: int, height: int) -> bytes:
    scanlines = bytearray()
    for y in range(height):
        scanlines.append(0)  # PNG filter type: None
        for x in range(width):
            # A bright central X on a blue background: recognizable even at 44x44.
            diagonal = abs(x - y) < max(width // 22, 2) or abs((width - 1 - x) - y) < max(width // 22, 2)
            if diagonal and width == height:
                scanlines.extend((95, 228, 176, 255))
            else:
                scanlines.extend((24, 51 + (y * 25 // height), 71, 255))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(scanlines))
        + chunk(b"IEND", b"")
    )


def main() -> None:
    OUT.mkdir(exist_ok=True)
    for name, dims in {
        "StoreLogo.png": (50, 50),
        "Square150x150Logo.png": (150, 150),
        "Square44x44Logo.png": (44, 44),
        "SplashScreen.png": (620, 300),
    }.items():
        width, height = dims
        (OUT / name).write_bytes(png(width, height))
        print(f"generated {name} ({width}x{height})")


if __name__ == "__main__":
    main()
