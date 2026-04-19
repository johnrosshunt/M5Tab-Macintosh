/*
 * board_tab5.cpp - top-level board lifecycle for M5Stack Tab5.
 *
 * All the heavy lifting (display, touch, audio, power) is done by
 * M5Unified. This file just calls M5.begin() once, forwards M5.update()
 * for per-frame touch polling, and returns true.
 */

#include "board.h"

#include <M5Unified.h>

static bool s_board_inited = false;

extern "C" bool Board_Init(void)
{
    if (s_board_inited) {
        return true;
    }
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(3);  /* landscape - matches pre-HAL behaviour */
    s_board_inited = true;
    return true;
}

extern "C" void Board_Update(void)
{
    M5.update();
}
