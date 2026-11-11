#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""How each of Amethyst's four knobs could change the stage's picture (docs/AMETHYST.md): a sketch to choose from,
made only of things the FM-1 can do as it draws a row: shift the row sideways (WOBBLE), mix the row's 16 colors
toward a fog color (FOG), draw in bigger blocks with fewer colors and torn rows (GRIT), let the moss reach further
and turn the colors green (MOSS).

  python3 tools/amethyst/knob_looks.py     -> docs/amethyst/knobs/sheet.png

The photo is docs/amethyst/gemini/the-end-moss.jpg (steve_w, CC BY 2.0, changed with Gemini)."""
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

import mockups as mk

ROOT = mk.ROOT
S = 240
SRC = ROOT / "docs" / "amethyst" / "gemini"


def base16():
    im = Image.open(SRC / "the-end-moss.jpg").convert("RGB").resize((S, S), Image.Resampling.LANCZOS)
    return np.asarray(im.quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert("RGB"), np.float32)


def moss_layer():
    im = Image.open(SRC / "layer-moss.jpg").convert("RGB")
    w, h = im.size
    a = np.asarray(im.crop((40, 40, w - 40, h - 40)).resize((S, S), Image.Resampling.LANCZOS), np.float32)
    return a, (a @ np.array([0.3, 0.59, 0.11], np.float32)) > 26


def look(wobble=0, fog=0, grit=0, moss=0, t=0.0):
    """each knob 0..100"""
    a = base16().copy()
    r = mk.rng(7)
    if moss:                                               # the moss reaches as far as the knob says; all turns green
        m, has = moss_layer()
        yy, xx = np.mgrid[0:S, 0:S]
        noise = np.asarray(Image.fromarray((r.random((30, 30)) * 255).astype(np.uint8)).resize((S, S), Image.Resampling.BICUBIC), np.float32) / 255
        reach = (1 - yy / S) * 0.75 + np.minimum(xx, S - 1 - xx) / S * 0.6 + noise * 0.25
        on = has & (reach < moss / 100 * 1.25)
        a[on] = m[on]
        k = moss / 100 * 0.5
        a = a * (1 - k) + a * np.array([0.55, 1.05, 0.5], np.float32) * k
    if grit:                                               # bigger blocks, fewer colors, torn rows, specks
        b = 1 + grit * 3 // 100
        if b > 1:
            a = a[::b, ::b].repeat(b, 0).repeat(b, 1)[:S, :S]
        lev = 2 ** (5 - grit * 3 // 100)
        a = np.round(a / 255 * lev) / lev * 255
        for y in r.integers(0, S, grit // 6):
            h = int(r.integers(1, 4))
            a[y:y + h] = np.roll(a[y:y + h], int(r.integers(-30, 31)), axis=1)
        sp = r.random((S, S)) < grit / 100 * 0.05
        a[sp] = 255 - a[sp]
    if fog:                                                # each row's colors toward the fog, thickest at the street
        y = np.arange(S, dtype=np.float32)
        al = np.clip((y - 40) / 200, 0, 1) ** 0.8 * fog / 100 * 0.95
        al += 0.12 * fog / 100 * np.sin(y / 9 + t * 2)       # it drifts in bands
        al = np.clip(al, 0, 0.97)[:, None, None]
        a = a * (1 - al) + np.array([176, 166, 204], np.float32) * al
    if wobble:                                             # the rows sway with the bass
        out = a.copy()
        for y in range(S):
            dx = int(round(wobble / 100 * 12 * math.sin(y / 13 + t * 6) * (0.4 + 0.6 * y / S)))
            out[y] = np.roll(a[y], dx, axis=0)
        a = out
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


def tag(im, f, text):
    d = ImageDraw.Draw(im)
    w = d.textlength(text, font=mk.font(f, 16)) + 14
    d.rectangle([6, 6, 6 + w, 30], fill=(12, 10, 10), outline=(110, 96, 70))
    d.text((13, 8), text, font=mk.font(f, 16), fill=(236, 228, 210))
    return im


def main():
    f = mk.find_font(None)
    shots = [tag(look(), f, "ALL AT 0"), tag(look(wobble=100, t=0.3), f, "WOBBLE 100"), tag(look(fog=100), f, "FOG 100"),
             tag(look(grit=100), f, "GRIT 100"), tag(look(moss=100), f, "MOSS 100"),
             tag(look(60, 60, 60, 60, 0.3), f, "ALL AT 60")]
    gap = 8
    sheet = Image.new("RGB", (3 * (S * 2 + gap) + gap, 2 * (S * 2 + gap) + gap), (12, 10, 18))
    for i, im in enumerate(shots):
        sheet.paste(im.resize((S * 2, S * 2), Image.Resampling.NEAREST), (gap + (i % 3) * (S * 2 + gap), gap + (i // 3) * (S * 2 + gap)))
    out = ROOT / "docs" / "amethyst" / "knobs"
    out.mkdir(parents=True, exist_ok=True)
    sheet.save(out / "sheet.png")
    print(out / "sheet.png")


if __name__ == "__main__":
    main()
