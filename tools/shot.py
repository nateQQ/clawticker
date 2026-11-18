#!/usr/bin/env python3
"""Capture the board's screen over USB serial and save it as PNG.
Usage: python3 tools/shot.py /dev/cu.usbmodemXXXX out.png [--next N] [--settings]
Needs pyserial and Pillow (pip install pyserial pillow)."""
import sys, time
import serial
from PIL import Image

port, out = sys.argv[1], sys.argv[2]
presses = int(sys.argv[sys.argv.index("--next") + 1]) if "--next" in sys.argv else 0
s = serial.Serial(port, 115200, timeout=2)
time.sleep(0.3)
for _ in range(presses):
    s.write(b"next\n"); time.sleep(0.8)
if "--settings" in sys.argv:
    s.write(b"settings\n"); time.sleep(1.0)
s.reset_input_buffer()
s.write(b"shot\n")
while True:
    line = s.readline().decode(errors="replace")
    if line.startswith("SHOT"):
        _, w, h = line.split(); w, h = int(w), int(h); break
raw = b""
while len(raw) < w * h * 2:
    raw += s.read(w * h * 2 - len(raw))
img = Image.new("RGB", (w, h))
px = img.load()
for i in range(w * h):
    v = raw[2 * i] | (raw[2 * i + 1] << 8)
    px[i % w, i // w] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
img.resize((w * 2, h * 2), Image.NEAREST).save(out)
print("saved", out)
