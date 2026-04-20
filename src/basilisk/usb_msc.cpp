/*
 * usb_msc.cpp - Pre-boot "USB Disk" mode implementation.
 *
 * See usb_msc.h for the public contract. We use Arduino-ESP32's USBMSC
 * class (a thin wrapper over arduino_tinyusb) with SD_FS.readRAW /
 * writeRAW as the raw-sector backing store. Compared to re-initializing
 * the IDF SDMMC driver ourselves this has two wins:
 *
 *   1. It coexists cleanly with Arduino's own TinyUSB bundle - no
 *      duplicate-symbol linkage with esp_tinyusb.
 *   2. The SD card stays mounted through Arduino SD / SD_MMC, so there's
 *      no unmount/remount dance and existing SD state (open file
 *      handles, seek positions) is preserved.
 *
 * Platform notes:
 *   - Waveshare (ARDUINO_USB_MODE=0): USB-OTG is otherwise idle during
 *     pre-boot; USB.begin() enumerates us as an MSC-only device.
 *   - Tab5 (ARDUINO_USB_MODE=1): the port is HWCDC/upload at boot, not
 *     USB-OTG, so Arduino's USBMSC isn't available. UsbMsc_IsSupported()
 *     returns false and the boot GUI hides the button.
 */

#include <Arduino.h>
#include "usb_msc.h"
#include "board_sd.h"
#include "board_config.h"

#include "sdkconfig.h"

// Arduino USBMSC is only compiled in OTG mode + when MSC is enabled via
// the Kconfig option we set in sdkconfig.{tab5,waveshare}.
#if defined(SOC_USB_OTG_SUPPORTED) && CONFIG_TINYUSB_MSC_ENABLED && \
    !ARDUINO_USB_MODE
#define USB_MSC_AVAILABLE 1
#else
#define USB_MSC_AVAILABLE 0
#endif

#if USB_MSC_AVAILABLE

#include <USB.h>
#include <USBMSC.h>

static USBMSC            s_msc;
static volatile bool     s_exit_requested = false;
static volatile bool     s_host_mounted   = false;
static bool              s_msc_up         = false;
static bool              s_usb_started    = false;

/* ------------------------------------------------------------------------- */
/* MSC <-> SD read/write callbacks                                           */
/* ------------------------------------------------------------------------- */

static int32_t msc_on_read(uint32_t lba, uint32_t offset, void *buffer,
                           uint32_t bufsize)
{
    // SD_FS's readRAW works in whole sectors at LBA granularity. The
    // offset argument is always 0 in our usage because bufsize is always
    // a multiple of sectorSize; the Arduino example pattern handles that
    // by iterating sector-at-a-time which also matches bufsize=N*secsz.
    (void)offset;
    uint32_t sec_size = SD_FS.sectorSize();
    if (!sec_size) return -1;
    uint32_t nsec = bufsize / sec_size;
    uint8_t *out  = static_cast<uint8_t *>(buffer);
    for (uint32_t i = 0; i < nsec; ++i) {
        if (!SD_FS.readRAW(out + i * sec_size, lba + i)) {
            return -1;
        }
    }
    return nsec * sec_size;
}

static int32_t msc_on_write(uint32_t lba, uint32_t offset, uint8_t *buffer,
                            uint32_t bufsize)
{
    (void)offset;
    uint32_t sec_size = SD_FS.sectorSize();
    if (!sec_size) return -1;
    uint32_t nsec = bufsize / sec_size;
    for (uint32_t i = 0; i < nsec; ++i) {
        // writeRAW takes a non-const buffer; the MSC callback gives us one.
        if (!SD_FS.writeRAW(buffer + i * sec_size, lba + i)) {
            return -1;
        }
    }
    return nsec * sec_size;
}

static bool msc_on_start_stop(uint8_t /*power_condition*/, bool start,
                              bool load_eject)
{
    // A host-initiated eject signals that data has been flushed. Use that
    // as a proxy for "host disconnected the disk".
    if (load_eject && !start) {
        s_host_mounted = false;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* USB bus events - used to update host_mounted for the UI indicator.        */
/* ------------------------------------------------------------------------- */

static void usb_event_cb(void *arg, esp_event_base_t base, int32_t id,
                         void *data)
{
    (void)arg; (void)data;
    if (base != ARDUINO_USB_EVENTS) return;
    switch (id) {
        case ARDUINO_USB_STARTED_EVENT:
            s_host_mounted = true;
            Serial.println("[USB_MSC] Host attached");
            break;
        case ARDUINO_USB_STOPPED_EVENT:
            s_host_mounted = false;
            Serial.println("[USB_MSC] Host detached");
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */

extern "C" bool UsbMsc_IsSupported(void) { return true; }

extern "C" bool UsbMsc_Enter(const char **out_error_msg)
{
    Serial.println("[USB_MSC] Entering USB Disk mode...");
    s_exit_requested = false;
    s_host_mounted   = false;

    uint32_t sec_size = SD_FS.sectorSize();
    uint32_t n_sec    = SD_FS.numSectors();
    if (!sec_size || !n_sec) {
        if (out_error_msg) *out_error_msg = "SD card not available";
        return false;
    }
    Serial.printf("[USB_MSC] SD geometry: %u sectors x %u bytes\n",
                  (unsigned)n_sec, (unsigned)sec_size);

    s_msc.vendorID("M5Tab");
    s_msc.productID("Macintosh-SD");
    s_msc.productRevision("1.0");
    s_msc.onRead(msc_on_read);
    s_msc.onWrite(msc_on_write);
    s_msc.onStartStop(msc_on_start_stop);
    s_msc.mediaPresent(true);
    s_msc.isWritable(true);

    if (!s_msc.begin(n_sec, sec_size)) {
        if (out_error_msg) *out_error_msg = "USBMSC.begin() failed";
        return false;
    }
    s_msc_up = true;

    if (!s_usb_started) {
        USB.productName("Macintosh-SD");
        USB.manufacturerName("M5Tab");
        USB.onEvent(usb_event_cb);
        if (!USB.begin()) {
            if (out_error_msg) *out_error_msg = "USB.begin() failed";
            s_msc.end();
            s_msc_up = false;
            return false;
        }
        s_usb_started = true;
    }

    Serial.println("[USB_MSC] MSC device running - waiting for exit...");

    // Park until the boot GUI asks us to leave. The host OS sees the
    // microSD as a normal USB drive in the meantime.
    while (!s_exit_requested) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    Serial.println("[USB_MSC] Exit requested, detaching MSC...");

    // Detach the MSC class. Arduino's USB stays begun - USB.end() isn't
    // exposed. That's fine: when MSC is gone the host sees the disk
    // disappear and we stop responding to sector reads until the next
    // UsbMsc_Enter(). No re-mount of SD is needed because we never
    // unmounted it in the first place.
    s_msc.mediaPresent(false);
    s_msc.end();
    s_msc_up        = false;
    s_host_mounted  = false;

    Serial.println("[USB_MSC] USB Disk mode exited cleanly");
    return true;
}

extern "C" void UsbMsc_RequestExit(void)
{
    s_exit_requested = true;
}

extern "C" bool UsbMsc_HostMounted(void)
{
    return s_host_mounted;
}

#else  /* USB_MSC_AVAILABLE */

// This build target doesn't have USBMSC available (Tab5 in HWCDC mode,
// or TinyUSB MSC not enabled in sdkconfig). Stub out the entry points
// so the module still links; the boot GUI checks UsbMsc_IsSupported()
// and hides the button when false.

extern "C" bool UsbMsc_IsSupported(void)           { return false; }
extern "C" bool UsbMsc_HostMounted(void)           { return false; }
extern "C" void UsbMsc_RequestExit(void)           {}
extern "C" bool UsbMsc_Enter(const char **out_error_msg)
{
    if (out_error_msg) {
        *out_error_msg = "USB Disk mode not available in this build";
    }
    return false;
}

#endif  /* USB_MSC_AVAILABLE */
