/*
 * board_touch_waveshare.cpp - Touch HAL for the Waveshare board.
 *
 * Uses the vendored BSP's bsp_touch_new() which under the hood instantiates
 * the GT911/GT9271 driver. The panel is physically mounted in portrait
 * (800 x 1280), so the raw touch coordinates are in portrait space. We
 * apply the same rotation we use for the framebuffer so boot_gui.cpp sees
 * landscape coordinates matching BOARD_DISPLAY_WIDTH x BOARD_DISPLAY_HEIGHT.
 *
 * Multi-touch: GT911 reports up to 5 simultaneous points; we cache the
 * full set in BoardTouch_Update() so both BoardTouch_GetDetail() and
 * BoardTouch_GetMulti() are O(1) reads.
 */

#include "board_touch.h"
#include "board_config.h"

#include "esp_log.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_x.h"
#include "bsp/touch.h"
#include "esp_lcd_touch.h"

static const char *TAG = "board_touch";
static esp_lcd_touch_handle_t s_touch = nullptr;
static BoardTouchMulti        s_multi{};

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
    s_multi.count = 0;
    for (int i = 0; i < BOARD_TOUCH_MAX_POINTS; ++i) {
        s_multi.p[i].pressed = false;
        s_multi.p[i].x = 0;
        s_multi.p[i].y = 0;
        s_multi.p[i].id = -1;
    }

    if (!s_touch) return;

    esp_lcd_touch_read_data(s_touch);

    uint16_t tx[BOARD_TOUCH_MAX_POINTS] = {0};
    uint16_t ty[BOARD_TOUCH_MAX_POINTS] = {0};
    uint16_t strength[BOARD_TOUCH_MAX_POINTS] = {0};
    uint8_t  cnt = 0;
    bool pressed = esp_lcd_touch_get_coordinates(
        s_touch, tx, ty, strength, &cnt, BOARD_TOUCH_MAX_POINTS);
    if (!pressed || cnt == 0) return;
    if (cnt > BOARD_TOUCH_MAX_POINTS) cnt = BOARD_TOUCH_MAX_POINTS;

    for (uint8_t i = 0; i < cnt; ++i) {
        /* Apply the same rotation used by the framebuffer: the panel is
         * natively portrait (PANEL_W wide, PANEL_H tall) but we present a
         * landscape frame via
         *   logical (lx, ly) -> physical (PANEL_W - 1 - ly, lx).
         * The inverse of that, used here to go raw-touch -> logical:
         *   logical_x = ty
         *   logical_y = PANEL_W - 1 - tx
         */
        int lx = (int)ty[i];
        int ly = (int)(PANEL_W - 1) - (int)tx[i];
        if (lx < 0) lx = 0;
        if (lx >= BOARD_DISPLAY_WIDTH)  lx = BOARD_DISPLAY_WIDTH  - 1;
        if (ly < 0) ly = 0;
        if (ly >= BOARD_DISPLAY_HEIGHT) ly = BOARD_DISPLAY_HEIGHT - 1;

        BoardTouchDetail *d = &s_multi.p[s_multi.count++];
        d->pressed = true;
        d->x = lx;
        d->y = ly;
        /* esp_lcd_touch does not expose per-point IDs through this API.
         * Use the array index as a stable slot id within this sample.
         * Overlay manager treats "id != -1" as presence and otherwise
         * matches by slot order, which is good enough for 2-3 simultaneous
         * fingers driving a keyboard. */
        d->id = (int)i;
    }
}

extern "C" BoardTouchDetail BoardTouch_GetDetail(void)
{
    if (s_multi.count == 0) {
        BoardTouchDetail empty{};
        empty.pressed = false;
        empty.x = 0;
        empty.y = 0;
        empty.id = -1;
        return empty;
    }
    return s_multi.p[0];
}

extern "C" void BoardTouch_GetMulti(BoardTouchMulti *out)
{
    if (!out) return;
    *out = s_multi;
}
