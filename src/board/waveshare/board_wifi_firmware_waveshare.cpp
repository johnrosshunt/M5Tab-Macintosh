/*
 * board_wifi_firmware_waveshare.cpp - Bake-in OTA for the ESP32-C6.
 *
 * On the Waveshare P4 10.1, WiFi / Bluetooth run on an ESP32-C6 slave
 * reached via esp_wifi_remote -> esp_hosted -> SDIO slot 1. Arduino's
 * ESP-Hosted layer expects the slave firmware version to exactly match
 * the host library (``ESP_HOSTED_VERSION_*``); a mismatch leaves WiFi
 * permanently stuck at "Could not get slave firmware version".
 *
 * The Arduino core ships an ``ESP_HostedOTA`` example that downloads a
 * fresh blob over HTTPS and streams it into the C6. That's a chicken-
 * and-egg problem when the mismatch itself is what's preventing WiFi
 * from working.
 *
 * Solution: bake the right blob into the host firmware. The pre-build
 * ``scripts/fetch_c6_firmware.py`` downloads
 * ``esp32c6-v<ESP_HOSTED_VERSION_*>.bin`` from Espressif's CDN and
 * emits ``src/generated/c6_firmware_blob.S`` with a ``.incbin`` into
 * .rodata. We expose three symbols:
 *
 *   - ``c6_firmware_bin``         - byte 0 of the blob
 *   - ``c6_firmware_bin_end``     - one byte past the last byte
 *   - ``c6_firmware_version_str`` - NUL-terminated "X.Y.Z" for logging
 *
 * At runtime, after ``initWiFi()`` has brought up the hosted transport,
 * boot_gui calls BoardWifi_MaybeUpdateCoprocessorFirmware(). If the
 * versions differ, we push the blob via the esp_hosted slave OTA API
 * (``hostedBeginUpdate`` / ``hostedWriteUpdate`` / ``hostedEndUpdate``
 * / ``hostedActivateUpdate``). Activate reboots the C6; the caller
 * then soft-resets the P4 so the next boot comes up on matching
 * firmware and WiFi actually works.
 */

#include "board_wifi.h"

#include <Arduino.h>
#include "sdkconfig.h"

#if defined(CONFIG_ESP_WIFI_REMOTE_ENABLED)

#include "esp32-hal-hosted.h"

/*
 * Symbols produced by scripts/fetch_c6_firmware.py. The generated
 * assembly uses ``.incbin`` so we get raw bytes with no ELF header,
 * trailing NUL, or size prefix - the chunk is literally [bin .. end).
 */
extern "C" const uint8_t c6_firmware_bin[];
extern "C" const uint8_t c6_firmware_bin_end[];
extern "C" const char    c6_firmware_version_str[];

/* Fed to hostedWriteUpdate() in 2KB chunks so the SDIO TX ring stays
 * happy - the ESP_HostedOTA example uses the same size. */
static constexpr size_t kChunkSize = 2048;

/* Upstream's ``esp_hosted_slave_ota_activate`` resets the C6, which
 * tears down the SDIO link; waiting for the RPC response can hang the
 * P4 for many seconds waiting on a queue that will never drain. We
 * print progress by byte count so a serial watcher can confirm things
 * are actually moving. */
static constexpr size_t kProgressStep = 64 * 1024;

extern "C" bool BoardWifi_IsCoprocessorFirmwareOutdated(void)
{
    if (!hostedIsInitialized()) {
        /* hostedHasUpdate logs an error if called before init; mirror
         * its behavior but stay silent - we may be called speculatively
         * just to decide whether to show a splash. */
        return false;
    }
    return hostedHasUpdate();
}

extern "C" BoardWifiFirmwareResult BoardWifi_ApplyCoprocessorFirmware(void)
{
    if (!hostedIsInitialized()) {
        /* initWiFi() should have brought the hosted transport up by the
         * time we're called. If it didn't, nothing we can do here. */
        Serial.println("[C6 OTA] Skipping: esp-hosted is not initialized");
        return BOARD_WIFI_FW_FAILED;
    }

    const size_t blob_size = static_cast<size_t>(c6_firmware_bin_end - c6_firmware_bin);
    Serial.printf(
        "[C6 OTA] Updating ESP32-C6 slave firmware to v%s (%u bytes embedded)\n",
        c6_firmware_version_str, static_cast<unsigned>(blob_size));

    if (!hostedBeginUpdate()) {
        Serial.println("[C6 OTA] hostedBeginUpdate() failed");
        return BOARD_WIFI_FW_FAILED;
    }

    size_t next_progress = kProgressStep;
    size_t offset = 0;
    while (offset < blob_size) {
        size_t chunk = blob_size - offset;
        if (chunk > kChunkSize) {
            chunk = kChunkSize;
        }
        /* hostedWriteUpdate doesn't mutate its input but declares a
         * non-const pointer; cast away const for the call. */
        uint8_t *buf = const_cast<uint8_t *>(c6_firmware_bin + offset);
        if (!hostedWriteUpdate(buf, static_cast<uint32_t>(chunk))) {
            Serial.printf("[C6 OTA] hostedWriteUpdate failed at offset %u/%u\n",
                          static_cast<unsigned>(offset), static_cast<unsigned>(blob_size));
            return BOARD_WIFI_FW_FAILED;
        }
        offset += chunk;
        if (offset >= next_progress || offset == blob_size) {
            Serial.printf("[C6 OTA]   wrote %u / %u bytes (%u%%)\n",
                          static_cast<unsigned>(offset), static_cast<unsigned>(blob_size),
                          static_cast<unsigned>((offset * 100U) / blob_size));
            next_progress += kProgressStep;
        }
        /* yield() keeps the WDT fed and lets any hosted background
         * tasks drain the SDIO FIFOs between chunks. */
        yield();
    }

    if (!hostedEndUpdate()) {
        Serial.println("[C6 OTA] hostedEndUpdate() failed");
        return BOARD_WIFI_FW_FAILED;
    }

    Serial.println("[C6 OTA] Activating new co-processor firmware...");
    if (!hostedActivateUpdate()) {
        Serial.println("[C6 OTA] hostedActivateUpdate() failed");
        return BOARD_WIFI_FW_FAILED;
    }

    Serial.println("[C6 OTA] Activation done - caller should reboot the host");
    return BOARD_WIFI_FW_UPDATED;
}

#else /* !CONFIG_ESP_WIFI_REMOTE_ENABLED */

extern "C" bool BoardWifi_IsCoprocessorFirmwareOutdated(void)
{
    return false;
}

extern "C" BoardWifiFirmwareResult BoardWifi_ApplyCoprocessorFirmware(void)
{
    return BOARD_WIFI_FW_NOT_SUPPORTED;
}

#endif /* CONFIG_ESP_WIFI_REMOTE_ENABLED */
