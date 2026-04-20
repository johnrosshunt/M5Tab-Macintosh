/*
 * board_wifi_firmware_tab5.cpp - No-op co-processor OTA on Tab5.
 *
 * The Tab5 build uses arduino-esp32's native WiFi (Arduino WiFi.setPins
 * path) rather than esp_wifi_remote, so there's no separate slave
 * firmware to OTA. The symbol is provided so call sites in shared code
 * (boot_gui.cpp) link cleanly on both boards.
 */

#include "board_wifi.h"

extern "C" bool BoardWifi_IsCoprocessorFirmwareOutdated(void)
{
    return false;
}

extern "C" BoardWifiFirmwareResult BoardWifi_ApplyCoprocessorFirmware(void)
{
    return BOARD_WIFI_FW_NOT_SUPPORTED;
}
