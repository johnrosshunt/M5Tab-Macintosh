/*
 * chicago_font.h - backend-neutral Chicago bitmap font + simple asset blitters
 *                  for the pre-boot Mac splash and settings overlays.
 *
 * The font data lives in `src/generated/chicago_font_data.h`, produced at
 * build time from `assets/fonts/chicago.bdf` (or a baked-in 5x7 ASCII
 * fallback, for first-time or Pillow-less builds).
 * Drawing is done through `BoardDisplay_Gfx()` so the same code compiles on
 * both the M5GFX (Tab5) and MiniGfx (Waveshare) backends - we only touch
 * methods that both surfaces expose (fillRect, pushImage).
 */
#pragma once

#include <stdint.h>
#include "board_display.h"   /* pulls in TL_DATUM / MC_DATUM for the active backend */

/**
 * @brief Draw a NUL-terminated string in Chicago at its native pixel-perfect
 *        size (no scaling). One source pixel maps to one framebuffer pixel.
 *
 * The string is laid out along the baseline, then (x, y) is adjusted by the
 * supplied text `datum` (matches LovyanGFX / MiniGfx semantics - TL_DATUM,
 * MC_DATUM, etc.).
 *
 * Only the "on" glyph pixels are drawn; background is transparent.
 */
void Chicago_DrawString(const char *s, int x, int y,
                        uint16_t fg,
                        uint8_t  datum = TL_DATUM);

/** @brief Pixel width a string will take when rendered at native size. */
int Chicago_MeasureWidth(const char *s);

/** @brief Line height (ascent+descent) at native size. */
int Chicago_LineHeight(void);

/* ------------------------------------------------------------------------- */
/* Simple blit helpers used by the Mac splash; live next to the font because */
/* they share the same "backend-neutral via BoardDisplay_Gfx()" pattern.     */
/* ------------------------------------------------------------------------- */

/**
 * @brief Tile a small RGB565 bitmap across the entire display at 2x (or N).
 *
 * `tile` is row-major RGB565, length tw*th. At scale > 1 each source pixel is
 * drawn as a scale*scale block (nearest-neighbor). Fills the full screen.
 */
void Gfx_TileBackground(const uint16_t *tile, int tw, int th, int scale = 2);

/**
 * @brief Draw an RGB565 icon centered on the display at `scale` nearest-
 *        neighbor zoom. If `mask_bits` is non-null, each pixel with a 0 bit
 *        in the mask is treated as transparent. Mask is 1 bit per pixel,
 *        MSB-first, row-major, rows padded to whole bytes (matches the
 *        format emitted by `scripts/build_assets.py`).
 */
void Gfx_DrawIconCentered(const uint16_t *pixels, int w, int h,
                          const uint8_t *mask_bits = nullptr,
                          int mask_row_bytes = 0,
                          int scale = 2);
