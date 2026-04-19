/*
 * board_touch_tab5.cpp - touch HAL for M5Stack Tab5.
 */

#include "board_touch.h"

#include <M5Unified.h>

extern "C" bool BoardTouch_Init(void)
{
    /* M5.begin() already set up the ST7123 touch controller. */
    return true;
}

extern "C" void BoardTouch_Update(void)
{
    /* M5.update() pumps the touch state. It's already called from
     * Board_Update(), so nothing to do here. */
}

extern "C" BoardTouchDetail BoardTouch_GetDetail(void)
{
    auto d = M5.Touch.getDetail();
    BoardTouchDetail out;
    out.pressed = d.isPressed();
    out.x       = d.x;
    out.y       = d.y;
    return out;
}
