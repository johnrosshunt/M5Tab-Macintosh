/*
 *  sys_esp32.cpp - System dependent routines for ESP32 (SD card I/O)
 *
 *  BasiliskII ESP32 Port
 *
 *  Direct I/O with deferred flush. Each Sys_write only marks the handle
 *  as dirty; basilisk_loop() flushes periodically (DISK_FLUSH_INTERVAL)
 *  and on idle (Sys_last_write_ms + idle window). The shutdown hook in
 *  src/main.cpp calls Sys_flush_now() so ESP-IDF panic / reboot still
 *  drains the buffer to the card.
 */

#include "sysdeps.h"
#include "main.h"
#include "macos_util.h"
#include "prefs.h"
#include "sys.h"

#include "board_sd.h"  /* SD_FS alias (SD on Tab5, SD_MMC on Waveshare) */
#include <FS.h>
#include <Arduino.h>

#define DEBUG 0
#include "debug.h"

// File handle structure - minimal with dirty tracking
struct file_handle {
    File file;
    bool is_open;
    bool read_only;
    bool is_floppy;
    bool is_cdrom;
    bool is_dirty;      // Track if there are pending writes to flush
    bool pos_valid;     // Track cached file position to avoid redundant seek()
    loff_t pos;         // Current file position when pos_valid is true
    loff_t size;
    char path[256];
};

// Static flag for SD initialization
static bool sd_initialized = false;

// Open file handles for periodic flush
static file_handle *open_file_handles[16] = {NULL};

// Track when the most recent dirty-marking write happened so the main
// loop's idle-flush path can issue a flush ~500 ms after activity ends.
// Volatile because it can be read from basilisk_loop while Sys_write
// runs on the same core but in a different RTOS context.
static volatile uint32_t s_last_write_ms = 0;

/*
 *  Initialize SD card
 */
static bool init_sd_card(void)
{
    if (sd_initialized) {
        return true;
    }
    
    Serial.println("[SYS] SD card should already be initialized by main.cpp");
    sd_initialized = true;
    
    return true;
}

/*
 *  Register an open file handle
 */
static void register_file_handle(file_handle *fh)
{
    for (int i = 0; i < 16; i++) {
        if (open_file_handles[i] == NULL) {
            open_file_handles[i] = fh;
            return;
        }
    }
}

/*
 *  Unregister a file handle
 */
static void unregister_file_handle(file_handle *fh)
{
    for (int i = 0; i < 16; i++) {
        if (open_file_handles[i] == fh) {
            open_file_handles[i] = NULL;
            return;
        }
    }
}

/*
 *  Periodic flush - ensures data is written to SD card.
 *  Called from basilisk_loop() on DISK_FLUSH_INTERVAL cadence (a few
 *  seconds) and again on the idle-flush path shortly after activity
 *  stops. Only flushes handles dirtied since the last flush.
 */
void Sys_periodic_flush(void)
{
    for (int i = 0; i < 16; i++) {
        file_handle *fh = open_file_handles[i];
        if (fh != NULL && fh->is_open && !fh->read_only && fh->is_dirty) {
            fh->file.flush();
            fh->is_dirty = false;
        }
    }
}

/*
 *  Force-flush every dirty open handle right now and log a summary.
 *  Safe to call from a shutdown handler / power-button long-press.
 */
void Sys_flush_now(void)
{
    int handles = 0;
    int dirty   = 0;
    for (int i = 0; i < 16; i++) {
        file_handle *fh = open_file_handles[i];
        if (fh == NULL || !fh->is_open || fh->read_only) {
            continue;
        }
        handles++;
        if (!fh->is_dirty) {
            continue;
        }
        dirty++;
        fh->file.flush();
        fh->is_dirty = false;
    }
    Serial.printf("[SYS] flush_now: handles=%d dirty=%d\n", handles, dirty);
}

bool Sys_has_dirty_handles(void)
{
    for (int i = 0; i < 16; i++) {
        file_handle *fh = open_file_handles[i];
        if (fh != NULL && fh->is_open && !fh->read_only && fh->is_dirty) {
            return true;
        }
    }
    return false;
}

uint32_t Sys_last_write_ms(void)
{
    return s_last_write_ms;
}

/*
 *  Initialization
 */
void SysInit(void)
{
    init_sd_card();
    Serial.println("[SYS] Direct I/O mode (no caching)");
}

/*
 *  Deinitialization
 */
void SysExit(void)
{
    // Flush all open files
    Sys_periodic_flush();
    sd_initialized = false;
}

/*
 *  Mount first floppy disk
 */
void SysAddFloppyPrefs(void)
{
}

/*
 *  Mount first hard disk
 */
void SysAddDiskPrefs(void)
{
}

/*
 *  Mount CD-ROM
 */
void SysAddCDROMPrefs(void)
{
}

/*
 *  Add serial port preferences
 */
void SysAddSerialPrefs(void)
{
}

/*
 *  Repair HFS volume - fix common corruption issues from improper shutdown
 */
static void Sys_repair_hfs_volume(const char *path)
{
    // Only repair .dsk files
    if (strstr(path, ".dsk") == NULL && strstr(path, ".DSK") == NULL) {
        return;
    }
    
    Serial.printf("[SYS] Checking HFS volume: %s\n", path);
    
    File f = SD_FS.open(path, "r+b");
    if (!f) {
        return;
    }
    
    size_t file_size = f.size();
    if (file_size < 1024 + 512) {
        f.close();
        return;
    }
    
    // Read main MDB
    uint8_t mdb[128];
    if (!f.seek(1024) || f.read(mdb, 128) != 128) {
        f.close();
        return;
    }
    
    // Check HFS signature
    uint16_t signature = (mdb[0] << 8) | mdb[1];
    if (signature != 0x4244) {
        f.close();
        return;
    }
    
    // Read key fields
    uint16_t drAtrb = (mdb[10] << 8) | mdb[11];
    uint32_t drFndrInfo2 = (mdb[100] << 24) | (mdb[101] << 16) | (mdb[102] << 8) | mdb[103];
    uint32_t drFndrInfo3 = (mdb[104] << 24) | (mdb[105] << 16) | (mdb[106] << 8) | mdb[107];
    
    // Get original drAtrb from Alternate MDB
    size_t amdb_offset = ((file_size / 512) - 2) * 512;
    uint16_t original_drAtrb = drAtrb;
    
    uint8_t amdb_sig[2];
    if (f.seek(amdb_offset) && f.read(amdb_sig, 2) == 2) {
        if ((amdb_sig[0] << 8 | amdb_sig[1]) == 0x4244) {
            uint8_t amdb_atrb[2];
            if (f.seek(amdb_offset + 10) && f.read(amdb_atrb, 2) == 2) {
                original_drAtrb = (amdb_atrb[0] << 8) | amdb_atrb[1];
            }
        }
    }
    
    bool needs_repair = false;
    
    if (drAtrb != original_drAtrb) {
        mdb[10] = (original_drAtrb >> 8) & 0xFF;
        mdb[11] = original_drAtrb & 0xFF;
        needs_repair = true;
    }
    
    if (drFndrInfo2 != 0) {
        mdb[100] = mdb[101] = mdb[102] = mdb[103] = 0;
        needs_repair = true;
    }
    
    if (drFndrInfo3 != 0) {
        mdb[104] = mdb[105] = mdb[106] = mdb[107] = 0;
        needs_repair = true;
    }
    
    if (needs_repair) {
        Serial.println("[SYS] Repairing HFS volume...");
        f.seek(1024 + 10);
        f.write(&mdb[10], 2);
        f.seek(1024 + 100);
        f.write(&mdb[100], 8);
        f.flush();
        Serial.println("[SYS] Volume repaired");
    } else {
        Serial.println("[SYS] Volume OK");
    }
    
    f.close();
}

/*
 *  Open a file/device
 */
void *Sys_open(const char *name, bool read_only, bool is_cdrom)
{
    if (!name || strlen(name) == 0) {
        return NULL;
    }
    
    // Repair HFS volume before opening
    if (!read_only && !is_cdrom) {
        Sys_repair_hfs_volume(name);
    }
    
    file_handle *fh = new file_handle;
    if (!fh) {
        return NULL;
    }
    
    memset(fh, 0, sizeof(file_handle));
    strncpy(fh->path, name, sizeof(fh->path) - 1);
    fh->is_cdrom = is_cdrom;
    fh->is_floppy = (strstr(name, ".img") != NULL || strstr(name, ".IMG") != NULL);
    
    // Determine read-only status. Anything that looks like an optical
    // image is opened read-only - .iso, .cdr (Disk Copy raw), .toast
    // (Roxio) - so games that try to write back protected sectors
    // fail with wPrErr instead of silently dirtying the host file.
    bool looks_optical = (strstr(name, ".iso")   != NULL) ||
                         (strstr(name, ".ISO")   != NULL) ||
                         (strstr(name, ".cdr")   != NULL) ||
                         (strstr(name, ".CDR")   != NULL) ||
                         (strstr(name, ".toast") != NULL) ||
                         (strstr(name, ".TOAST") != NULL);
    if (is_cdrom || looks_optical) {
        fh->read_only = true;
    } else {
        fh->read_only = read_only;
    }
    
    // Open file
    if (fh->read_only) {
        fh->file = SD_FS.open(name, FILE_READ);
    } else {
        fh->file = SD_FS.open(name, "r+b");
        if (!fh->file) {
            fh->file = SD_FS.open(name, FILE_READ);
            fh->read_only = true;
        }
    }
    
    if (!fh->file) {
        delete fh;
        return NULL;
    }
    
    fh->size = fh->file.size();
    if (fh->size == 0) {
        if (fh->file.seek(0, SeekEnd)) {
            fh->size = fh->file.position();
            fh->file.seek(0, SeekSet);
        }
    }
    
    if (fh->size == 0) {
        fh->file.close();
        delete fh;
        return NULL;
    }
    
    fh->is_open = true;
    fh->pos = 0;
    fh->pos_valid = true;
    register_file_handle(fh);
    
    Serial.printf("[SYS] Opened %s (%lld KB, ro=%d)\n", 
                  name, (long long)(fh->size / 1024), fh->read_only);
    
    return fh;
}

/*
 *  Close a file/device
 */
void Sys_close(void *arg)
{
    file_handle *fh = (file_handle *)arg;
    if (!fh) return;
    
    if (fh->is_open) {
        unregister_file_handle(fh);
        fh->file.flush();
        fh->file.close();
        fh->is_open = false;
    }
    
    delete fh;
}

/*
 *  Read from a file/device - direct read, no caching
 */
size_t Sys_read(void *arg, void *buffer, loff_t offset, size_t length)
{
    file_handle *fh = (file_handle *)arg;
    if (!fh || !fh->is_open || !buffer) {
        return 0;
    }

    if (!fh->pos_valid || fh->pos != offset) {
        if (!fh->file.seek(offset)) {
            fh->pos_valid = false;
            return 0;
        }
        fh->pos = offset;
        fh->pos_valid = true;
    }

    size_t read_len = fh->file.read((uint8_t *)buffer, length);
    if (read_len > 0) {
        fh->pos += (loff_t)read_len;
    }
    return read_len;
}

/*
 *  Write to a file/device - direct write, no buffering
 *  Marks handle dirty for deferred flush
 */
size_t Sys_write(void *arg, void *buffer, loff_t offset, size_t length)
{
    file_handle *fh = (file_handle *)arg;
    if (!fh || !fh->is_open || !buffer || fh->read_only) {
        return 0;
    }

    if (!fh->pos_valid || fh->pos != offset) {
        if (!fh->file.seek(offset)) {
            fh->pos_valid = false;
            return 0;
        }
        fh->pos = offset;
        fh->pos_valid = true;
    }

    size_t written = fh->file.write((uint8_t *)buffer, length);
    if (written > 0) {
        fh->is_dirty = true;
        fh->pos += (loff_t)written;
        s_last_write_ms = millis();
    }
    return written;
}

/*
 *  Return size of file/device
 */
loff_t SysGetFileSize(void *arg)
{
    file_handle *fh = (file_handle *)arg;
    if (!fh || !fh->is_open) {
        return 0;
    }
    return fh->size;
}

/*
 *  Eject disk (no-op)
 */
void SysEject(void *arg)
{
    UNUSED(arg);
}

/*
 *  Format disk (not supported)
 */
bool SysFormat(void *arg)
{
    UNUSED(arg);
    return false;
}

/*
 *  Check if file/device is read-only
 */
bool SysIsReadOnly(void *arg)
{
    file_handle *fh = (file_handle *)arg;
    if (!fh) return true;
    return fh->read_only;
}

/*
 *  Check if a fixed disk
 */
bool SysIsFixedDisk(void *arg)
{
    file_handle *fh = (file_handle *)arg;
    if (!fh) return true;
    return !fh->is_floppy && !fh->is_cdrom;
}

/*
 *  Check if disk is inserted
 */
bool SysIsDiskInserted(void *arg)
{
    file_handle *fh = (file_handle *)arg;
    if (!fh) return false;
    return fh->is_open;
}

void SysPreventRemoval(void *arg) { UNUSED(arg); }
void SysAllowRemoval(void *arg) { UNUSED(arg); }

/*
 *  CD-ROM stubs.
 *
 *  We don't drive real optical hardware - everything is a flat file on
 *  the SD card. But Mac OS install discs (and a few "are you a real
 *  CD?" probes in regular software) do call SysCDReadTOC and friends
 *  during startup, and previously we returned false for all of them,
 *  which caused the System 7.5/8 install discs to give up and error
 *  out instead of booting.
 *
 *  These stubs synthesise a minimum-viable single-data-track TOC and
 *  a "stopped at start of disc" audio position so the Mac is happy.
 *  We treat the file as a 2048-byte/sector ISO 9660 image when
 *  computing the lead-out MSF, which is correct for nearly every .iso
 *  in the wild and only ever overstates the run length on .cdr/.toast
 *  raw images (Mac OS clamps reads to file size anyway).
 */

static void msf_from_lba(uint32_t lba_with_pregap, uint8 &m, uint8 &s, uint8 &f)
{
    m = (uint8)(lba_with_pregap / (60u * 75u));
    s = (uint8)((lba_with_pregap / 75u) % 60u);
    f = (uint8)(lba_with_pregap % 75u);
}

bool SysCDReadTOC(void *arg, uint8 *toc)
{
    file_handle *fh = (file_handle *)arg;
    if (!fh || !fh->is_open || !toc) {
        return false;
    }

    // Wipe the whole 804-byte TOC buffer so any unused entries are
    // guaranteed to be zero (read_toc() in cdrom.cpp scans until it
    // sees the 0xAA lead-out marker).
    memset(toc, 0, 804);

    // Header: bytes 0..1 are big-endian total length, then first/last
    // track numbers. We have one data track + lead-out, so length is
    // 4 (header) + 2 entries * 8 = 20.
    toc[0] = 0;
    toc[1] = 18;       // length excluding the 2-byte length field
    toc[2] = 1;        // first track
    toc[3] = 1;        // last track

    // Track 1 entry at offset 4..11.
    //   ctrl/addr 0x14 = data track, copy permitted, ADR=1.
    toc[4 + 0] = 0;    // session
    toc[4 + 1] = 0x14; // ctrl/addr
    toc[4 + 2] = 1;    // track number
    toc[4 + 3] = 0;    // point/index
    toc[4 + 4] = 0;
    toc[4 + 5] = 0;    // M
    toc[4 + 6] = 2;    // S (standard 2-second pregap before track 1)
    toc[4 + 7] = 0;    // F

    // Compute lead-out MSF from the file size, assuming 2048-byte
    // sectors. The Mac side just uses this to know "where the disc
    // ends" so a small overestimate is harmless - reads past EOF
    // return short and the driver clamps them.
    uint32_t total_lba = 150;  // 2-second pregap at the start
    if (fh->size > 0) {
        total_lba += (uint32_t)(fh->size / 2048);
    }
    uint8 lead_m, lead_s, lead_f;
    msf_from_lba(total_lba, lead_m, lead_s, lead_f);

    // Lead-out at offset 12..19. Track number 0xAA marks lead-out.
    toc[12 + 0] = 0;
    toc[12 + 1] = 0x14;
    toc[12 + 2] = 0xAA;
    toc[12 + 3] = 0;
    toc[12 + 4] = 0;
    toc[12 + 5] = lead_m;
    toc[12 + 6] = lead_s;
    toc[12 + 7] = lead_f;

    return true;
}

bool SysCDGetPosition(void *arg, uint8 *pos)
{
    file_handle *fh = (file_handle *)arg;
    if (!fh || !fh->is_open || !pos) {
        return false;
    }

    // Synthesise a "stopped at the start of the data track" status.
    // Layout follows the existing cdrom.cpp readers (see the
    // ReadTheQSubcode / AudioStatus paths around line 920-1030):
    //   pos[1] = audio status byte (0x13 = play stopped)
    //   pos[5] = ctrl/addr (0x14 for data)
    //   pos[6] = track number
    //   pos[7] = index number
    //   pos[9..11]  = absolute M/S/F
    //   pos[13..15] = relative M/S/F
    memset(pos, 0, 16);
    pos[1]  = 0x13;
    pos[5]  = 0x14;
    pos[6]  = 1;
    pos[7]  = 1;
    pos[9]  = 0;
    pos[10] = 2;
    pos[11] = 0;
    pos[13] = 0;
    pos[14] = 0;
    pos[15] = 0;
    return true;
}

bool SysCDPlay(void *arg, uint8 start_m, uint8 start_s, uint8 start_f, uint8 end_m, uint8 end_s, uint8 end_f) {
    UNUSED(arg); UNUSED(start_m); UNUSED(start_s); UNUSED(start_f);
    UNUSED(end_m); UNUSED(end_s); UNUSED(end_f); return false;
}
bool SysCDPause(void *arg) { UNUSED(arg); return false; }
bool SysCDResume(void *arg) { UNUSED(arg); return false; }
bool SysCDStop(void *arg, uint8 lead_out_m, uint8 lead_out_s, uint8 lead_out_f) {
    UNUSED(arg); UNUSED(lead_out_m); UNUSED(lead_out_s); UNUSED(lead_out_f); return false;
}
bool SysCDScan(void *arg, uint8 start_m, uint8 start_s, uint8 start_f, bool reverse) {
    UNUSED(arg); UNUSED(start_m); UNUSED(start_s); UNUSED(start_f); UNUSED(reverse); return false;
}
void SysCDSetVolume(void *arg, uint8 left, uint8 right) { UNUSED(arg); UNUSED(left); UNUSED(right); }
void SysCDGetVolume(void *arg, uint8 &left, uint8 &right) { UNUSED(arg); left = right = 0; }
