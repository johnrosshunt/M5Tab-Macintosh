/*
 * board_display.h - display bring-up + framebuffer push + drawing surface
 *
 * The drawing surface for boot_gui.cpp / main.cpp is exposed as a C++
 * reference from BoardDisplay_Gfx(). Its concrete type differs per board:
 *   - Tab5:      reference to M5.Display (M5GFX / LovyanGFX)
 *   - Waveshare: reference to a MiniGfx software RGB565 canvas
 *
 * Both types expose the same subset of drawing primitives that boot_gui
 * relies on (fillRect, drawRect, drawString, setTextColor, ...), so the
 * calling code does not need to know which board it is on.
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
void BoardDisplay_FillRect(int x, int y, int w, int h, uint16_t color);
void BoardDisplay_SetBacklight(int percent);

/**
 * @brief Flush the software drawing surface (if any) to the physical
 *        panel. On Tab5 this is a no-op - M5.Display writes land on the
 *        panel immediately. On Waveshare this copies the MiniGfx PSRAM
 *        framebuffer to the MIPI-DSI back buffer.
 */
void BoardDisplay_Present(void);

#ifdef __cplusplus
} /* extern "C" */

/* Typed C++ accessor - per-board inline. Callers #include this header and
 * then either get the M5GFX LovyanGFX device (Tab5) or the MiniGfx shim
 * (Waveshare). The concrete type is visible at the call site so method
 * dispatch works even though the signatures differ across boards. */

#if defined(BOARD_M5STACK_TAB5)
#include <M5Unified.h>
static inline auto &BoardDisplay_Gfx(void) { return M5.Display; }
#elif defined(BOARD_WAVESHARE_P4_101)
#include "waveshare/mini_gfx.h"
MiniGfx &BoardDisplay_Gfx_Waveshare(void);
static inline MiniGfx &BoardDisplay_Gfx(void) { return BoardDisplay_Gfx_Waveshare(); }
#endif

#endif /* __cplusplus */
