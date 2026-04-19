/**
 * @file main.cpp
 * BasiliskII ESP32 - Macintosh Emulator
 *
 * Initializes the board (display / touch / SD), runs the boot GUI, and
 * hands off to the BasiliskII emulator core. All board-specific bring-up
 * is hidden behind the small HAL in src/board/.
 */

#include <Arduino.h>

#include "board.h"
#include "board_config.h"
#include "board_display.h"
#include "board_sd.h"  /* pulls in SD_FS (SD on Tab5, SD_MMC on Waveshare) */

#include "boot_gui.h"

/* Forward declarations for BasiliskII functions */
extern void basilisk_setup(void);
extern void basilisk_loop(void);
extern bool basilisk_is_running(void);

/* ============================================================================
 * Display helpers (cross-board)
 * ==========================================================================*/

/* TFT color constants we use for the startup screens. They map to the same
 * RGB565 values as LovyanGFX/M5GFX, so both backends draw the right colors. */
#ifndef TFT_BLACK
#define TFT_BLACK   0x0000u
#endif
#ifndef TFT_WHITE
#define TFT_WHITE   0xFFFFu
#endif
#ifndef TFT_MAROON
#define TFT_MAROON  0x7800u
#endif

static void showStartupScreen(void)
{
    auto &gfx = BoardDisplay_Gfx();
    gfx.fillScreen(TFT_BLACK);
    gfx.setTextColor(TFT_WHITE);
    gfx.setTextSize(2);
    const int cx = BoardDisplay_Width()  / 2;
    const int cy = BoardDisplay_Height() / 2;
    gfx.setTextDatum(MC_DATUM);
    gfx.drawString("BasiliskII ESP32", cx, cy - 60);
    gfx.drawString("Macintosh Emulator", cx, cy - 20);
    gfx.setTextSize(1);
    gfx.drawString("Initializing...", cx, cy + 40);
#if defined(BOARD_WAVESHARE_P4_101)
    gfx.flushAll();
#endif
}

static void showErrorScreen(const char *error)
{
    auto &gfx = BoardDisplay_Gfx();
    gfx.fillScreen(TFT_MAROON);
    gfx.setTextColor(TFT_WHITE);
    gfx.setTextSize(2);
    const int cx = BoardDisplay_Width() / 2;
    gfx.setTextDatum(MC_DATUM);
    gfx.drawString("ERROR", cx, 100);
    gfx.setTextSize(1);
    gfx.drawString(error, cx, 160);
#if defined(BOARD_WAVESHARE_P4_101)
    gfx.flushAll();
#endif
}

/* ============================================================================
 * SD card bring-up + ROM existence check
 * ==========================================================================*/

static bool initSDCard(void)
{
    Serial.println("[MAIN] Initializing SD card via board HAL...");
    if (!BoardSD_Init()) {
        Serial.println("[MAIN] ERROR: SD card initialization failed");
        Serial.println("[MAIN] Ensure SD card is inserted and formatted as FAT32");
        return false;
    }

    Serial.printf("[MAIN] SD card initialized: %llu MB\n",
                  (unsigned long long)BoardSD_CardSizeMB());

    /* SD_FS is SD on Tab5 (SPI) and SD_MMC on Waveshare (native SDMMC). */
    const bool hasROM   = SD_FS.exists("/Q650.ROM");
    const bool hasDisk  = SD_FS.exists("/Macintosh.dsk");
    const bool hasFloppy = SD_FS.exists("/DiskTools1.img");

    Serial.printf("[MAIN] Q650.ROM: %s\n",      hasROM    ? "found" : "MISSING");
    Serial.printf("[MAIN] Macintosh.dsk: %s\n", hasDisk   ? "found" : "MISSING");
    Serial.printf("[MAIN] DiskTools1.img: %s\n", hasFloppy ? "found" : "MISSING");

    if (!hasROM) {
        Serial.println("[MAIN] ERROR: Q650.ROM not found on SD card!");
        return false;
    }
    return true;
}

/* ============================================================================
 * Setup
 * ==========================================================================*/

void setup(void)
{
    Serial.begin(115200);
    delay(500);

    Serial.println("\n\n========================================");
    Serial.println("  BasiliskII ESP32 - Macintosh Emulator");
    Serial.printf ("  Board: %s\n", BOARD_NAME);
    Serial.println("========================================\n");

    if (!Board_Init()) {
        Serial.println("[MAIN] ERROR: Board_Init failed - halting");
        while (true) { delay(1000); }
    }
    if (!BoardDisplay_Init()) {
        Serial.println("[MAIN] ERROR: BoardDisplay_Init failed - halting");
        while (true) { delay(1000); }
    }

    showStartupScreen();

    Serial.printf("[MAIN] Display: %dx%d\n", BoardDisplay_Width(), BoardDisplay_Height());
    Serial.printf("[MAIN] Free heap: %d bytes\n",     ESP.getFreeHeap());
    Serial.printf("[MAIN] Free PSRAM: %d bytes\n",    ESP.getFreePsram());
    Serial.printf("[MAIN] Total PSRAM: %d bytes\n",   ESP.getPsramSize());
    Serial.printf("[MAIN] CPU Freq: %d MHz\n",        ESP.getCpuFreqMHz());

    if (!initSDCard()) {
        showErrorScreen("SD card or ROM file not found");
        Serial.println("[MAIN] Halting - SD card initialization failed");
        while (true) { delay(1000); }
    }

    if (!BootGUI_Init()) {
        showErrorScreen("Boot GUI initialization failed");
        Serial.println("[MAIN] Halting - Boot GUI initialization failed");
        while (true) { delay(1000); }
    }

    BootGUI_Run();

    Serial.println("[MAIN] Starting BasiliskII emulator...");
    basilisk_setup();

    Serial.println("[MAIN] Emulator exited");
}

/* ============================================================================
 * Main Loop
 * ==========================================================================*/

void loop(void)
{
    Board_Update();
    /* Emulator owns its own tasks after basilisk_setup(); we only get here
     * if it exits. */
    delay(100);
}
