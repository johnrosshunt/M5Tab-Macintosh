/*
 * board_touch_waveshare.cpp - Touch HAL for the Waveshare board.
 *
 * Uses the vendored BSP's bsp_touch_new() which under the hood instantiates
 * the GT911/GT9271 driver. The panel is physically mounted in portrait
 * (800 x 1280), so the raw touch coordinates are in portrait space. We
 * apply the same rotation we use for the framebuffer so boot_gui.cpp sees
 * landscape coordinates matching BOARD_DISPLAY_WIDTH x BOARD_DISPLAY_HEIGHT.
 */

#include "board_touch.h"
#include "board_config.h"

#include "esp_log.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_x.h"
#include "bsp/touch.h"
#include "esp_lcd_touch.h"

static const char *TAG = "board_touch";
static esp_lcd_touch_handle_t s_touch = nullptr;
static BoardTouchDetail       s_last{false, 0, 0};

/* Panel native dimensions (portrait). These must match the panel geometry
 * used by board_display_waveshare.cpp. */
static constexpr int PANEL_W = 800;
static constexpr int PANEL_H = 1280;

extern "C" bool BoardTouch_Init(void)
{
    if (s_touch) return true;

    bsp_display_cfg_t cfg = {};
    /* Ask the BSP for raw portrait coordinates (the touch controller's
     * native frame). We do the rotation to landscape ourselves in
     * BoardTouch_Update() so it stays in lock-step with the framebuffer
     * rotation in board_display_waveshare.cpp and MiniGfx. The BSP's
     * own mirror/swap flags use the unrotated panel max values and do
     * not compose correctly with a 90-degree rotation. */
    cfg.touch_flags.swap_xy  = 0;
    cfg.touch_flags.mirror_x = 0;
    cfg.touch_flags.mirror_y = 0;

    esp_err_t err = bsp_touch_new(&cfg, &s_touch);
    if (err != ESP_OK || !s_touch) {
        ESP_LOGE(TAG, "bsp_touch_new failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "GT911 touch initialized");
    return true;
}

extern "C" void BoardTouch_Update(void)
{
    if (!s_touch) { s_last.pressed = false; return; }

    esp_lcd_touch_read_data(s_touch);

    uint16_t tx = 0, ty = 0, strength = 0;
    uint8_t  cnt = 0;
    bool pressed = esp_lcd_touch_get_coordinates(s_touch, &tx, &ty, &strength, &cnt, 1);

    s_last.pressed = pressed && (cnt > 0);
    if (s_last.pressed) {
        /* Apply the same rotation used by the framebuffer: the panel is
         * natively portrait (PANEL_W wide, PANEL_H tall) but we present a
         * landscape frame via
         *   logical (lx, ly) -> physical (PANEL_W - 1 - ly, lx).
         * The inverse of that, used here to go raw-touch -> logical:
         *   logical_x = ty
         *   logical_y = PANEL_W - 1 - tx
         *
         * Assumes raw touch reports portrait-native coordinates, i.e.
         * tx in [0, PANEL_W) and ty in [0, PANEL_H).
         */
        int lx = (int)ty;
        int ly = (int)(PANEL_W - 1) - (int)tx;
        if (lx < 0) lx = 0;
        if (lx >= BOARD_DISPLAY_WIDTH)  lx = BOARD_DISPLAY_WIDTH  - 1;
        if (ly < 0) ly = 0;
        if (ly >= BOARD_DISPLAY_HEIGHT) ly = BOARD_DISPLAY_HEIGHT - 1;
        s_last.x = lx;
        s_last.y = ly;
    } else {
        s_last.x = 0;
        s_last.y = 0;
    }
}

extern "C" BoardTouchDetail BoardTouch_GetDetail(void)
{
    return s_last;
}
