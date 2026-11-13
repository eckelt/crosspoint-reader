#!/usr/bin/env python3
"""Rasterizes the SVGs next to this script to the 20x20 title-row icons and
prints them as C arrays for src/deskleaf/EventIcons.h. Needs rsvg-convert and
Pillow. teams/meet/ne/zoom.svg come from deskleaf-for-obsidian/resources."""
import io
import os
import subprocess

from PIL import Image

N = 20
HERE = os.path.dirname(os.path.abspath(__file__))
# name -> (C name, size of the longer side inside the 20 px box)
ICONS = {
    'teams': ('kTeams', 18), 'meet': ('kMeet', 20), 'ne': ('kNE', 18), 'linkedin': ('kLinkedIn', 16),
    'pin': ('kOnSite', 19), 'phone': ('kPhone', 17), 'series': ('kSeries', 18),
    'series_moved': ('kSeriesMoved', 18), 'attachment': ('kAttachment', 18),
    'zoom': ('kZoom', 18), 'webex': ('kWebex', 18),
}

for name, (cname, size) in ICONS.items():
    png = subprocess.run(['rsvg-convert', '-w', '400', '-h', '400', '--keep-aspect-ratio',
                          os.path.join(HERE, name + '.svg')], capture_output=True, check=True).stdout
    rgba = Image.open(io.BytesIO(png)).convert('RGBA')
    # Ink = opaque and dark, so white strokes inside a shape stay open.
    lum = rgba.convert('L')
    alpha = Image.composite(Image.eval(lum, lambda v: 255 - v), Image.new('L', rgba.size, 0), rgba.split()[3])
    if name == 'meet':  # drop the half-transparent separator
        alpha = alpha.point(lambda v: 255 if v > 200 else 0)
    alpha = alpha.crop(alpha.getbbox())
    scale = size / max(alpha.size)
    dim = (max(1, round(alpha.size[0] * scale)), max(1, round(alpha.size[1] * scale)))
    alpha = alpha.resize(dim, Image.LANCZOS)
    box = Image.new('L', (N, N), 0)
    box.paste(alpha, ((N - dim[0]) // 2, (N - dim[1]) // 2))
    data = []
    for y in range(N):
        bits = ''.join('1' if box.getpixel((x, y)) > 110 else '0' for x in range(N)) + '0000'
        v = int(bits, 2)
        data += [v >> 16 & 255, v >> 8 & 255, v & 255]
    print(f'inline constexpr unsigned char {cname}[] = {{{", ".join("0x%02X" % b for b in data)}}};')

# Header: a sync running over Bluetooth or Wi-Fi, 24x24, the whole viewBox
# scaled (no crop), so ring and symbol keep their drawn proportions.
for name, cname in (('sync_bluetooth', 'kSyncBluetooth'), ('sync_wifi', 'kSyncWifi')):
    S = 24
    png = subprocess.run(['rsvg-convert', '-w', str(S * 8), '-h', str(S * 8), os.path.join(HERE, name + '.svg')],
                         capture_output=True, check=True).stdout
    rgba = Image.open(io.BytesIO(png)).convert('RGBA')
    alpha = rgba.split()[3].resize((S, S), Image.LANCZOS)
    data = []
    for y in range(S):
        v = int(''.join('1' if alpha.getpixel((x, y)) > 100 else '0' for x in range(S)), 2)
        data += [v >> 16 & 255, v >> 8 & 255, v & 255]
    print(f'inline constexpr unsigned char {cname}[] = {{{", ".join("0x%02X" % b for b in data)}}};')
