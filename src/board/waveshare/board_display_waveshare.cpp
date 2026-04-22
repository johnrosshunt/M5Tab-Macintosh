/*
 * board_display_waveshare.cpp - Display HAL for the Waveshare
 * ESP32-P4-WIFI6-Touch-LCD-10.1 board.
 *
 * Bring-up is delegated to the vendored Waveshare BSP
 * (lib/esp32_p4_wifi6_touch_lcd_x/) which brings up the MIPI-DSI bus, the
 * JD9365 panel, and the LEDC backlight PWM. We then do three extra things
 * on top of what the BSP provides:
 *
 *   1. Allocate a MiniGfx instance for boot_gui.cpp to draw into. It owns a
 *      portrait RGB565 framebuffer in PSRAM and handles landscape->portrait
 *      coordinate rotation transparently.
 *
 *   2. Route BoardDisplay_PushTile() (used by video_esp32.cpp) through
 *      esp_lcd_panel_draw_bitmap. A rotation buffer in internal SRAM
 *      converts the incoming landscape tile into portrait scan order so the
 *      physical panel displays it correctly.
 *
 *   3. Expose BoardDisplay_Gfx() as a reference to the MiniGfx instance so
 *      boot_gui.cpp's "#define gfx BoardDisplay_Gfx()" pattern works.
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

#include "bsp/esp32_p4_wifi6_touch_lcd_x.h"
#include "bsp/display.h"

static const char *TAG = "board_display";

/* Panel is 800x1280 portrait natively - from BSP display.h constants. */
static constexpr int PANEL_W = 800;
static constexpr int PANEL_H = 1280;

static esp_lcd_panel_handle_t    s_panel    = nullptr;
static esp_lcd_panel_io_handle_t s_panel_io = nullptr;
static MiniGfx                   s_gfx;
static bool                      s_inited   = false;
static SemaphoreHandle_t         s_dma_done = nullptr;  /* signals completion of the last draw_bitmap */

/* ISR-safe callback fired by the MIPI-DSI driver when the last framebuffer
 * refresh has finished. We use it to gate further draw_bitmap calls so they
 * do not trip the "previous draw operation is not finished" error. */
static IRAM_ATTR bool on_dsi_trans_done(esp_lcd_panel_handle_t /*panel*/,
                                        esp_lcd_dpi_panel_event_data_t * /*edata*/,
                                        void *user_ctx)
{
    SemaphoreHandle_t sem = static_cast<SemaphoreHandle_t>(user_ctx);
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(sem, &woke);
    return woke == pdTRUE;
}

/* Scratch rotation buffer for PushTile. Sized for the worst-case tile we
 * push (80x80 @ RGB565 = 12.8 KB) plus a bit of headroom. Placed in
 * internal SRAM (DMA-capable, lowest-latency path).                        */
static constexpr size_t ROT_BUF_PIXELS = 128 * 128;
DRAM_ATTR static uint16_t s_rot_buf[ROT_BUF_PIXELS];

extern "C" bool BoardDisplay_Init(void)
{
    if (s_inited) return true;

    ESP_LOGI(TAG, "Initializing Waveshare P4 10.1 display...");

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_i2c_init failed: %s", esp_err_to_name(err));
        return false;
    }

    bsp_display_config_t cfg = {};
    err = bsp_display_new(&cfg, &s_panel, &s_panel_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_display_new failed: %s", esp_err_to_name(err));
        return false;
    }

    /* Backlight on. The BSP already configured the LEDC PWM inside
     * bsp_display_new -> bsp_display_brightness_init. */
    bsp_display_brightness_set(100);

    /* Create a "frame done" semaphore and prime it. It starts as given so
     * the first draw_bitmap can proceed immediately; subsequent calls wait
     * for the DSI driver's trans_done callback. */
    s_dma_done = xSemaphoreCreateBinary();
    if (!s_dma_done) {
        ESP_LOGE(TAG, "failed to create DMA semaphore");
        return false;
    }
    xSemaphoreGive(s_dma_done);

    esp_lcd_dpi_panel_event_callbacks_t cbs = {};
    cbs.on_color_trans_done = on_dsi_trans_done;
    esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, s_dma_done);

    /* Allocate the software drawing surface. Logical landscape dims are
     * BOARD_DISPLAY_WIDTH/HEIGHT from board_config.h. */
    if (!s_gfx.begin(s_panel,
                     BOARD_DISPLAY_WIDTH, BOARD_DISPLAY_HEIGHT,
                     PANEL_W, PANEL_H)) {
        ESP_LOGE(TAG, "MiniGfx begin failed");
        return false;
    }

    /* Blank the panel on first show. */
    s_gfx.fillScreen(0x0000u);
    s_gfx.flushAllForce();

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
    /* Wait for the previous transfer to signal completion, then re-take so
     * subsequent waits block correctly. 100 ms is a generous timeout; one
     * frame at 60 Hz is ~16 ms. */
    if (xSemaphoreTake(s_dma_done, pdMS_TO_TICKS(100)) == pdTRUE) {
        xSemaphoreGive(s_dma_done);
    }
}

/* Internal helper: block until the previous draw_bitmap completes, then
 * claim the ready-slot for the next one. Paired with release by the
 * on_dsi_trans_done callback giving the semaphore back. */
static inline void claim_dma_slot(void)
{
    if (!s_dma_done) return;
    xSemaphoreTake(s_dma_done, pdMS_TO_TICKS(100));
}

extern "C" void BoardDisplay_PushTile(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (!s_panel || !pixels) return;
    if (w <= 0 || h <= 0) return;

    /* Rotate the tile into portrait scan order. Landscape input is w x h
     * pixels starting at logical (x, y). Physical rectangle is h x w
     * pixels starting at (panel_w - y - h, x). Each logical row j becomes
     * a reversed physical column at px = panel_w - 1 - y - j.            */
    const size_t pixels_needed = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (pixels_needed > ROT_BUF_PIXELS) {
        ESP_LOGW(TAG, "PushTile tile %dx%d > rotation buffer", w, h);
        return;
    }

    uint16_t *dst = s_rot_buf;
    /* Portrait rect width = h, height = w. We write row by row in portrait
     * order. Physical row py corresponds to logical column (py - x) after
     * shifting by the rectangle origin. Inside each portrait row, physical
     * column index relative to the rect start maps to (h - 1 - (j)) where
     * j indexes into the logical source row.                              */
    for (int py_rel = 0; py_rel < w; ++py_rel) {
        const int lx_rel = py_rel;  /* logical column index within the rect */
        for (int px_rel = 0; px_rel < h; ++px_rel) {
            const int ly_rel = h - 1 - px_rel;
            dst[py_rel * h + px_rel] = pixels[ly_rel * w + lx_rel];
        }
    }

    const int px_start = PANEL_W - y - h;
    const int py_start = x;
    claim_dma_slot();
    esp_lcd_panel_draw_bitmap(s_panel,
                              px_start, py_start,
                              px_start + h, py_start + w,
                              s_rot_buf);
}

extern "C" void BoardDisplay_PushFullFrame(const uint16_t *pixels)
{
    if (!s_panel || !pixels) return;
    /* Full-frame path builds in MiniGfx's native portrait FB. Callers that
     * already have a landscape buffer should use the rotating PushTile
     * path one tile at a time. */
    claim_dma_slot();
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, PANEL_W, PANEL_H, pixels);
}

extern "C" void BoardDisplay_ClearScreen(uint16_t color)
{
    if (!s_panel) return;

    /* Fill the portrait panel one horizontal strip at a time out of
     * s_rot_buf (32 KB, ROT_BUF_PIXELS = 128*128). That's enough for
     * an 800-wide * 20-row strip on the Waveshare panel without
     * another internal SRAM allocation.                                */
    const int strip_rows = ROT_BUF_PIXELS / PANEL_W;   /* 16384 / 800 = 20 */
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
    /* Ensure the last transfer completes before we hand control back.  */
    if (s_dma_done) {
        xSemaphoreTake(s_dma_done, pdMS_TO_TICKS(100));
        xSemaphoreGive(s_dma_done);
    }
}

/* Internal helper exposed so MiniGfx::flushAll can also gate on the
 * trans_done semaphore without needing its own knowledge of the panel. */
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
    bsp_display_brightness_set(percent);
}

MiniGfx &BoardDisplay_Gfx_Board(void)
{
    return s_gfx;
}
