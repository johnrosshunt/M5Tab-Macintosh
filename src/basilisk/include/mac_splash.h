/*
 * mac_splash.h - Classic-Mac themed pre-boot splash.
 *
 * This replaces the old BasiliskII text startup screen and the 3-second
 * "Change Settings" countdown inside boot_gui. The splash is the very
 * first visual once BoardDisplay_Init() returns and stays up until we
 * transition into the emulator. Tapping anywhere in the first 2 seconds
 * signals that the user wants the settings UI; otherwise the splash
 * hands off silently.
 *
 * Drawing uses the generated asset headers and Chicago font emitted by
 * scripts/build_assets.py.
 *
 * Touch polling reuses the BootGUI touch task so we don't stand up a
 * second FreeRTOS task just for the splash.
 */
#pragma once

#include <stdint.h>

namespace MacSplash {

/**
 * @brief Warm up the touch panel, start the shared touch task, and paint
 *        the splash (tiled BgTile background + centered Happy Mac).
 *
 * Safe to call once, after BoardDisplay_Init().
 */
void Begin();

/**
 * @brief Block up to `ms` milliseconds and return true if the user
 *        tapped-and-released the screen during that window.
 *
 * Any consumed press/release is cleared from the touch queue so it
 * doesn't get reinterpreted by the settings screen that follows.
 */
bool WaitForTapOrTimeout(uint32_t ms);

/**
 * @brief Smooth clear sequence handing off to the emulator: flash white,
 *        then black, then return. The emulator's VideoInit paints its own
 *        startup colour on top of whatever's on the panel, so all we need
 *        is a clean transition with no leftover splash artifacts.
 */
void TransitionToEmulator();

/**
 * @brief Paint a simple error overlay (Chicago text on a dark dimmed
 *        background) on top of whatever's currently on the splash. Used
 *        by main.cpp for the pre-SD / pre-boot-GUI fatal errors.
 */
void ShowErrorOverlay(const char *msg);

/**
 * @brief Paint the classic "It is now safe to switch off your computer"
 *        screen: pure black fill with centered white Chicago. Called
 *        after the emulator cleanly exits so the user isn't left with
 *        the stale last Mac framebuffer.
 */
void ShowSafeToPowerOff();

} // namespace MacSplash
