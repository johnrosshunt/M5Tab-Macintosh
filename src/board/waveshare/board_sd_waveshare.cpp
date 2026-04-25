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
#include "board_sd_probe.h"
#include "board_config.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

static const char *TAG = "board_sd";
static bool     s_mounted = false;
static uint64_t s_size_mb = 0;

extern "C" bool BoardSD_Init(void)
{
    if (s_mounted) return true;

    ESP_LOGI(TAG, "Mounting SD card via Arduino SD_MMC (4-bit, slot 0, high-speed)...");

    /* SD_MMC.begin(mountpoint, mode1bit, formatOnFail, frequency, maxFiles)
     * On P4 this also brings up the on-chip LDO that powers SD_VDD.
     *
     * We ask for SDMMC_FREQ_HIGHSPEED (40 MHz) - roughly double the 20 MHz
     * SDMMC_FREQ_DEFAULT. The IDF host driver renegotiates down to a
     * supported clock if the card doesn't advertise HS mode, so the worst
     * case is ending up at the same 20 MHz we had before. */
    if (!SD_MMC.begin(BOARD_SD_MOUNT_POINT,
                      /* mode1bit      */ false,
                      /* formatOnFail  */ false,
                      /* freq_khz      */ SDMMC_FREQ_HIGHSPEED,
                      /* max open files*/ 5)) {
        ESP_LOGW(TAG, "SD_MMC.begin at HIGHSPEED failed, retrying at DEFAULT (20 MHz)");
        if (!SD_MMC.begin(BOARD_SD_MOUNT_POINT,
                          /* mode1bit      */ false,
                          /* formatOnFail  */ false,
                          /* freq_khz      */ SDMMC_FREQ_DEFAULT,
                          /* max open files*/ 5)) {
            ESP_LOGE(TAG, "SD_MMC.begin failed at both HIGHSPEED and DEFAULT");
            BoardSdFsType fs = BoardSD_ProbeFilesystem();
            if (fs == BOARD_SD_FS_EXFAT) {
                Serial.println("[SD]   Card detected, formatted as exFAT.");
                Serial.println("[SD]   This firmware uses the precompiled FatFs from");
                Serial.println("[SD]   pioarduino which does NOT include exFAT support.");
                Serial.println("[SD]   Please reformat the card as FAT32.");
                Serial.println("[SD]   (Mac OS HFS disk images are 2 GB max anyway, so the");
                Serial.println("[SD]   FAT32 4 GB per-file limit is not a practical issue.)");
            } else if (fs == BOARD_SD_FS_FAT) {
                Serial.println("[SD]   Card detected with a FAT volume but mount still");
                Serial.println("[SD]   failed - the volume may be damaged. Try reformatting.");
            } else {
                Serial.println("[SD]   No recognisable boot sector found. Possible causes:");
                Serial.println("[SD]   card unformatted, contacts dirty, or not seated.");
                Serial.println("[SD]   If the card was formatted as exFAT (default for");
                Serial.println("[SD]   modern cards >32 GB), reformat as FAT32.");
            }
            return false;
        }
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

/*
 * Probe sector 0 directly via the ESP-IDF SDMMC host driver. This runs
 * after SD_MMC.begin() has failed and torn down its own host state, so
 * we do a fresh sdmmc_host_init() / init_slot() / card_init() / read /
 * deinit cycle. The probe is a diagnostic helper - any failure path
 * collapses to BOARD_SD_FS_UNKNOWN, never crashes.
 */
extern "C" BoardSdFsType BoardSD_ProbeFilesystem(void)
{
    sdmmc_host_t        host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    /* The Waveshare SDMMC slot is wired 4-bit on default IO MUX pins
     * (matches what SD_MMC.begin uses with mode1bit=false). Run the
     * probe at the conservative DEFAULT clock so a marginal card is
     * less likely to drop us during identification. */
    slot.width = 4;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "probe: sdmmc_host_init err=0x%x", err);
        return BOARD_SD_FS_UNKNOWN;
    }
    err = sdmmc_host_init_slot(host.slot, &slot);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "probe: sdmmc_host_init_slot err=0x%x", err);
        sdmmc_host_deinit();
        return BOARD_SD_FS_UNKNOWN;
    }

    sdmmc_card_t *card = (sdmmc_card_t *)heap_caps_calloc(
        1, sizeof(sdmmc_card_t), MALLOC_CAP_DEFAULT);
    if (card == NULL) {
        sdmmc_host_deinit();
        return BOARD_SD_FS_UNKNOWN;
    }

    BoardSdFsType result = BOARD_SD_FS_UNKNOWN;
    err = sdmmc_card_init(&host, card);
    if (err == ESP_OK) {
        /* Heap-allocate the sector buffer so it doesn't permanently
         * live in BSS and crowd out the 68k emulator's internal-SRAM
         * structures (mem_banks, cpufunctbl). 512 bytes is the
         * standard SD sector size and what sdmmc_read_sectors expects. */
        uint8_t *sector_buf = (uint8_t *)heap_caps_malloc(512, MALLOC_CAP_DMA);
        if (sector_buf != NULL) {
            err = sdmmc_read_sectors(card, sector_buf, 0, 1);
            if (err == ESP_OK) {
                if (memcmp(&sector_buf[3], "EXFAT   ", 8) == 0) {
                    result = BOARD_SD_FS_EXFAT;
                } else if (memcmp(&sector_buf[82], "FAT32   ", 8) == 0 ||
                           memcmp(&sector_buf[54], "FAT12   ", 8) == 0 ||
                           memcmp(&sector_buf[54], "FAT16   ", 8) == 0) {
                    result = BOARD_SD_FS_FAT;
                }
            } else {
                ESP_LOGW(TAG, "probe: sdmmc_read_sectors err=0x%x", err);
            }
            free(sector_buf);
        }
    } else {
        ESP_LOGW(TAG, "probe: sdmmc_card_init err=0x%x", err);
    }

    free(card);
    sdmmc_host_deinit();
    return result;
}
