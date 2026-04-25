/*
 * hid_descriptor.h - Minimal HID report-descriptor parser for mouse devices.
 *
 * Walks a HID report descriptor blob (as returned by GET_DESCRIPTOR(REPORT))
 * and extracts the bit offsets / sizes / signedness of the X, Y, Wheel and
 * Button fields, plus the Report ID prefix if the device uses one. The
 * parser intentionally only models the items needed to decode pointing-
 * device reports - it is not a full HID parser.
 *
 * Designed for use from input_esp32.cpp on the ESP32-P4 USB host stack.
 * On parse failure, callers should fall back to the heuristic byte-layout
 * decoder so vintage boot-mode mice keep working.
 */
#ifndef BASILISK_HID_DESCRIPTOR_H
#define BASILISK_HID_DESCRIPTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Whether the parser found enough fields to decode movement. We
     * require at least an X *and* a Y field; any device without both
     * is flagged as not-a-mouse (parser returns valid=false and the
     * dispatcher falls back to its boot-protocol heuristics). */
    bool     valid;

    /* Report ID prefix. 0 means the device sends raw reports with no
     * 1-byte Report ID prefix; non-zero means the first byte of each
     * report is the Report ID and our other offsets are measured from
     * the byte *after* it (i.e. they already account for the prefix). */
    uint8_t  report_id;

    /* Total report length in bytes including the Report ID prefix. */
    uint16_t report_bytes;

    /* Buttons. Up to 8 sequential button bits starting at button_offset. */
    bool     has_buttons;
    uint16_t button_offset_bits;   /* bit offset from start of report */
    uint8_t  button_count;         /* number of button bits, 1..8 */

    /* X axis. Always present when valid==true. */
    uint16_t x_offset_bits;
    uint8_t  x_size_bits;          /* typically 8, 12, or 16 */
    bool     x_signed;             /* true if logical_min < 0 */

    /* Y axis. Always present when valid==true. */
    uint16_t y_offset_bits;
    uint8_t  y_size_bits;
    bool     y_signed;

    /* Wheel. Optional. */
    bool     has_wheel;
    uint16_t wheel_offset_bits;
    uint8_t  wheel_size_bits;
    bool     wheel_signed;
} HidMouseLayout;

/*
 * Parse a HID report descriptor.
 *
 *   desc/len: raw descriptor bytes from GET_DESCRIPTOR(REPORT).
 *   out:      output layout. Always written; on failure, valid=false
 *             and the rest of the fields are zeroed.
 *
 * Returns true if a usable mouse layout (X *and* Y at minimum) was
 * found. Passes through silently when given a non-mouse descriptor.
 */
bool HidParseMouseDescriptor(const uint8_t *desc, size_t len,
                             HidMouseLayout *out);

/*
 * Decode a single HID input report using a parsed layout.
 *
 *   data/len:  bytes of the input report as received from the
 *              interrupt-IN endpoint (including the Report ID prefix
 *              if the device uses one).
 *   layout:    the parsed mouse layout for this device.
 *   buttons:   output bitmask, bit i = button i+1 pressed.
 *   dx/dy/dw:  signed deltas (movement and wheel).
 *
 * Returns false if the report is too short, has the wrong Report ID
 * for this layout, or layout->valid is false. On false return, the
 * output values are unchanged so the caller can keep its prior state.
 */
bool HidDecodeMouseReport(const uint8_t *data, size_t len,
                          const HidMouseLayout *layout,
                          uint8_t *buttons,
                          int16_t *dx, int16_t *dy, int16_t *dw);

#ifdef __cplusplus
}
#endif

#endif /* BASILISK_HID_DESCRIPTOR_H */
