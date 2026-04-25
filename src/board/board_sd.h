/*
 * board_sd.h - microSD mount interface
 *
 * Both boards put the microSD card on the same set of GPIOs (39-44), but
 * the Tab5 firmware drives it in SPI mode while the Waveshare BSP uses
 * SDMMC 4-bit mode. BoardSD_Init() hides the difference; after it returns
 * the card is mounted at BOARD_SD_MOUNT_POINT ("/sd" on both boards) and
 * the existing Arduino SD.* or POSIX VFS APIs work as-is. App code that
 * uses Arduino's SD_FS.open("/Q650.ROM") still uses SD-root-relative
 * paths because Arduino's File class transparently prepends the VFS
 * mountpoint internally.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool BoardSD_Init(void);

/** @brief Card capacity in MB, or 0 if not mounted. */
uint64_t BoardSD_CardSizeMB(void);

#ifdef __cplusplus
}

/* Board-specific Arduino filesystem alias. Call sites can use SD_FS.exists(),
 * SD_FS.open(), etc. without knowing whether the card is behind the Arduino
 * SD (SPI) library or SD_MMC (native SDMMC) library. */
#include "board_config.h"
#if defined(BOARD_M5STACK_TAB5)
  #include <SD.h>
  #define SD_FS SD
#elif defined(BOARD_WAVESHARE_P4_101)
  #include <SD_MMC.h>
  #define SD_FS SD_MMC
#endif

#endif /* __cplusplus */
