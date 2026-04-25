/*
 * mac_splash.cpp - see mac_splash.h.
 */

#include <Arduino.h>

#include "mac_splash.h"
#include "board.h"
#include "board_display.h"
#include "boot_gui.h"            /* touch task + BootGUITouch event */
#include "chicago_font.h"

/* Generated asset data (scripts/build_assets.py). The header names here
 * match what the script emits into src/generated/. */
#include "asset_bg_tile.h"       /* BG_TILE_W/H/PIXELS                     */
#include "asset_happy_mac.h"     /* HAPPY_MAC_W/H/PIXELS/MASK              */

namespace {

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kMacDesktopGray = 0xC618;  /* 0xC0C0C0 in RGB565 */

const uint8_t *happy_mac_mask_ptr()
{
#if HAPPY_MAC_HAS_MASK
    return HAPPY_MAC_MASK;
#else
    return nullptr;
#endif
}

int happy_mac_mask_row_bytes()
{
#if HAPPY_MAC_HAS_MASK
    return HAPPY_MAC_MASK_ROW_BYTES;
#else
    return 0;
#endif
}

void paint_splash()
{
    /* No explicit fillScreen: Gfx_TileBackground paints the entire
     * screen with the desktop stipple (it iterates x=0..screen_w,
     * y=0..screen_h), so a prior black fill is pure waste and, with
     * the Tab5's externally-mapped DPI framebuffer, shows up as a
     * visible black flash before the checkerboard draws in.          */
    Gfx_TileBackground(BG_TILE_PIXELS, BG_TILE_W, BG_TILE_H, /*scale=*/2);
    Gfx_DrawIconCentered(HAPPY_MAC_PIXELS, HAPPY_MAC_W, HAPPY_MAC_H,
                         happy_mac_mask_ptr(), happy_mac_mask_row_bytes(),
                         /*scale=*/2);
    BoardDisplay_Present();
}

void drain_touch_edges()
{
    /* Consume any buffered edges so the splash's leftover press/release
     * doesn't instantly trigger the settings screen's own handlers. The
     * touch task overwrites the queue at ~60 Hz, so a couple of short
     * delays between polls is enough to see the cleared state land. */
    BootGUITouch t;
    for (int i = 0; i < 4; ++i) {
        if (!BootGUI_PollTouch(&t)) break;
        if (!t.was_pressed && !t.was_released) break;
        delay(20);  /* allow the 60 Hz touch task to refresh the queue */
    }
}

} // namespace

namespace MacSplash {

void Begin()
{
    /* Paint the splash FIRST so the touch-warmup happens while the
     * user is looking at the Happy Mac, not at a plain black screen. */
    Serial.printf("[MAC_SPLASH] Painting splash (%dx%d)\n",
                  BoardDisplay_Width(), BoardDisplay_Height());
    paint_splash();

    /* Start the touch task BEFORE the warmup so any taps the user
     * lands while the Happy Mac is on screen are actually captured.
     * Previously the warmup ran first as a synchronous Board_Update
     * loop, which meant ~1 s of splash-visible time during which the
     * tap-detection path was completely deaf - users reasonably
     * tapping the splash had a window of just SPLASH_TAP_WINDOW_MS
     * minus reaction time and ended up booting straight to Mac OS.
     * The touch task itself calls Board_Update() every poll, so it
     * naturally warms up the GT911 once it starts running. */
    if (!BootGUI_StartTouchTask()) {
        Serial.println("[MAC_SPLASH] WARNING: failed to start touch task");
    }

    /* Brief delay so the GT911 has time to produce its first valid
     * report before WaitForTapOrTimeout consumes events. The touch
     * task's own ~16 ms poll cadence handles most of the warm-up;
     * we just need a short head start. */
    Serial.println("[MAC_SPLASH] Touch task primed");
    delay(150);
}

bool WaitForTapOrTimeout(uint32_t ms)
{
    uint32_t start = millis();
    BootGUITouch t;

    /* Discard any spurious edge flagged before the window opened. */
    drain_touch_edges();

    bool tapped = false;
    while (millis() - start < ms) {
        if (BootGUI_PollTouch(&t)) {
            if (t.was_released) {
                Serial.printf("[MAC_SPLASH] Tap at (%d,%d) - opening settings\n",
                              t.x, t.y);
                tapped = true;
                break;
            }
        }
        delay(8);  /* ~120 Hz poll, plenty for human tap reaction */
    }

    /* Swallow any remaining edge so the settings UI starts fresh. */
    drain_touch_edges();
    return tapped;
}

void TransitionToEmulator()
{
    /* Keep the classic checkerboard + Happy Mac on screen right up to
     * the moment the emulator takes over. The settings screen, if it
     * ran, will have overwritten the splash - so repaint it here so
     * the last thing the user sees before the Mac OS boot is the same
     * checkerboard they saw at power-on. The emulator's VideoInit
     * clears to its own color on first frame, so we don't need a
     * separate blank step. */
    paint_splash();
}

void ShowErrorOverlay(const char *msg)
{
    if (!msg) msg = "Unknown error";

    auto &gfx = BoardDisplay_Gfx();
    int screen_w = BoardDisplay_Width();
    int screen_h = BoardDisplay_Height();

    /* Dim the background to a solid dark gray rather than redrawing the
     * splash, so the error is clearly different from the boot state. */
    gfx.fillScreen(0x4208);  /* ~25% gray */

    /* Centered panel with a white Chicago error message. Chicago renders
     * at its native 23-px pixel-perfect size; we stack lines at
     * line-height intervals instead of scaling up. */
    int line_h  = Chicago_LineHeight();
    int panel_w = screen_w * 3 / 4;
    int panel_h = line_h * 6;
    int panel_x = (screen_w - panel_w) / 2;
    int panel_y = (screen_h - panel_h) / 2;

    gfx.fillRect(panel_x, panel_y, panel_w, panel_h, kBlack);
    gfx.drawRect(panel_x, panel_y, panel_w, panel_h, kWhite);
    gfx.drawRect(panel_x + 2, panel_y + 2, panel_w - 4, panel_h - 4, kWhite);

    int header_y = panel_y + panel_h / 3;
    int msg_y    = panel_y + panel_h * 2 / 3;

    Chicago_DrawString("ERROR",
                       screen_w / 2, header_y,
                       kWhite,
                       /*datum=*/MC_DATUM);
    Chicago_DrawString(msg,
                       screen_w / 2, msg_y,
                       kWhite,
                       /*datum=*/MC_DATUM);

    BoardDisplay_Present();
}

void ShowSafeToPowerOff()
{
    auto &gfx = BoardDisplay_Gfx();
    int screen_w = BoardDisplay_Width();
    int screen_h = BoardDisplay_Height();
    int cx = screen_w / 2;
    int cy = screen_h / 2;
    int lh = Chicago_LineHeight();

    gfx.fillScreen(kBlack);
    Chicago_DrawString("It is now safe to switch off",
                       cx, cy - lh, kWhite, MC_DATUM);
    Chicago_DrawString("your Macintosh.",
                       cx, cy + 4, kWhite, MC_DATUM);
    BoardDisplay_Present();
}

} // namespace MacSplash
