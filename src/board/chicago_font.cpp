/*
 * chicago_font.cpp - see chicago_font.h.
 *
 * Rendering is done exclusively through MiniGfx::fillRect. For
 * transparent glyphs this is the cheapest path - one fillRect per "on"
 * pixel block - and keeps a single compiled source for both boards.
 */

#include "chicago_font.h"
#include "board_display.h"

/* Generated font data lives in src/generated/, which is on the include path.
 * The generated header is named chicago_font_data.h specifically to avoid
 * colliding with this module's own chicago_font.h above. */
#include "chicago_font_data.h"

/* ------------------------------------------------------------------------- */
/* Glyph helpers                                                              */
/* ------------------------------------------------------------------------- */

static inline const ChicagoGlyph *glyph_for(char c)
{
    unsigned char uc = (unsigned char)c;
    if (uc < CHICAGO_FIRST_CP || uc > CHICAGO_LAST_CP) {
        uc = '?';
        if (uc < CHICAGO_FIRST_CP || uc > CHICAGO_LAST_CP) {
            uc = CHICAGO_FIRST_CP;
        }
    }
    return &CHICAGO_GLYPH_TABLE[uc - CHICAGO_FIRST_CP];
}

static inline bool glyph_bit(const ChicagoGlyph *g, int gx, int gy)
{
    if (gx < 0 || gx >= g->w || gy < 0 || gy >= g->h) return false;
    int row_bytes = (g->w + 7) / 8;
    uint8_t b = CHICAGO_GLYPH_BLOB[g->offset + gy * row_bytes + (gx >> 3)];
    return (b & (0x80 >> (gx & 7))) != 0;
}

static int string_width_px(const char *s)
{
    int w = 0;
    for (const char *p = s; *p; ++p) {
        w += glyph_for(*p)->advance;
    }
    return w;
}

/* ------------------------------------------------------------------------- */
/* Public font API                                                            */
/* ------------------------------------------------------------------------- */

int Chicago_MeasureWidth(const char *s)
{
    return string_width_px(s);
}

int Chicago_LineHeight(void)
{
    return CHICAGO_LINE_HEIGHT;
}

void Chicago_DrawString(const char *s, int x, int y, uint16_t fg,
                        uint8_t datum)
{
    if (!s || !*s) return;

    int width_px  = string_width_px(s);
    int height_px = CHICAGO_ASCENT + CHICAGO_DESCENT;

    /* Convert (x, y) + datum into a top-left pen position. The pen itself
     * sits on the baseline, so we keep a separate baseline_y. */
    int top_left_x = x;
    int top_left_y = y;
    switch (datum) {
        case TC_DATUM: top_left_x = x - width_px / 2;              break;
        case TR_DATUM: top_left_x = x - width_px;                   break;
        case ML_DATUM: top_left_y = y - height_px / 2;              break;
        case MC_DATUM: top_left_x = x - width_px / 2;
                       top_left_y = y - height_px / 2;              break;
        case MR_DATUM: top_left_x = x - width_px;
                       top_left_y = y - height_px / 2;              break;
        case BL_DATUM: top_left_y = y - height_px;                  break;
        case BC_DATUM: top_left_x = x - width_px / 2;
                       top_left_y = y - height_px;                  break;
        case BR_DATUM: top_left_x = x - width_px;
                       top_left_y = y - height_px;                  break;
        case TL_DATUM:
        default:                                                    break;
    }

    int baseline_y = top_left_y + CHICAGO_ASCENT;
    int pen_x      = top_left_x;

    auto &gfx = BoardDisplay_Gfx();

    for (const char *p = s; *p; ++p) {
        const ChicagoGlyph *g = glyph_for(*p);
        /* Glyph bitmap sits at: left = pen_x + bbx_x,
         *                      bottom = baseline_y - bbx_y. */
        int glyph_left   = pen_x + g->bbx_x;
        int glyph_bottom = baseline_y - g->bbx_y;
        int glyph_top    = glyph_bottom - g->h;
        for (int gy = 0; gy < g->h; ++gy) {
            for (int gx = 0; gx < g->w; ++gx) {
                if (glyph_bit(g, gx, gy)) {
                    gfx.fillRect(glyph_left + gx,
                                 glyph_top  + gy,
                                 1, 1, fg);
                }
            }
        }
        pen_x += g->advance;
    }
}

/* ------------------------------------------------------------------------- */
/* Tile / icon helpers                                                        */
/* ------------------------------------------------------------------------- */

void Gfx_TileBackground(const uint16_t *tile, int tw, int th, int scale)
{
    if (!tile || tw <= 0 || th <= 0 || scale < 1) return;

    auto &gfx  = BoardDisplay_Gfx();
    int screen_w = BoardDisplay_Width();
    int screen_h = BoardDisplay_Height();

    const int block = scale;           /* size of one source-pixel block */
    for (int y = 0; y < screen_h; y += block) {
        int src_y = (y / block) % th;
        for (int x = 0; x < screen_w; x += block) {
            int src_x = (x / block) % tw;
            uint16_t c = tile[src_y * tw + src_x];
            int w = block;
            if (screen_w - x < block) {
                w = screen_w - x;
            }
            int h = block;
            if (screen_h - y < block) {
                h = screen_h - y;
            }
            gfx.fillRect(x, y, w, h, c);
        }
    }
}

void Gfx_DrawIconCentered(const uint16_t *pixels, int w, int h,
                          const uint8_t *mask_bits, int mask_row_bytes,
                          int scale)
{
    if (!pixels || w <= 0 || h <= 0 || scale < 1) return;

    auto &gfx  = BoardDisplay_Gfx();
    int screen_w = BoardDisplay_Width();
    int screen_h = BoardDisplay_Height();

    int drawn_w = w * scale;
    int drawn_h = h * scale;
    int origin_x = (screen_w - drawn_w) / 2;
    int origin_y = (screen_h - drawn_h) / 2;

    const bool have_mask = (mask_bits != nullptr && mask_row_bytes > 0);

    for (int sy = 0; sy < h; ++sy) {
        for (int sx = 0; sx < w; ++sx) {
            if (have_mask) {
                uint8_t b = mask_bits[sy * mask_row_bytes + (sx >> 3)];
                if ((b & (0x80 >> (sx & 7))) == 0) continue;  /* transparent */
            }
            gfx.fillRect(origin_x + sx * scale,
                         origin_y + sy * scale,
                         scale, scale,
                         pixels[sy * w + sx]);
        }
    }
}
