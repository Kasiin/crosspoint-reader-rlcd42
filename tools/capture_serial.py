"""Capture the RLCD42 board's USB-CDC serial output for N seconds.

Usage: python tools/capture_serial.py [port] [seconds] [outfile]
Defaults: COM19, 10s, print to stdout.
"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM19"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
outfile = sys.argv[3] if len(sys.argv) > 3 else None

ser = serial.Serial(port, 115200, timeout=0.5)
ser.dtr = False
ser.rts = False
lines = []
deadline = time.time() + seconds
buf = b""
while time.time() < deadline:
    chunk = ser.read(4096)
    if chunk:
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").rstrip()
            lines.append(text)
            if not outfile:
                print(text)
ser.close()
if outfile:
    with open(outfile, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"captured {len(lines)} lines -> {outfile}")
