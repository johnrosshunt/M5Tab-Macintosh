/*
 * board_display.h - display bring-up + framebuffer push + drawing surface
 *
 * The drawing surface for boot_gui.cpp / main.cpp is exposed as a C++
 * reference from BoardDisplay_Gfx(). Both boards use the same MiniGfx
 * software RGB565 canvas, flushed to the panel via
 * esp_lcd_panel_draw_bitmap.
 *
 * The video pipeline (video_esp32.cpp) does NOT go through BoardDisplay_Gfx.
 * It pushes RGB565 tiles directly via BoardDisplay_PushTile().
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

bool BoardDisplay_Init(void);
int  BoardDisplay_Width(void);
int  BoardDisplay_Height(void);
void BoardDisplay_BeginTiles(void);
void BoardDisplay_EndTiles(void);
void BoardDisplay_PushTile(int x, int y, int w, int h, const uint16_t *pixels);
void BoardDisplay_WaitPush(void);
void BoardDisplay_PushFullFrame(const uint16_t *pixels);

/**
 * @brief Paint the full display with a single RGB565 color.
 *
 *  Used by the video subsystem to blank the screen on init. The HAL
 *  picks whatever scratch buffer or fill primitive is cheapest on the
 *  current board. Does not require a prior BeginTiles/EndTiles pair.
 */
void BoardDisplay_ClearScreen(uint16_t color);
void BoardDisplay_FillRect(int x, int y, int w, int h, uint16_t color);
void BoardDisplay_SetBacklight(int percent);

/**
 * @brief Flush the software drawing surface to the physical panel. On
 *        both boards this copies the MiniGfx PSRAM framebuffer to the
 *        MIPI-DSI back buffer via esp_lcd_panel_draw_bitmap.
 */
void BoardDisplay_Present(void);

#ifdef __cplusplus
} /* extern "C" */

/* Typed C++ accessor - returns the per-board drawing surface. Both
 * supported boards now use the MiniGfx software framebuffer backed by
 * esp_lcd_panel_draw_bitmap, so the surface type is uniform across
 * boards and callers don't need board-specific drawing code. */

#if defined(BOARD_M5STACK_TAB5) || defined(BOARD_WAVESHARE_P4_101)
#include "mini_gfx/mini_gfx.h"
MiniGfx &BoardDisplay_Gfx_Board(void);
static inline MiniGfx &BoardDisplay_Gfx(void) { return BoardDisplay_Gfx_Board(); }
#endif

#endif /* __cplusplus */
