/*
 * board_sd_tab5.cpp - microSD mount for M5Stack Tab5 (SPI mode).
 *
 * Mirrors the initSDCard() behaviour that lived in src/main.cpp before the
 * board HAL landed. Keeping SPI mode on Tab5 preserves the existing, tested
 * throughput characteristics. The mount point is the Arduino SD library
 * default ("/" - all paths are relative to the SD root).
 */

#include "board_sd.h"
#include "board_config.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

static bool     s_mounted = false;
static uint64_t s_size_mb = 0;

extern "C" bool BoardSD_Init(void)
{
    if (s_mounted) return true;

    Serial.println("[SD] Initializing SD card (SPI mode)...");
    Serial.printf("[SD] Pins: SCK=%d MOSI=%d MISO=%d CS=%d\n",
                  BOARD_SD_SPI_SCK, BOARD_SD_SPI_MOSI, BOARD_SD_SPI_MISO, BOARD_SD_SPI_CS);

    SPI.begin(BOARD_SD_SPI_SCK, BOARD_SD_SPI_MISO, BOARD_SD_SPI_MOSI, BOARD_SD_SPI_CS);

    constexpr uint32_t kSdSpiHz = 25000000;
    if (!SD.begin(BOARD_SD_SPI_CS, SPI, kSdSpiHz)) {
        Serial.println("[SD] ERROR: initialization failed!");
        return false;
    }

    s_size_mb = SD.cardSize() / (1024 * 1024);
    s_mounted = true;
    Serial.printf("[SD] Mounted, %llu MB\n", (unsigned long long)s_size_mb);
    return true;
}

extern "C" uint64_t BoardSD_CardSizeMB(void)
{
    return s_size_mb;
}
