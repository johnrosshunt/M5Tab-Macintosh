#pragma once

/**************************************************************************************************
 * BSP configuration
 **************************************************************************************************/
// By default, this BSP is shipped with LVGL graphical library. Enabling this option will exclude it.
// If you want to use BSP without LVGL, select BSP version with 'noglib' suffix.
#if !defined(BSP_CONFIG_NO_GRAPHIC_LIB) // Check if the symbol is not coming from compiler definitions (-D...)
#define BSP_CONFIG_NO_GRAPHIC_LIB (0)
#endif

/* When the BSP is vendored under PlatformIO/Arduino the BSP's own
 * Kconfig.projbuild is not processed, so CONFIG_BSP_* symbols would otherwise
 * be undefined. Provide defaults here that match sdkconfig.waveshare. The
 * #ifndef guards let -D flags from platformio.ini or a real sdkconfig.h win.
 */

#ifndef CONFIG_BSP_I2C_NUM
#define CONFIG_BSP_I2C_NUM 1
#endif

#ifndef CONFIG_BSP_I2C_CLK_SPEED_HZ
#define CONFIG_BSP_I2C_CLK_SPEED_HZ 400000
#endif

#ifndef CONFIG_BSP_I2S_NUM
#define CONFIG_BSP_I2S_NUM 1
#endif

#ifndef CONFIG_BSP_LCD_DPI_BUFFER_NUMS
#define CONFIG_BSP_LCD_DPI_BUFFER_NUMS 3
#endif

#ifndef CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH
#define CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH 1
#endif

#if !defined(CONFIG_BSP_LCD_TYPE_800_1280_10_1_INCH) && \
    !defined(CONFIG_BSP_LCD_TYPE_800_1280_8_INCH) && \
    !defined(CONFIG_BSP_LCD_TYPE_720_1280_7_INCH)
#define CONFIG_BSP_LCD_TYPE_800_1280_10_1_INCH 1
#endif

#if !defined(CONFIG_BSP_LCD_COLOR_FORMAT_RGB565) && \
    !defined(CONFIG_BSP_LCD_COLOR_FORMAT_RGB888)
#define CONFIG_BSP_LCD_COLOR_FORMAT_RGB565 1
#endif

#ifndef CONFIG_BSP_SD_MOUNT_POINT
#define CONFIG_BSP_SD_MOUNT_POINT "/sd"
#endif

#ifndef CONFIG_BSP_SPIFFS_MOUNT_POINT
#define CONFIG_BSP_SPIFFS_MOUNT_POINT "/spiffs"
#endif

#ifndef CONFIG_BSP_SPIFFS_PARTITION_LABEL
#define CONFIG_BSP_SPIFFS_PARTITION_LABEL "storage"
#endif

#ifndef CONFIG_BSP_SPIFFS_MAX_FILES
#define CONFIG_BSP_SPIFFS_MAX_FILES 5
#endif
