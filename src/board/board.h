/*
 * board.h - top-level board lifecycle
 *
 * Each board target implements Board_Init() to bring up display, touch,
 * audio, and any board-specific peripherals, and Board_Update() to pump
 * per-frame work (touch polling, button events).
 */
#pragma once

#include <stdbool.h>
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up display, touch, audio, I2C. Must be the first board call
 *        from setup(). Safe to call multiple times - subsequent calls are
 *        ignored. Returns false if any critical peripheral failed to init.
 */
bool Board_Init(void);

/**
 * @brief Per-frame update hook. On Tab5 this calls M5.update() to pump
 *        touch/button state. On Waveshare it polls the GT911 via the BSP.
 */
void Board_Update(void);

#ifdef __cplusplus
}
#endif
