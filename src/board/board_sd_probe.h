/*
 * board_sd_probe.h - lightweight filesystem detector for the microSD card.
 *
 * The pioarduino IDF ships its FatFs precompiled with FF_FS_EXFAT=0, so
 * the regular SD.begin() / SD_MMC.begin() paths can not mount exFAT
 * cards: they fail with the generic "init failed" message, leaving
 * users with a card that "doesn't work" and no clear cause. This helper
 * probes the card's boot sector independently of Arduino's mount path
 * and reports whether the card is FAT12/16/32 (mountable), exFAT
 * (recognised but unsupported), or unknown.
 *
 * Implementation lives in the per-board source files (Tab5 uses
 * sdspi_host; Waveshare uses sdmmc_host). Both expose this same C API.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_SD_FS_UNKNOWN = 0,  /* card not detected, or boot sector unrecognisable */
    BOARD_SD_FS_FAT,          /* FAT12 / FAT16 / FAT32 - supported by the firmware */
    BOARD_SD_FS_EXFAT,        /* exFAT - card is fine but firmware can't mount it */
} BoardSdFsType;

/**
 * Probe sector 0 of the microSD card to identify its filesystem.
 *
 * Returns BOARD_SD_FS_FAT if the card holds a recognisable FAT volume,
 * BOARD_SD_FS_EXFAT if the boot sector matches the exFAT signature, or
 * BOARD_SD_FS_UNKNOWN if the card couldn't be probed (no card, low-level
 * init failure, unrecognised boot sector).
 *
 * Safe to call after a failed BoardSD_Init() - the implementation does
 * its own host bring-up and tear-down so it doesn't depend on the high
 * level SD library being in any particular state.
 */
BoardSdFsType BoardSD_ProbeFilesystem(void);

#ifdef __cplusplus
}
#endif
