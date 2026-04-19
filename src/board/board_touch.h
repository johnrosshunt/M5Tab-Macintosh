/*
 * board_touch.h - touchscreen read interface
 *
 * Both boards expose a capacitive touchscreen wired to I2C. This HAL maps
 * whatever the underlying driver reports to physical display coordinates
 * (x in [0, BoardDisplay_Width), y in [0, BoardDisplay_Height)).
 *
 * BoardTouch_GetDetail() returns a POD struct compatible with the
 * M5.Touch.getDetail() subset that boot_gui.cpp and input_esp32.cpp use.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BoardTouchDetail {
    bool pressed;
    int  x;
    int  y;
} BoardTouchDetail;

bool BoardTouch_Init(void);

/**
 * @brief Refresh cached touch state. Called by Board_Update() each frame.
 *        On Tab5 this is a no-op because M5.update() already did the work.
 */
void BoardTouch_Update(void);

/**
 * @brief Get the latest touch state. Coordinates are in physical display
 *        pixels. x/y are 0 when !pressed.
 */
BoardTouchDetail BoardTouch_GetDetail(void);

#ifdef __cplusplus
} /* extern "C" */

/* For call sites that used `auto touch = M5.Touch.getDetail();` patterns,
 * wrap isPressed() as a method so boot_gui.cpp can keep its shape. */
struct BoardTouchDetailObj {
    BoardTouchDetail d;
    bool isPressed() const { return d.pressed; }
    int x_() const { return d.x; }
    int y_() const { return d.y; }
};

#endif
