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
    int  id;   /* Stable touch-slot id from the controller; -1 when !pressed. */
} BoardTouchDetail;

#define BOARD_TOUCH_MAX_POINTS 5

typedef struct BoardTouchMulti {
    uint8_t          count;     /* Number of currently-pressed fingers (0..BOARD_TOUCH_MAX_POINTS). */
    BoardTouchDetail p[BOARD_TOUCH_MAX_POINTS];
} BoardTouchMulti;

bool BoardTouch_Init(void);

/**
 * @brief Refresh cached touch state. Called by Board_Update() each frame.
 *        On Tab5 this is a no-op because M5.update() already did the work.
 */
void BoardTouch_Update(void);

/**
 * @brief Get the latest touch state. Coordinates are in physical display
 *        pixels. x/y are 0 when !pressed. Equivalent to the first slot in
 *        BoardTouch_GetMulti() when any finger is pressed.
 */
BoardTouchDetail BoardTouch_GetDetail(void);

/**
 * @brief Get up to BOARD_TOUCH_MAX_POINTS active touch points. Coordinates
 *        are in physical display pixels (landscape after HAL rotation).
 *        `out->count` is 0 when nothing is pressed.
 */
void BoardTouch_GetMulti(BoardTouchMulti *out);

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
