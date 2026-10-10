# Writes art/grain.dds: a 128x128 tiling grain tile (black, noise in alpha) as an
# uncompressed B8G8R8A8 DDS, the format SKSE Menu Framework's DDS loader takes as-is.
# The widget draws it over each bar fill at 32% opacity with screen-space UVs.
import os
import struct
import sys

import numpy as np

SIZE = 128
rng = np.random.default_rng(7)


def tile_noise(cells):
    # Smooth value noise that tiles: random grid, wrapped bilinear upsampling.
    g = rng.random((cells, cells))
    y, x = np.mgrid[0:SIZE, 0:SIZE] * (cells / SIZE)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
    top = g[y0 % cells, x0 % cells] * (1 - fx) + g[y0 % cells, x1] * fx
    bot = g[y1, x0 % cells] * (1 - fx) + g[y1, x1] * fx
    return top * (1 - fy) + bot * fy


n = 0.55 * rng.random((SIZE, SIZE)) + 0.30 * tile_noise(64) + 0.15 * tile_noise(16)
n = (n - n.min()) / (n.max() - n.min())
alpha = np.clip((0.15 + 0.85 * n) * 0.9 * 255.0, 0, 255).astype(np.uint8)

px = np.zeros((SIZE, SIZE, 4), np.uint8)  # B, G, R stay 0 (black)
px[..., 3] = alpha

header = struct.pack(
    "<4s7I44x8I5I",
    b"DDS ",
    124,                      # dwSize
    0x1 | 0x2 | 0x4 | 0x8 | 0x1000,  # CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    SIZE, SIZE,               # height, width
    SIZE * 4,                 # pitch
    0, 0,                     # depth, mip count
    32, 0x41, 0, 32,          # pixelformat: size, ALPHAPIXELS|RGB, fourCC, bit count
    0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000,  # R, G, B, A masks
    0x1000, 0, 0, 0, 0,       # caps TEXTURE, caps2-4, reserved2
)
assert len(header) == 128, len(header)

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "art", "grain.dds")
with open(out, "wb") as f:
    f.write(header)
    f.write(px.tobytes())
print("wrote", os.path.abspath(out), os.path.getsize(out), "bytes")
