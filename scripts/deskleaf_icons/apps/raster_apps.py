#!/usr/bin/env python3
"""Rasterizes the app tiles' 56x56 icons (Werkzeuge springboard) to C arrays
for src/deskleaf/AppIcons.h. Needs rsvg-convert and Pillow."""
import io
import os
import subprocess

from PIL import Image

S = 56
HERE = os.path.dirname(os.path.abspath(__file__))
for name, cname in (('calendar', 'kAppCalendar'), ('clicker', 'kAppClicker'), ('qr', 'kAppQr')):
    png = subprocess.run(['rsvg-convert', '-w', str(S * 8), '-h', str(S * 8), os.path.join(HERE, name + '.svg')],
                         capture_output=True, check=True).stdout
    alpha = Image.open(io.BytesIO(png)).convert('RGBA').split()[3].resize((S, S), Image.LANCZOS)
    data = []
    for y in range(S):
        bits = ''.join('1' if alpha.getpixel((x, y)) > 110 else '0' for x in range(S))
        data += [int(bits[i:i + 8], 2) for i in range(0, S, 8)]
    print(f'inline constexpr unsigned char {cname}[] = {{{", ".join("0x%02X" % b for b in data)}}};')
