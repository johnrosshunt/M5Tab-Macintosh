/*
 * board_waveshare.cpp - top-level board lifecycle for the Waveshare
 * ESP32-P4-WIFI6-Touch-LCD-10.1 board.
 *
 * Unlike the Tab5 path (which leans on M5.begin()), we explicitly bring
 * each subsystem up in the correct order:
 *   1. Display + backlight (also brings up I2C master on GPIO7/8)
 *   2. Touch (shares the display I2C master)
 *
 * Audio and SD come up later when the emulator core requests them, so
 * they are not initialized here.
 *
 * Board_Update() polls the GT911 every tick; since boot_gui.cpp and
 * input_esp32.cpp both look at the cached touch state via
 * BoardTouch_GetDetail(), that gives us a consistent picture without
 * contention.
 */

#include "board.h"
#include "board_display.h"
#include "board_touch.h"

#include <stdbool.h>

#include "esp_log.h"

static const char *TAG = "board";
static bool s_inited = false;

extern "C" bool Board_Init(void)
{
    if (s_inited) return true;

    if (!BoardDisplay_Init()) {
        ESP_LOGE(TAG, "display init failed");
        return false;
    }
    if (!BoardTouch_Init()) {
        ESP_LOGE(TAG, "touch init failed - continuing without touch");
        /* Non-fatal - the boot countdown still fires even without touch. */
    }

    s_inited = true;
    return true;
}

extern "C" void Board_Update(void)
{
    BoardTouch_Update();
}
