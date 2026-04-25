/*
 *  xpram_esp32.cpp - XPRAM handling for ESP32
 *
 *  BasiliskII ESP32 Port
 */

#include "sysdeps.h"
#include "xpram.h"
#include "sys.h"

#include "board_sd.h"  /* SD_FS alias */

#define DEBUG 1
#include "debug.h"

// XPRAM file on SD card
const char XPRAM_FILE_PATH[] = "/BasiliskII_XPRAM";

// Shadow copy of the last XPRAM state we flushed to disk. Used by SaveXPRAM()
// to short-circuit no-op writes so the periodic flush in basilisk_loop() does
// not hammer the SD card. Initialized to a "known-different" sentinel so the
// first SaveXPRAM() always writes (we don't know what was on disk before).
static uint8 xpram_shadow[XPRAM_SIZE];
static bool  xpram_shadow_valid = false;

/*
 *  Load XPRAM from SD card
 */
void LoadXPRAM(const char *vmdir)
{
    UNUSED(vmdir);

    Serial.println("[XPRAM] Loading XPRAM...");

    // Check if XPRAM is allocated
    if (XPRAM == NULL) {
        Serial.println("[XPRAM] ERROR: XPRAM not allocated");
        return;
    }

    // Clear XPRAM first
    memset(XPRAM, 0, XPRAM_SIZE);

    // Try to load from SD card
    File f = SD_FS.open(XPRAM_FILE_PATH, FILE_READ);
    if (f) {
        size_t bytes_read = f.read(XPRAM, XPRAM_SIZE);
        f.close();
        Serial.printf("[XPRAM] Loaded %d bytes from %s\n", bytes_read, XPRAM_FILE_PATH);

        // Seed the shadow with what we just loaded - SaveXPRAM() becomes a
        // no-op until Mac OS actually pokes PRAM.
        memcpy(xpram_shadow, XPRAM, XPRAM_SIZE);
        xpram_shadow_valid = true;
    } else {
        Serial.println("[XPRAM] No saved XPRAM found, using defaults");
        // No file yet: leave the shadow invalid so the first SaveXPRAM() call
        // creates the file on disk.
        xpram_shadow_valid = false;
    }
}

/*
 *  Save XPRAM to SD card
 *
 *  Safe to call frequently - if the in-RAM XPRAM matches our last-written
 *  shadow copy byte-for-byte, we skip the SD write entirely. This lets the
 *  main loop call us on a short cadence so PRAM changes are durable even
 *  without a clean shutdown.
 */
void SaveXPRAM(void)
{
    if (XPRAM == NULL) {
        Serial.println("[XPRAM] ERROR: XPRAM not allocated");
        return;
    }

    // Fast path: nothing changed since the last successful write.
    if (xpram_shadow_valid && memcmp(XPRAM, xpram_shadow, XPRAM_SIZE) == 0) {
        return;
    }

    Serial.println("[XPRAM] Saving XPRAM (changed)...");

    File f = SD_FS.open(XPRAM_FILE_PATH, FILE_WRITE);
    if (!f) {
        Serial.printf("[XPRAM] ERROR: Cannot write to %s\n", XPRAM_FILE_PATH);
        return;
    }

    size_t bytes_written = f.write(XPRAM, XPRAM_SIZE);
    f.close();
    Serial.printf("[XPRAM] Saved %d bytes to %s\n", bytes_written, XPRAM_FILE_PATH);

    if (bytes_written == XPRAM_SIZE) {
        memcpy(xpram_shadow, XPRAM, XPRAM_SIZE);
        xpram_shadow_valid = true;
    }

    // A PRAM change is a strong "the user just told the Mac to remember
    // something" signal (Startup Disk, Sound volume, etc.). Take the
    // opportunity to push any other dirty disk-image writes out too so
    // a power pull immediately after will not lose the visible change.
    Sys_flush_now();
}

/*
 *  Delete XPRAM file
 */
void ZapPRAM(void)
{
    Serial.println("[XPRAM] Zapping PRAM...");

    if (XPRAM != NULL) {
        memset(XPRAM, 0, XPRAM_SIZE);
    }
    SD_FS.remove(XPRAM_FILE_PATH);

    // Reset shadow so the next SaveXPRAM() writes unconditionally.
    memset(xpram_shadow, 0, XPRAM_SIZE);
    xpram_shadow_valid = false;
}
