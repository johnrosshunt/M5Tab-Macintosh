/*
 * board_display_tab5.cpp - Display HAL for M5Stack Tab5.
 *
 * The M5GFX `writePixelsDMA` path on ESP32-P4 MIPI-DSI is a CPU memcpy
 * into the panel's line buffer with no DMA completion sync. At the
 * video task's ~20 FPS tile cadence this produces visible tearing
 * around the mouse cursor and on window drags. This HAL mirrors
 * board_display_waveshare.cpp instead: draw into a MiniGfx PSRAM
 * framebuffer and flush to the panel via `esp_lcd_panel_draw_bitmap`,
 * gated on the MIPI-DSI `on_color_trans_done` callback.
 *
 * M5Unified (M5.begin() in Board_Init) still owns touch, audio, and
 * backlight. We grab the `esp_lcd_panel_handle_t` it created for the
 * ILI9881C MIPI-DSI panel via a friend-access subclass of Panel_DSI
 * and drive it ourselves for all pixel writes.
 */

#include "board_display.h"
#include "board_config.h"

#include "mini_gfx/mini_gfx.h"

#include <string.h>
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <M5Unified.h>
#include "lgfx/v1/platforms/esp32p4/Panel_DSI.hpp"

static const char *TAG = "board_display";

/* Tab5 ILI9881C is natively 720x1280 portrait (see M5GFX.cpp where it
 * sets cfg.panel_width=720 / cfg.panel_height=1280 on autodetect). */
static constexpr int PANEL_W = 720;
static constexpr int PANEL_H = 1280;

/* Friend-access subclass: Panel_DSI has an `_disp_panel_handle` member
 * that is protected. A subclass added purely for access exposes it
 * without forking M5GFX. We reinterpret-cast the concrete Panel_ILI9881C
 * pointer into this accessor; Panel_ILI9881C adds no data members of
 * its own so the memory layout is identical. */
struct Tab5DsiAccess : public lgfx::Panel_DSI {
    esp_lcd_panel_handle_t handle(void) { return _disp_panel_handle; }
};

static esp_lcd_panel_handle_t        s_panel    = nullptr;
static MiniGfx                       s_gfx;
static bool                          s_inited   = false;
static SemaphoreHandle_t             s_dma_done = nullptr;

/* ISR-safe callback fired by the MIPI-DSI driver when the last
 * framebuffer copy has finished. Gates sequential draw_bitmap calls
 * so they don't trip "previous draw operation is not finished". */
static IRAM_ATTR bool on_dsi_trans_done(esp_lcd_panel_handle_t /*panel*/,
                                        esp_lcd_dpi_panel_event_data_t * /*edata*/,
                                        void *user_ctx)
{
    SemaphoreHandle_t sem = static_cast<SemaphoreHandle_t>(user_ctx);
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(sem, &woke);
    return woke == pdTRUE;
}

/* Rotation scratch buffer sized for the 80x80 RGB565 tile the video
 * task pushes (12.8 KB), with headroom. Lives in internal SRAM for the
 * lowest-latency path. */
static constexpr size_t ROT_BUF_PIXELS = 128 * 128;
DRAM_ATTR static uint16_t s_rot_buf[ROT_BUF_PIXELS];

extern "C" bool BoardDisplay_Init(void)
{
    if (s_inited) return true;

    ESP_LOGI(TAG, "Initializing Tab5 display (direct DSI pipeline)...");

    /* M5.begin() in Board_Init already brought up the ILI9881C panel.
     * Reach into M5.Display to grab the esp_lcd_panel_handle_t it
     * created so we can drive the DSI bus directly. */
    auto *panel_dev = M5.Display.getPanel();
    if (!panel_dev) {
        ESP_LOGE(TAG, "M5.Display has no panel device");
        return false;
    }
    auto *dsi = reinterpret_cast<Tab5DsiAccess *>(
        static_cast<lgfx::Panel_DSI *>(panel_dev));
    s_panel = dsi->handle();
    if (!s_panel) {
        ESP_LOGE(TAG, "Tab5 Panel_DSI has no esp_lcd_panel_handle");
        return false;
    }

    s_dma_done = xSemaphoreCreateBinary();
    if (!s_dma_done) {
        ESP_LOGE(TAG, "failed to create DMA semaphore");
        return false;
    }
    xSemaphoreGive(s_dma_done);

    esp_lcd_dpi_panel_event_callbacks_t cbs = {};
    cbs.on_color_trans_done = on_dsi_trans_done;
    esp_err_t err = esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, s_dma_done);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register_event_callbacks failed: %s", esp_err_to_name(err));
        return false;
    }

    /* Grab the DPI panel's own internal framebuffer and use it as
     * MiniGfx's drawing surface. This makes esp_lcd_panel_draw_bitmap
     * take the "source lives in a panel FB" fast path (cache writeback
     * only), avoiding the PSRAM->PSRAM DMA2D copy that otherwise causes
     * MIPI-DSI bridge underruns and full-frame black flicker. */
    void *dpi_fb = nullptr;
    err = esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &dpi_fb);
    if (err != ESP_OK || !dpi_fb) {
        ESP_LOGE(TAG, "get_frame_buffer failed: %s", esp_err_to_name(err));
        return false;
    }

    if (!s_gfx.beginExternalFb(s_panel, dpi_fb,
                               BOARD_DISPLAY_WIDTH, BOARD_DISPLAY_HEIGHT,
                               PANEL_W, PANEL_H)) {
        ESP_LOGE(TAG, "MiniGfx beginExternalFb failed");
        return false;
    }

    /* Tab5 ships with the panel ribbon at the top of the landscape view
     * when using the default 90-CW mapping. Flip the landscape output
     * 180 degrees so the Mac desktop appears right-side up when the
     * device is held with the USB-C port on the left.                   */
    s_gfx.setFlip180(true);

    /* Do NOT explicitly fillScreen + flushAllForce here. beginExternalFb
     * already zeroed the DPI framebuffer, and the DPI is continuously
     * scanning that same FB, so an explicit black flush would be a
     * second visible refresh before the first real content (splash)
     * lands. The caller (mac_splash::paint_splash) will overwrite every
     * pixel and call BoardDisplay_Present, which does the cache sync.   */

    s_inited = true;
    ESP_LOGI(TAG, "Display up: logical %dx%d, panel %dx%d",
             BOARD_DISPLAY_WIDTH, BOARD_DISPLAY_HEIGHT, PANEL_W, PANEL_H);
    return true;
}

extern "C" int BoardDisplay_Width(void)  { return BOARD_DISPLAY_WIDTH; }
extern "C" int BoardDisplay_Height(void) { return BOARD_DISPLAY_HEIGHT; }

extern "C" void BoardDisplay_BeginTiles(void) { /* no-op on MIPI-DSI */ }
extern "C" void BoardDisplay_EndTiles(void)   { /* no-op on MIPI-DSI */ }

extern "C" void BoardDisplay_WaitPush(void)
{
    if (!s_dma_done) return;
    if (xSemaphoreTake(s_dma_done, pdMS_TO_TICKS(100)) == pdTRUE) {
        xSemaphoreGive(s_dma_done);
    }
}

static inline void claim_dma_slot(void)
{
    if (!s_dma_done) return;
    xSemaphoreTake(s_dma_done, pdMS_TO_TICKS(100));
}

extern "C" void BoardDisplay_PushTile(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (!s_panel || !pixels) return;
    if (w <= 0 || h <= 0) return;

    /* Rotate the tile into portrait scan order for the 180-flipped
     * landscape mapping used by MiniGfx: logical (lx, ly) -> portrait
     * (ly, PANEL_H - 1 - lx). Input is a w x h landscape tile at
     * logical (x, y). Output is an h x w portrait rectangle starting at
     * physical (y, PANEL_H - x - w).                                    */
    const size_t pixels_needed = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (pixels_needed > ROT_BUF_PIXELS) {
        ESP_LOGW(TAG, "PushTile tile %dx%d > rotation buffer", w, h);
        return;
    }

    uint16_t *dst = s_rot_buf;
    for (int py_rel = 0; py_rel < w; ++py_rel) {
        const int lx_rel = w - 1 - py_rel;
        for (int px_rel = 0; px_rel < h; ++px_rel) {
            const int ly_rel = px_rel;
            dst[py_rel * h + px_rel] = pixels[ly_rel * w + lx_rel];
        }
    }

    const int px_start = y;
    const int py_start = PANEL_H - x - w;
    claim_dma_slot();
    esp_lcd_panel_draw_bitmap(s_panel,
                              px_start, py_start,
                              px_start + h, py_start + w,
                              s_rot_buf);
}

extern "C" void BoardDisplay_PushFullFrame(const uint16_t *pixels)
{
    if (!s_panel || !pixels) return;
    /* Full-frame path assumes pixels are already in native portrait
     * scan order. Callers that have a landscape buffer should push it
     * a tile at a time through PushTile. */
    claim_dma_slot();
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, PANEL_W, PANEL_H, pixels);
}

extern "C" void BoardDisplay_ClearScreen(uint16_t color)
{
    if (!s_panel) return;

    /* Fill the portrait panel one horizontal strip at a time out of
     * s_rot_buf (32 KB). strip_rows = 128*128/720 = 22 rows. */
    const int strip_rows = ROT_BUF_PIXELS / PANEL_W;
    if (strip_rows <= 0) return;

    const size_t strip_pixels = (size_t)PANEL_W * (size_t)strip_rows;
    for (size_t i = 0; i < strip_pixels; ++i) {
        s_rot_buf[i] = color;
    }

    for (int y = 0; y < PANEL_H; y += strip_rows) {
        int rows = strip_rows;
        if (y + rows > PANEL_H) rows = PANEL_H - y;
        claim_dma_slot();
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, PANEL_W, y + rows, s_rot_buf);
    }
    if (s_dma_done) {
        xSemaphoreTake(s_dma_done, pdMS_TO_TICKS(100));
        xSemaphoreGive(s_dma_done);
    }
}

/* Exposed so MiniGfx::flushAll can gate on the trans_done semaphore
 * without needing its own knowledge of the panel.                       */
extern "C" void BoardDisplay_ClaimDmaSlot(void)
{
    claim_dma_slot();
}

extern "C" void BoardDisplay_FillRect(int x, int y, int w, int h, uint16_t color)
{
    s_gfx.fillRect(x, y, w, h, color);
    s_gfx.flushRect(x, y, w, h);
}

extern "C" void BoardDisplay_Present(void)
{
    s_gfx.flushAll();
}

extern "C" void BoardDisplay_SetBacklight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    /* M5Unified already configured the Tab5 backlight LEDC (GPIO22 PWM
     * on a channel allocated by M5GFX). Reuse its setter. Maps 0-100
     * percent to the 0-255 brightness range LovyanGFX expects. */
    M5.Display.setBrightness((percent * 255) / 100);
}

MiniGfx &BoardDisplay_Gfx_Board(void)
{
    return s_gfx;
}
