/*
 * board_sd_waveshare.cpp - microSD mount for the Waveshare board.
 *
 * The P4 SDMMC slot sits on the IO MUX default pins and needs the on-chip
 * LDO (channel 4) powered up before the card will respond. Arduino's
 * SD_MMC library in pioarduino 55.x knows how to do both on ESP32-P4, so
 * we use SD_MMC.begin() here and expose the global `SD_MMC` object as the
 * board's filesystem (aliased as SD_FS in board_sd.h).
 */

#include "board_sd.h"
#include "board_config.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include "esp_log.h"

static const char *TAG = "board_sd";
static bool     s_mounted = false;
static uint64_t s_size_mb = 0;

extern "C" bool BoardSD_Init(void)
{
    if (s_mounted) return true;

    ESP_LOGI(TAG, "Mounting SD card via Arduino SD_MMC (4-bit, slot 0)...");

    /* SD_MMC.begin(mountpoint, mode1bit, formatOnFail, frequency, maxFiles)
     * On P4 this also brings up the on-chip LDO that powers SD_VDD. */
    if (!SD_MMC.begin(BOARD_SD_MOUNT_POINT,
                      /* mode1bit      */ false,
                      /* formatOnFail  */ false,
                      /* freq_khz      */ SDMMC_FREQ_DEFAULT,
                      /* max open files*/ 5)) {
        ESP_LOGE(TAG, "SD_MMC.begin failed");
        return false;
    }

    s_size_mb = SD_MMC.cardSize() / (1024ULL * 1024ULL);
    s_mounted = true;
    ESP_LOGI(TAG, "SD mounted at %s (%llu MB)", BOARD_SD_MOUNT_POINT,
             (unsigned long long)s_size_mb);
    return true;
}

extern "C" uint64_t BoardSD_CardSizeMB(void)
{
    return s_size_mb;
}
