#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_assets.py - Convert pre-boot artwork and fonts into RGB565 / C headers.

Reads (all optional, in priority order for the font):
    assets/BgTile.png          - tiled background (any small PNG, usually 1x scale)
    assets/happymacicon.png    - centered icon (RGBA PNG with transparency)
    assets/fonts/pixChicago.ttf - pixel-perfect Chicago TTF (preferred)
    assets/fonts/chicago.bdf   - Chicago proportional bitmap font (BDF)

Writes:
    src/generated/asset_bg_tile.h
    src/generated/asset_happy_mac.h
    src/generated/chicago_font_data.h

If a source asset is missing, a compiled-in classic default is used so the
firmware always builds. Drop real assets into `assets/` to override any of them.

Also runnable standalone for CI / manual invocation:
    python3 scripts/build_assets.py

Designed to also run as a PlatformIO `pre:` script. It auto-registers an
AlwaysBuild hook when `Import("env")` is available.
"""

import os
import sys
import time

# ---------------------------------------------------------------------------
# Paths
#
# When invoked by PlatformIO via SConscript, `__file__` isn't defined in the
# exec'd globals, so we probe a few sources to find the project root. When
# run standalone (python3 scripts/build_assets.py), `__file__` works fine.
# ---------------------------------------------------------------------------

# Pick up the PIO env as early as possible so we can read PROJECT_DIR from it.
_PIO_ENV = None
try:
    Import("env")          # type: ignore[name-defined] # noqa: F821
    _PIO_ENV = env          # type: ignore[name-defined] # noqa: F821
except Exception:
    _PIO_ENV = None


def _resolve_project_root():
    if _PIO_ENV is not None:
        try:
            p = _PIO_ENV["PROJECT_DIR"]
            if p:
                return p
        except Exception:
            pass
    try:
        return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    except NameError:
        return os.getcwd()


_PROJECT_ROOT = _resolve_project_root()
_SCRIPTS_DIR = os.path.join(_PROJECT_ROOT, "scripts")
_THIS = os.path.join(_SCRIPTS_DIR, "build_assets.py")

ASSETS_DIR   = os.path.join(_PROJECT_ROOT, "assets")
FONTS_DIR    = os.path.join(ASSETS_DIR, "fonts")
GEN_DIR      = os.path.join(_PROJECT_ROOT, "src", "generated")

def _first_existing(directory, basenames):
    """Return the first path in `directory` whose basename matches any entry
    of `basenames` case-insensitively. Returns the canonical (first) path
    even if the file does not exist, so callers can still print it as the
    expected location in warnings.
    """
    if not basenames:
        return ""
    try:
        present = os.listdir(directory)
    except OSError:
        present = []
    lower_map = {p.lower(): p for p in present}
    for want in basenames:
        hit = lower_map.get(want.lower())
        if hit:
            return os.path.join(directory, hit)
    return os.path.join(directory, basenames[0])


SRC_BG_TILE   = _first_existing(ASSETS_DIR, ["BgTile.png", "bgtile.png"])
SRC_HAPPY_MAC = _first_existing(ASSETS_DIR, ["happymacIconNew.png", "happymacicon.png",
                                              "happymacIcon.png",
                                              "HappyMacIcon.png", "happyMacIcon.png"])
SRC_FONT_TTF  = _first_existing(FONTS_DIR,  ["pixChicago.ttf", "pixchicago.ttf",
                                              "PixChicago.ttf", "chicago.ttf",
                                              "Chicago.ttf"])
SRC_FONT_BDF  = _first_existing(FONTS_DIR,  ["chicago.bdf", "Chicago.bdf"])

# Pixel size used when rasterizing the TTF. pixChicago is a pixel font where
# size=12 produces the "canonical" Chicago 12 glyphs at ~3x native pixel
# scale. Callers pass scale=1 to Chicago_DrawString for body UI text, scale=2
# for titles.
CHICAGO_TTF_RENDER_SIZE = 12

OUT_BG_TILE   = os.path.join(GEN_DIR, "asset_bg_tile.h")
OUT_HAPPY_MAC = os.path.join(GEN_DIR, "asset_happy_mac.h")
OUT_FONT      = os.path.join(GEN_DIR, "chicago_font_data.h")


# ---------------------------------------------------------------------------
# Lazy Pillow import (only needed when real PNG sources are present)
# ---------------------------------------------------------------------------

def _try_import_pillow():
    try:
        from PIL import Image  # noqa: F401
        return True
    except Exception:
        pass
    try:
        import subprocess
        subprocess.check_call(
            [sys.executable, "-m", "pip", "install", "--quiet", "Pillow"]
        )
        from PIL import Image  # noqa: F401
        return True
    except Exception as exc:
        print("[build_assets] Pillow unavailable: %s" % exc)
        return False


# ---------------------------------------------------------------------------
# Colour helpers
# ---------------------------------------------------------------------------

def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)


# ---------------------------------------------------------------------------
# Emit helpers
# ---------------------------------------------------------------------------

def _mtime(path):
    try:
        return os.path.getmtime(path)
    except OSError:
        return 0.0


def _needs_rebuild(out_path, src_paths):
    out_m = _mtime(out_path)
    if out_m == 0.0:
        return True
    src_newest = max([_mtime(p) for p in src_paths] + [_mtime(_THIS)])
    return src_newest > out_m


def _ensure_gen_dir():
    os.makedirs(GEN_DIR, exist_ok=True)


def _write_if_changed(path, contents):
    old = None
    try:
        with open(path, "r", encoding="utf-8") as f:
            old = f.read()
    except OSError:
        pass
    if old == contents:
        return False
    with open(path, "w", encoding="utf-8") as f:
        f.write(contents)
    return True


def _format_u16_array(values, per_line=16):
    out_lines = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        out_lines.append("    " + ", ".join("0x%04X" % v for v in chunk) + ",")
    return "\n".join(out_lines)


def _format_u8_array(values, per_line=16):
    out_lines = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        out_lines.append("    " + ", ".join("0x%02X" % v for v in chunk) + ",")
    return "\n".join(out_lines)


def _preamble(source_label):
    return (
        "// -----------------------------------------------------------------\n"
        "// AUTO-GENERATED by scripts/build_assets.py - DO NOT EDIT BY HAND.\n"
        "// Source: %s\n"
        "// Regenerated: %s\n"
        "// -----------------------------------------------------------------\n"
        "#pragma once\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
    ) % (source_label, time.strftime("%Y-%m-%d %H:%M:%S"))


# ---------------------------------------------------------------------------
# Default (fallback) artwork
# ---------------------------------------------------------------------------

# Classic Mac "50% gray" desktop pattern: alternating pixels. Looks gray at
# native 1x and gives the canonical stippled texture when tiled.
def _default_bg_tile():
    w = 16
    h = 16
    pixels = []
    for y in range(h):
        for x in range(w):
            on = (x + y) & 1
            if on:
                pixels.append(rgb888_to_rgb565(0x88, 0x88, 0x88))
            else:
                pixels.append(rgb888_to_rgb565(0xC0, 0xC0, 0xC0))
    return w, h, pixels


# Classic 32x32 Happy Mac (1-bit). Mirrors the bitmap previously embedded in
# boot_gui.cpp so the out-of-the-box boot still shows a recognizable icon.
_DEFAULT_HAPPY_MAC_1BIT = [
    0x00, 0x0F, 0xF0, 0x00,
    0x00, 0x3F, 0xFC, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0xFF, 0xFF, 0x00,
    0x01, 0xFF, 0xFF, 0x80,
    0x03, 0xFF, 0xFF, 0xC0,
    0x07, 0xE0, 0x07, 0xE0,
    0x07, 0xC0, 0x03, 0xE0,
    0x0F, 0x9E, 0x79, 0xF0,
    0x0F, 0x9E, 0x79, 0xF0,
    0x0F, 0x80, 0x01, 0xF0,
    0x0F, 0x80, 0x01, 0xF0,
    0x0F, 0x8C, 0x31, 0xF0,
    0x0F, 0x87, 0xE1, 0xF0,
    0x07, 0xC0, 0x03, 0xE0,
    0x07, 0xE0, 0x07, 0xE0,
    0x03, 0xFF, 0xFF, 0xC0,
    0x01, 0xFF, 0xFF, 0x80,
    0x00, 0xFF, 0xFF, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0x3F, 0xFC, 0x00,
    0x00, 0x0F, 0xF0, 0x00,
    0x00, 0x07, 0xE0, 0x00,
    0x00, 0x1F, 0xF8, 0x00,
    0x00, 0x3F, 0xFC, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0x3F, 0xFC, 0x00,
    0x00, 0x1F, 0xF8, 0x00,
    0x00, 0x07, 0xE0, 0x00,
]


def _default_happy_mac():
    w, h = 32, 32
    pixels = [0] * (w * h)
    mask_bits = [0] * (w * h)
    white = rgb888_to_rgb565(0xFF, 0xFF, 0xFF)
    for y in range(h):
        for bx in range(4):
            byte = _DEFAULT_HAPPY_MAC_1BIT[y * 4 + bx]
            for bit in range(8):
                x = bx * 8 + bit
                if byte & (1 << (7 - bit)):
                    pixels[y * w + x] = white
                    mask_bits[y * w + x] = 1
    return w, h, pixels, mask_bits


# 5x7 ASCII fallback font (same table as src/board/waveshare/mini_gfx_font.h,
# transposed into row-major bitmaps at pack time). Each entry = 5 column bytes.
_DEFAULT_FONT_5x7 = [
    (0x00, 0x00, 0x00, 0x00, 0x00),  # space
    (0x00, 0x00, 0x5F, 0x00, 0x00),  # !
    (0x00, 0x07, 0x00, 0x07, 0x00),  # "
    (0x14, 0x7F, 0x14, 0x7F, 0x14),  # #
    (0x24, 0x2A, 0x7F, 0x2A, 0x12),  # $
    (0x23, 0x13, 0x08, 0x64, 0x62),  # %
    (0x36, 0x49, 0x55, 0x22, 0x50),  # &
    (0x00, 0x05, 0x03, 0x00, 0x00),  # '
    (0x00, 0x1C, 0x22, 0x41, 0x00),  # (
    (0x00, 0x41, 0x22, 0x1C, 0x00),  # )
    (0x08, 0x2A, 0x1C, 0x2A, 0x08),  # *
    (0x08, 0x08, 0x3E, 0x08, 0x08),  # +
    (0x00, 0x50, 0x30, 0x00, 0x00),  # ,
    (0x08, 0x08, 0x08, 0x08, 0x08),  # -
    (0x00, 0x60, 0x60, 0x00, 0x00),  # .
    (0x20, 0x10, 0x08, 0x04, 0x02),  # /
    (0x3E, 0x51, 0x49, 0x45, 0x3E),  # 0
    (0x00, 0x42, 0x7F, 0x40, 0x00),  # 1
    (0x42, 0x61, 0x51, 0x49, 0x46),  # 2
    (0x21, 0x41, 0x45, 0x4B, 0x31),  # 3
    (0x18, 0x14, 0x12, 0x7F, 0x10),  # 4
    (0x27, 0x45, 0x45, 0x45, 0x39),  # 5
    (0x3C, 0x4A, 0x49, 0x49, 0x30),  # 6
    (0x01, 0x71, 0x09, 0x05, 0x03),  # 7
    (0x36, 0x49, 0x49, 0x49, 0x36),  # 8
    (0x06, 0x49, 0x49, 0x29, 0x1E),  # 9
    (0x00, 0x36, 0x36, 0x00, 0x00),  # :
    (0x00, 0x56, 0x36, 0x00, 0x00),  # ;
    (0x00, 0x08, 0x14, 0x22, 0x41),  # <
    (0x14, 0x14, 0x14, 0x14, 0x14),  # =
    (0x41, 0x22, 0x14, 0x08, 0x00),  # >
    (0x02, 0x01, 0x51, 0x09, 0x06),  # ?
    (0x32, 0x49, 0x79, 0x41, 0x3E),  # @
    (0x7E, 0x11, 0x11, 0x11, 0x7E),  # A
    (0x7F, 0x49, 0x49, 0x49, 0x36),  # B
    (0x3E, 0x41, 0x41, 0x41, 0x22),  # C
    (0x7F, 0x41, 0x41, 0x22, 0x1C),  # D
    (0x7F, 0x49, 0x49, 0x49, 0x41),  # E
    (0x7F, 0x09, 0x09, 0x01, 0x01),  # F
    (0x3E, 0x41, 0x41, 0x51, 0x32),  # G
    (0x7F, 0x08, 0x08, 0x08, 0x7F),  # H
    (0x00, 0x41, 0x7F, 0x41, 0x00),  # I
    (0x20, 0x40, 0x41, 0x3F, 0x01),  # J
    (0x7F, 0x08, 0x14, 0x22, 0x41),  # K
    (0x7F, 0x40, 0x40, 0x40, 0x40),  # L
    (0x7F, 0x02, 0x04, 0x02, 0x7F),  # M
    (0x7F, 0x04, 0x08, 0x10, 0x7F),  # N
    (0x3E, 0x41, 0x41, 0x41, 0x3E),  # O
    (0x7F, 0x09, 0x09, 0x09, 0x06),  # P
    (0x3E, 0x41, 0x51, 0x21, 0x5E),  # Q
    (0x7F, 0x09, 0x19, 0x29, 0x46),  # R
    (0x46, 0x49, 0x49, 0x49, 0x31),  # S
    (0x01, 0x01, 0x7F, 0x01, 0x01),  # T
    (0x3F, 0x40, 0x40, 0x40, 0x3F),  # U
    (0x1F, 0x20, 0x40, 0x20, 0x1F),  # V
    (0x7F, 0x20, 0x18, 0x20, 0x7F),  # W
    (0x63, 0x14, 0x08, 0x14, 0x63),  # X
    (0x03, 0x04, 0x78, 0x04, 0x03),  # Y
    (0x61, 0x51, 0x49, 0x45, 0x43),  # Z
    (0x00, 0x00, 0x7F, 0x41, 0x41),  # [
    (0x02, 0x04, 0x08, 0x10, 0x20),  # backslash
    (0x41, 0x41, 0x7F, 0x00, 0x00),  # ]
    (0x04, 0x02, 0x01, 0x02, 0x04),  # ^
    (0x40, 0x40, 0x40, 0x40, 0x40),  # _
    (0x00, 0x01, 0x02, 0x04, 0x00),  # `
    (0x20, 0x54, 0x54, 0x54, 0x78),  # a
    (0x7F, 0x48, 0x44, 0x44, 0x38),  # b
    (0x38, 0x44, 0x44, 0x44, 0x20),  # c
    (0x38, 0x44, 0x44, 0x48, 0x7F),  # d
    (0x38, 0x54, 0x54, 0x54, 0x18),  # e
    (0x08, 0x7E, 0x09, 0x01, 0x02),  # f
    (0x08, 0x14, 0x54, 0x54, 0x3C),  # g
    (0x7F, 0x08, 0x04, 0x04, 0x78),  # h
    (0x00, 0x44, 0x7D, 0x40, 0x00),  # i
    (0x20, 0x40, 0x44, 0x3D, 0x00),  # j
    (0x00, 0x7F, 0x10, 0x28, 0x44),  # k
    (0x00, 0x41, 0x7F, 0x40, 0x00),  # l
    (0x7C, 0x04, 0x18, 0x04, 0x78),  # m
    (0x7C, 0x08, 0x04, 0x04, 0x78),  # n
    (0x38, 0x44, 0x44, 0x44, 0x38),  # o
    (0x7C, 0x14, 0x14, 0x14, 0x08),  # p
    (0x08, 0x14, 0x14, 0x18, 0x7C),  # q
    (0x7C, 0x08, 0x04, 0x04, 0x08),  # r
    (0x48, 0x54, 0x54, 0x54, 0x20),  # s
    (0x04, 0x3F, 0x44, 0x40, 0x20),  # t
    (0x3C, 0x40, 0x40, 0x20, 0x7C),  # u
    (0x1C, 0x20, 0x40, 0x20, 0x1C),  # v
    (0x3C, 0x40, 0x30, 0x40, 0x3C),  # w
    (0x44, 0x28, 0x10, 0x28, 0x44),  # x
    (0x0C, 0x50, 0x50, 0x50, 0x3C),  # y
    (0x44, 0x64, 0x54, 0x4C, 0x44),  # z
    (0x00, 0x08, 0x36, 0x41, 0x00),  # {
    (0x00, 0x00, 0x7F, 0x00, 0x00),  # |
    (0x00, 0x41, 0x36, 0x08, 0x00),  # }
    (0x08, 0x04, 0x08, 0x10, 0x08),  # ~
]


# ---------------------------------------------------------------------------
# PNG decoding via Pillow (optional)
# ---------------------------------------------------------------------------

def _decode_png_rgba(path):
    from PIL import Image
    img = Image.open(path).convert("RGBA")
    return img.width, img.height, list(img.getdata())


def _png_to_rgb565(path):
    """Return (w, h, rgb565_pixels, mask_bits_or_None)."""
    w, h, rgba = _decode_png_rgba(path)
    pixels = [0] * (w * h)
    mask = [0] * (w * h)
    any_transparent = False
    for i, (r, g, b, a) in enumerate(rgba):
        pixels[i] = rgb888_to_rgb565(r, g, b)
        if a >= 128:
            mask[i] = 1
        else:
            any_transparent = True
    if any_transparent:
        return w, h, pixels, mask
    return w, h, pixels, None


# ---------------------------------------------------------------------------
# BDF parsing
# ---------------------------------------------------------------------------

def _parse_bdf(path):
    """Minimal BDF parser sufficient for Chicago-style proportional fonts.

    Returns a dict:
        {
          'ascent': int, 'descent': int, 'line_height': int,
          'glyphs': { codepoint: {
              'w': int, 'h': int, 'bbx_x': int, 'bbx_y': int,
              'advance': int, 'rows_rowmajor': [bytes, ...]
          }}
        }
    BITMAP rows are emitted as MSB-first row-major bytes (ceil(w/8) bytes/row).
    """
    ascent = 0
    descent = 0
    line_height = 0
    glyphs = {}

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    i = 0
    n = len(lines)
    while i < n:
        line = lines[i].strip()
        if line.startswith("FONT_ASCENT"):
            ascent = int(line.split()[1])
        elif line.startswith("FONT_DESCENT"):
            descent = int(line.split()[1])
        elif line.startswith("PIXEL_SIZE"):
            line_height = int(line.split()[1])
        elif line == "STARTCHAR" or line.startswith("STARTCHAR "):
            # Parse one glyph
            cp = None
            bbx_w = bbx_h = bbx_x = bbx_y = 0
            advance = 0
            bitmap_src = []
            i += 1
            while i < n:
                l2 = lines[i].strip()
                if l2.startswith("ENCODING"):
                    cp = int(l2.split()[1])
                elif l2.startswith("DWIDTH"):
                    advance = int(l2.split()[1])
                elif l2.startswith("BBX"):
                    parts = l2.split()
                    bbx_w = int(parts[1])
                    bbx_h = int(parts[2])
                    bbx_x = int(parts[3])
                    bbx_y = int(parts[4])
                elif l2 == "BITMAP":
                    i += 1
                    while i < n and lines[i].strip() != "ENDCHAR":
                        bitmap_src.append(lines[i].strip())
                        i += 1
                elif l2 == "ENDCHAR":
                    break
                i += 1
            if cp is not None and 0x20 <= cp <= 0x7E:
                bytes_per_row = (bbx_w + 7) // 8
                rows = []
                for r_hex in bitmap_src:
                    if not r_hex:
                        rows.append(b"\x00" * max(1, bytes_per_row))
                        continue
                    row_int = int(r_hex, 16)
                    # BDF pads rows to whole bytes already; extract as bytes
                    nbytes = (len(r_hex) + 1) // 2
                    row_bytes = row_int.to_bytes(nbytes, "big")
                    # Trim / pad to bytes_per_row
                    if len(row_bytes) > bytes_per_row:
                        row_bytes = row_bytes[:bytes_per_row]
                    elif len(row_bytes) < bytes_per_row:
                        row_bytes = row_bytes + b"\x00" * (bytes_per_row - len(row_bytes))
                    rows.append(row_bytes)
                glyphs[cp] = {
                    "w": bbx_w,
                    "h": bbx_h,
                    "bbx_x": bbx_x,
                    "bbx_y": bbx_y,
                    "advance": advance,
                    "rows": rows,
                }
        i += 1

    if line_height == 0:
        line_height = ascent + descent
    return {
        "ascent": ascent,
        "descent": descent,
        "line_height": line_height,
        "glyphs": glyphs,
    }


def _build_font_from_ttf(path, pixel_size):
    """Rasterize a TTF pixel-font into the same dict shape as _parse_bdf.

    Each glyph is drawn onto an L-mode canvas, thresholded to 1-bit, cropped
    to its actual ink bounds, then packed MSB-first row-major into bytes -
    matching the format the BDF path already emits. Works for any pixel
    font that renders cleanly with no antialiasing (pixChicago in particular).
    """
    from PIL import Image, ImageDraw, ImageFont

    font = ImageFont.truetype(path, pixel_size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    # Render canvas big enough for the widest glyphs with margin. Chicago
    # glyphs at size=12 top out around 20px wide.
    canvas_w = max(pixel_size * 4, 32)
    canvas_h = (ascent + descent) * 2 + 8

    glyphs = {}
    first_cp = 0x20
    last_cp  = 0x7E

    for cp in range(first_cp, last_cp + 1):
        ch = chr(cp)

        # Advance width (pen advance after this glyph).
        try:
            advance = int(round(font.getlength(ch)))
        except AttributeError:
            # Pillow <8: fall back to getsize
            advance = font.getsize(ch)[0]

        # Space never has pixels - short-circuit.
        if cp == 0x20:
            glyphs[cp] = {
                "w": 0, "h": 0, "bbx_x": 0, "bbx_y": 0,
                "advance": max(1, advance),
                "rows": [],
            }
            continue

        img = Image.new("L", (canvas_w, canvas_h), 0)
        draw = ImageDraw.Draw(img)
        # Draw at (0, 0) - the baseline ends up at y=ascent.
        draw.text((0, 0), ch, font=font, fill=255)

        # Threshold to 1-bit.
        pixels = img.load()
        ink_pts = []
        for y in range(canvas_h):
            for x in range(canvas_w):
                if pixels[x, y] >= 128:
                    ink_pts.append((x, y))

        if not ink_pts:
            glyphs[cp] = {
                "w": 0, "h": 0, "bbx_x": 0, "bbx_y": 0,
                "advance": max(1, advance),
                "rows": [],
            }
            continue

        x_min = min(p[0] for p in ink_pts)
        x_max = max(p[0] for p in ink_pts)
        y_min = min(p[1] for p in ink_pts)
        y_max = max(p[1] for p in ink_pts)

        bbx_w = x_max - x_min + 1
        bbx_h = y_max - y_min + 1

        # bbx_x (left side bearing) and bbx_y (bitmap-bottom relative to
        # baseline, positive = up) follow the BDF convention that the
        # existing chicago_font.cpp renderer expects.
        bbx_x = x_min
        bbx_y = ascent - y_max - 1  # y_max is distance from canvas top to bitmap bottom

        # Pack rows MSB-first, padded to whole bytes per row.
        bytes_per_row = (bbx_w + 7) // 8
        ink_set = set(ink_pts)
        rows = []
        for ry in range(bbx_h):
            row_bytes = bytearray(bytes_per_row)
            for rx in range(bbx_w):
                if (x_min + rx, y_min + ry) in ink_set:
                    byte_idx = rx // 8
                    bit_idx  = 7 - (rx % 8)
                    row_bytes[byte_idx] |= (1 << bit_idx)
            rows.append(bytes(row_bytes))

        glyphs[cp] = {
            "w": bbx_w,
            "h": bbx_h,
            "bbx_x": bbx_x,
            "bbx_y": bbx_y,
            "advance": max(1, advance),
            "rows": rows,
        }

    # Guarantee a minimal space glyph even if the TTF didn't produce one.
    if 0x20 not in glyphs:
        glyphs[0x20] = {"w": 0, "h": 0, "bbx_x": 0, "bbx_y": 0,
                        "advance": max(3, pixel_size // 3), "rows": []}

    return {
        "ascent": ascent,
        "descent": descent,
        "line_height": line_height,
        "glyphs": glyphs,
    }


def _build_font_from_5x7():
    """Construct a font dict equivalent to _parse_bdf from the 5x7 fallback."""
    glyphs = {}
    for idx, cols in enumerate(_DEFAULT_FONT_5x7):
        cp = 0x20 + idx
        # Convert 5 column bytes (bit 0 = top) to 7 row-major bytes (MSB = left).
        rows = []
        for r in range(7):
            row_byte = 0
            for c in range(5):
                if cols[c] & (1 << r):
                    row_byte |= (1 << (7 - c))
            rows.append(bytes([row_byte]))
        glyphs[cp] = {
            "w": 5,
            "h": 7,
            "bbx_x": 0,
            "bbx_y": 0,   # descent from baseline
            "advance": 6,
            "rows": rows,
        }
    return {
        "ascent": 7,
        "descent": 0,
        "line_height": 9,
        "glyphs": glyphs,
    }


# ---------------------------------------------------------------------------
# Emit: bg tile
# ---------------------------------------------------------------------------

def emit_bg_tile(src_path, out_path):
    if os.path.exists(src_path) and _try_import_pillow():
        w, h, pixels, _mask = _png_to_rgb565(src_path)
        label = os.path.relpath(src_path, _PROJECT_ROOT)
    else:
        w, h, pixels = _default_bg_tile()
        label = "<built-in 16x16 classic 50%% gray tile>"

    contents = _preamble(label)
    contents += "constexpr int BG_TILE_W = %d;\n" % w
    contents += "constexpr int BG_TILE_H = %d;\n\n" % h
    contents += "constexpr uint16_t BG_TILE_PIXELS[%d * %d] = {\n" % (w, h)
    contents += _format_u16_array(pixels) + "\n"
    contents += "};\n"

    _write_if_changed(out_path, contents)
    print("[build_assets] wrote %s (%dx%d)" % (os.path.relpath(out_path, _PROJECT_ROOT), w, h))


# ---------------------------------------------------------------------------
# Emit: happy mac
# ---------------------------------------------------------------------------

def emit_happy_mac(src_path, out_path):
    mask = None
    if os.path.exists(src_path) and _try_import_pillow():
        w, h, pixels, mask = _png_to_rgb565(src_path)
        label = os.path.relpath(src_path, _PROJECT_ROOT)
    else:
        w, h, pixels, mask_bits = _default_happy_mac()
        mask = mask_bits
        label = "<built-in 32x32 classic Happy Mac>"

    contents = _preamble(label)
    contents += "constexpr int HAPPY_MAC_W = %d;\n" % w
    contents += "constexpr int HAPPY_MAC_H = %d;\n" % h
    if mask is not None:
        mask_define = "1"
    else:
        mask_define = "0"
    # Use a #define (not constexpr bool) so the preprocessor can conditionally
    # reference the mask array below.
    contents += "#define HAPPY_MAC_HAS_MASK %s\n\n" % mask_define
    contents += "constexpr uint16_t HAPPY_MAC_PIXELS[%d * %d] = {\n" % (w, h)
    contents += _format_u16_array(pixels) + "\n"
    contents += "};\n\n"

    if mask is not None:
        # Pack 1-bit mask MSB-first, row-major, pad rows to whole bytes.
        packed = []
        bits_per_row = w
        bytes_per_row = (bits_per_row + 7) // 8
        for y in range(h):
            for bx in range(bytes_per_row):
                byte = 0
                for b in range(8):
                    x = bx * 8 + b
                    if x < w:
                        idx = y * w + x
                        if mask[idx]:
                            byte |= 1 << (7 - b)
                packed.append(byte)
        contents += "// 1 bit per pixel, MSB-first, row-major, rows padded to whole bytes.\n"
        contents += "constexpr int HAPPY_MAC_MASK_ROW_BYTES = %d;\n" % bytes_per_row
        contents += "constexpr uint8_t HAPPY_MAC_MASK[%d] = {\n" % len(packed)
        contents += _format_u8_array(packed) + "\n"
        contents += "};\n"

    _write_if_changed(out_path, contents)
    mask_note = ""
    if mask is not None:
        mask_note = ", with mask"
    print("[build_assets] wrote %s (%dx%d%s)" % (
        os.path.relpath(out_path, _PROJECT_ROOT), w, h, mask_note))


# ---------------------------------------------------------------------------
# Emit: chicago font
# ---------------------------------------------------------------------------

def emit_chicago_font(src_path, out_path):
    # Preference order: TTF (pixChicago) -> BDF -> 5x7 stub. src_path points at
    # the BDF slot for the _needs_rebuild() timestamp check; the TTF path is
    # consulted independently below.
    if os.path.exists(SRC_FONT_TTF) and _try_import_pillow():
        font = _build_font_from_ttf(SRC_FONT_TTF, CHICAGO_TTF_RENDER_SIZE)
        label = "%s @ size=%d" % (
            os.path.relpath(SRC_FONT_TTF, _PROJECT_ROOT),
            CHICAGO_TTF_RENDER_SIZE,
        )
    elif os.path.exists(src_path):
        font = _parse_bdf(src_path)
        label = os.path.relpath(src_path, _PROJECT_ROOT)
    else:
        font = _build_font_from_5x7()
        label = "<built-in 5x7 ASCII fallback - drop assets/fonts/pixChicago.ttf to override>"

    first_cp = 0x20
    last_cp = 0x7E

    # Pack all glyph bitmaps into a contiguous blob; build table pointing into it.
    blob = bytearray()
    table = []
    missing = font["glyphs"].get(first_cp, None)
    if missing is None:
        # Guarantee at least a space glyph
        font["glyphs"][first_cp] = {
            "w": 0, "h": 0, "bbx_x": 0, "bbx_y": 0,
            "advance": 4, "rows": [],
        }

    for cp in range(first_cp, last_cp + 1):
        g = font["glyphs"].get(cp)
        if g is None:
            g = {"w": 0, "h": 0, "bbx_x": 0, "bbx_y": 0, "advance": 0, "rows": []}
        offset = len(blob)
        for row in g["rows"]:
            blob.extend(row)
        table.append((offset, g["w"], g["h"], g["bbx_x"], g["bbx_y"], g["advance"]))

    contents = _preamble(label)
    contents += "struct ChicagoGlyph {\n"
    contents += "    uint16_t offset;     // byte offset into CHICAGO_GLYPH_BLOB\n"
    contents += "    uint8_t  w;          // glyph bitmap width in pixels\n"
    contents += "    uint8_t  h;          // glyph bitmap height in pixels\n"
    contents += "    int8_t   bbx_x;      // offset from pen position (positive = right)\n"
    contents += "    int8_t   bbx_y;      // offset of bitmap bottom from baseline (positive = up)\n"
    contents += "    uint8_t  advance;    // pen advance after drawing, in pixels\n"
    contents += "};\n\n"

    contents += "constexpr int CHICAGO_FIRST_CP    = 0x%02X;\n" % first_cp
    contents += "constexpr int CHICAGO_LAST_CP     = 0x%02X;\n" % last_cp
    contents += "constexpr int CHICAGO_ASCENT      = %d;\n" % font["ascent"]
    contents += "constexpr int CHICAGO_DESCENT     = %d;\n" % font["descent"]
    contents += "constexpr int CHICAGO_LINE_HEIGHT = %d;\n\n" % font["line_height"]

    contents += "// Packed glyph bitmaps: each glyph stores h consecutive rows of\n"
    contents += "// ceil(w/8) bytes, MSB-first. Look up CHICAGO_GLYPH_TABLE[cp -\n"
    contents += "// CHICAGO_FIRST_CP] to find the byte offset, width, and height.\n"
    contents += "constexpr uint8_t CHICAGO_GLYPH_BLOB[%d] = {\n" % max(1, len(blob))
    if len(blob) == 0:
        contents += "    0x00\n"
    else:
        contents += _format_u8_array(list(blob)) + "\n"
    contents += "};\n\n"

    contents += "constexpr ChicagoGlyph CHICAGO_GLYPH_TABLE[%d] = {\n" % len(table)
    for (off, w, h, bbx_x, bbx_y, adv) in table:
        contents += "    { %d, %d, %d, %d, %d, %d },\n" % (off, w, h, bbx_x, bbx_y, adv)
    contents += "};\n"

    _write_if_changed(out_path, contents)
    print("[build_assets] wrote %s (%d glyphs, ascent=%d descent=%d)" % (
        os.path.relpath(out_path, _PROJECT_ROOT),
        len(table), font["ascent"], font["descent"]))


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def build_all(force=False):
    _ensure_gen_dir()
    tasks = [
        (SRC_BG_TILE,   OUT_BG_TILE,   emit_bg_tile,     [SRC_BG_TILE]),
        (SRC_HAPPY_MAC, OUT_HAPPY_MAC, emit_happy_mac,   [SRC_HAPPY_MAC]),
        # Font depends on both the TTF and BDF slots; whichever is newer
        # triggers a regeneration.
        (SRC_FONT_BDF,  OUT_FONT,      emit_chicago_font, [SRC_FONT_TTF, SRC_FONT_BDF]),
    ]
    any_written = False
    for src, dst, fn, deps in tasks:
        if force or _needs_rebuild(dst, deps):
            fn(src, dst)
            any_written = True
    if not any_written:
        print("[build_assets] generated headers are up to date")


# Run now:
#   - If PIO loaded us via SConscript, _PIO_ENV will be set; regenerate
#     headers before compilation.
#   - If invoked standalone (python3 scripts/build_assets.py), also run.
if _PIO_ENV is not None:
    build_all(force=False)
elif __name__ == "__main__":
    force = "--force" in sys.argv
    build_all(force=force)
