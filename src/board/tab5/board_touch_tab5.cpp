/*
 * board_touch_tab5.cpp - touch HAL for M5Stack Tab5.
 *
 * We pull raw portrait (720x1280) coordinates straight from the
 * ST7123/GT911 driver and rotate them ourselves to match MiniGfx's
 * logical landscape (1280x720). Going through M5.Touch.getDetail()
 * would apply M5GFX's display rotation, but we no longer draw through
 * M5.Display - our framebuffer rotation is done inside MiniGfx - so
 * the two rotations were off by 90 degrees (X and Y appeared swapped).
 */

#include "board_touch.h"
#include "board_config.h"

#include <M5Unified.h>

static constexpr int TAB5_PANEL_W_PORTRAIT = 720;
static constexpr int TAB5_PANEL_H_PORTRAIT = 1280;

extern "C" bool BoardTouch_Init(void)
{
    /* M5.begin() already set up the GT911 touch controller. */
    return true;
}

extern "C" void BoardTouch_Update(void)
{
    /* M5.update() pumps the touch state. It's already called from
     * Board_Update(), so nothing to do here. */
}

extern "C" BoardTouchDetail BoardTouch_GetDetail(void)
{
    /* getTouchPointRaw gives us native portrait (720x1280) coordinates
     * regardless of the display rotation state M5GFX is in. */
    auto raw = M5.Touch.getTouchPointRaw(0);
    auto det = M5.Touch.getDetail();

    BoardTouchDetail out;
    out.pressed = det.isPressed();

    int px = raw.x;
    int py = raw.y;
    if (px < 0) px = 0;
    if (px >= TAB5_PANEL_W_PORTRAIT) px = TAB5_PANEL_W_PORTRAIT - 1;
    if (py < 0) py = 0;
    if (py >= TAB5_PANEL_H_PORTRAIT) py = TAB5_PANEL_H_PORTRAIT - 1;

    /* MiniGfx is configured with setFlip180(true) on the Tab5, which
     * maps logical landscape (lx, ly) to portrait pixel
     * (px, py) = (ly, _ph - 1 - lx). Inverting that:
     *   lx = _ph - 1 - py = 1279 - py
     *   ly = px
     * This matches the 90-degree CCW rotation used in MiniGfx and in
     * BoardDisplay_PushTile's tile rotation. */
    out.x = (TAB5_PANEL_H_PORTRAIT - 1) - py;
    out.y = px;
    return out;
}
