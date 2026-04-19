/*
 * board_wifi.h - WiFi transport bring-up
 *
 * Both boards ship an ESP32-C6 co-processor and expose its WiFi to the P4
 * over SDIO. Tab5 uses the Arduino WiFi.setPins() path with hard-coded pins.
 * Waveshare uses esp_wifi_remote / esp_hosted which auto-configures SDIO
 * based on sdkconfig, so no explicit pin setup is needed.
 *
 * Call BoardWifi_Prepare() exactly once, before any WiFi.begin() or
 * equivalent call. Both boards still use the Arduino WiFi API on top of
 * this transport, so boot_gui.cpp's scan/connect flow works unchanged.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the WiFi transport pins and power on the co-processor.
 *        Must be called before WiFi.begin(). Safe to call once per boot.
 */
void BoardWifi_Prepare(void);

#ifdef __cplusplus
}
#endif
