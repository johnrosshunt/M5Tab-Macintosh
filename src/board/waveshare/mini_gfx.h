/*
 * mini_gfx.h - tiny RGB565 text/rect/primitive renderer for the boot GUI
 *              on boards that lack M5GFX/LovyanGFX.
 *
 * The boot GUI draws with a small API surface (fillRect, drawRect,
 * drawFastHLine/VLine, drawLine, drawCircle, drawPixel, drawString, ...).
 * MiniGfx provides exactly those methods on top of a landscape RGB565
 * PSRAM framebuffer, plus coordinate rotation so we can drive an 800x1280
 * portrait MIPI-DSI panel as if it were 1280x800 landscape.
 *
 * Draws update the backing framebuffer immediately. Call flushAll() or
 * flushRect() to copy the framebuffer to the panel (via the panel handle
 * registered with setPanel()).
 *
 * Method names and signatures mirror M5GFX / LovyanGFX for the subset that
 * boot_gui.cpp uses, so the same boot_gui code compiles against either
 * backend.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/* Text datum constants matching LovyanGFX / M5GFX exactly. */
#ifndef MC_DATUM
#define TL_DATUM 0   /* Top-Left     */
#define TC_DATUM 1   /* Top-Center   */
#define TR_DATUM 2   /* Top-Right    */
#define ML_DATUM 3   /* Middle-Left  */
#define MC_DATUM 4   /* Middle-Center*/
#define MR_DATUM 5   /* Middle-Right */
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8
#endif

class MiniGfx {
public:
    MiniGfx();

    /* Allocate framebuffer and attach to an esp_lcd_panel. `logical_w` and
     * `logical_h` are landscape dimensions. `panel_w`/`panel_h` are the
     * native portrait panel dimensions. */
    bool begin(void *panel_handle,
               int  logical_w, int  logical_h,
               int  panel_w,   int  panel_h);
    bool beginSansPanel(int logical_w, int logical_h,
                        int panel_w,   int panel_h);

    /* LovyanGFX-compatible drawing API (subset used by boot_gui.cpp). */
    int  width(void)  const { return _lw; }
    int  height(void) const { return _lh; }

    void fillScreen(uint32_t color);
    void fillRect(int x, int y, int w, int h, uint32_t color);
    void drawRect(int x, int y, int w, int h, uint32_t color);
    void drawFastHLine(int x, int y, int w, uint32_t color);
    void drawFastVLine(int x, int y, int h, uint32_t color);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t color);
    void drawPixel(int x, int y, uint32_t color);
    void drawCircle(int cx, int cy, int r, uint32_t color);
    void fillCircle(int cx, int cy, int r, uint32_t color);
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);

    void setTextColor(uint32_t color)               { _text_color = color; _text_bg_transparent = true; }
    void setTextColor(uint32_t color, uint32_t bg)  { _text_color = color; _text_bg = bg; _text_bg_transparent = false; }
    void setTextSize(uint8_t size)                  { _text_size = size < 1 ? 1 : size; }
    void setTextDatum(uint8_t datum)                { _text_datum = datum; }

    void drawString(const char *str, int x, int y);

    /* Direct push of a rotated logical-rectangular RGB565 region into the
     * framebuffer. No rotation is applied - pixels are laid out landscape. */
    void pushImage(int x, int y, int w, int h, const uint16_t *pixels);

    /* Panel push */
    void flushAll(void);
    void flushRect(int x, int y, int w, int h);

    /* Raw framebuffer (portrait orientation, size panel_w * panel_h). */
    uint16_t *portraitFb(void) { return _fb; }
    int       panelW(void)     { return _pw; }
    int       panelH(void)     { return _ph; }

private:
    void *   _panel        = nullptr;  /* esp_lcd_panel_handle_t - opaque to avoid header dep */
    uint16_t *_fb          = nullptr;  /* RGB565, portrait, _pw x _ph */
    int      _lw           = 0;        /* logical landscape width  */
    int      _lh           = 0;        /* logical landscape height */
    int      _pw           = 0;        /* panel portrait width     */
    int      _ph           = 0;        /* panel portrait height    */

    uint32_t _text_color   = 0xFFFFu;  /* white  */
    uint32_t _text_bg      = 0x0000u;  /* black  */
    bool     _text_bg_transparent = true;
    uint8_t  _text_size    = 1;
    uint8_t  _text_datum   = TL_DATUM;

    /* Map (lx, ly) logical -> (px, py) portrait and write one pixel. */
    inline void writeLogicalPixel(int lx, int ly, uint16_t color);
    /* Fill a logical rectangle (landscape coords) with a solid color. */
    void fillLogicalRect(int lx, int ly, int lw, int lh, uint16_t color);
    /* Text helpers */
    int  glyphWidthPx(uint8_t size) const  { return 6 * size; }
    int  glyphHeightPx(uint8_t size) const { return 8 * size; }
    int  measureStringPx(const char *str) const;
    void drawGlyph(int x, int y, char c, uint32_t fg, uint32_t bg, bool transparent, uint8_t size);
};
