/*
 * board_display_tab5.cpp - Display HAL for M5Stack Tab5.
 *
 * Thin passthrough to M5.Display (M5GFX / LovyanGFX). The drawing surface
 * returned by BoardDisplay_Gfx() IS M5.Display.
 */

#include "board_display.h"
#include "board_config.h"

#include <M5Unified.h>

extern "C" bool BoardDisplay_Init(void)
{
    /* M5.begin() in Board_Init already initialized the display. */
    return true;
}

extern "C" int BoardDisplay_Width(void)
{
    return M5.Display.width();
}

extern "C" int BoardDisplay_Height(void)
{
    return M5.Display.height();
}

extern "C" void BoardDisplay_BeginTiles(void)
{
    M5.Display.startWrite();
}

extern "C" void BoardDisplay_EndTiles(void)
{
    M5.Display.endWrite();
}

extern "C" void BoardDisplay_PushTile(int x, int y, int w, int h, const uint16_t *pixels)
{
    M5.Display.setAddrWindow(x, y, w, h);
    M5.Display.writePixelsDMA(pixels, (int32_t)w * h);
}

extern "C" void BoardDisplay_WaitPush(void)
{
    M5.Display.waitDMA();
}

extern "C" void BoardDisplay_PushFullFrame(const uint16_t *pixels)
{
    M5.Display.pushImage(0, 0, M5.Display.width(), M5.Display.height(), pixels);
}

extern "C" void BoardDisplay_ClearScreen(uint16_t color)
{
    M5.Display.fillScreen(color);
}

extern "C" void BoardDisplay_FillRect(int x, int y, int w, int h, uint16_t color)
{
    M5.Display.fillRect(x, y, w, h, color);
}

extern "C" void BoardDisplay_Present(void)
{
    /* M5.Display writes are synchronous for the calls we use; nothing to do. */
}

extern "C" void BoardDisplay_SetBacklight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    M5.Display.setBrightness((percent * 255) / 100);
}

/* The typed BoardDisplay_Gfx() accessor is defined inline in
 * board_display.h for Tab5 (returns M5.Display directly), so no out-of-
 * line implementation is needed here. */
