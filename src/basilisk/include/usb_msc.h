/*
 * usb_msc.h - Pre-boot "USB Disk" mode.
 *
 * Re-initializes the microSD card through the ESP-IDF sdmmc/sdspi driver
 * (after Arduino's SD wrapper has released it) and hands the raw
 * sdmmc_card_t to TinyUSB's MSC storage backend so a connected host PC
 * sees the card as a standard USB Mass Storage device. Used from the
 * boot GUI's "USB Disk" screen.
 *
 * The OTG controller is shared with the HID host stack used during
 * emulation, but the two are mutually exclusive in time: USB Disk mode
 * only runs in pre-boot, HID host only once the emulator is running.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Called once at startup (or the first time we need it) to check that
 *        required build configuration is present. Returns false if the
 *        required TinyUSB MSC Kconfig options are missing, in which case the
 *        boot GUI should hide the "USB Disk" button.
 */
bool UsbMsc_IsSupported(void);

/**
 * @brief Enter USB Disk mode.
 *
 * Unmounts the Arduino SD wrapper, reclaims the card via IDF APIs, and
 * starts TinyUSB with the MSC class. Blocks (caller expected to poll) and
 * does not return until @ref UsbMsc_RequestExit is called from another
 * task (the boot GUI's touch loop).
 *
 * @param[out] out_error_msg  On failure, set to a short human-readable
 *                            description (static string). May be NULL.
 * @return true on clean enter+run+exit, false if the USB stack refused to
 *         start. In the failure case the SD is re-mounted via Arduino
 *         before returning so the boot GUI can continue.
 */
bool UsbMsc_Enter(const char **out_error_msg);

/**
 * @brief Ask the @ref UsbMsc_Enter loop to tear down and return. Safe to
 *        call from any task.
 */
void UsbMsc_RequestExit(void);

/**
 * @brief Query whether the host PC currently has the disk mounted. Useful
 *        for showing a "Safe to eject" hint in the UI.
 */
bool UsbMsc_HostMounted(void);

#ifdef __cplusplus
}
#endif
