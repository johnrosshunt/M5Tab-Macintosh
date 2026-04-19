/*
 * sdkconfig_bsp_shim.h
 *
 * Defaults for every CONFIG_BSP_* symbol that the Waveshare BSP references.
 * The upstream BSP normally gets these values from its own Kconfig.projbuild
 * (processed by the ESP-IDF Component Manager). When the BSP is vendored as
 * a PlatformIO Arduino library the Kconfig is not processed, so we provide
 * the same defaults inline. Values here match the Kconfig defaults we set
 * in sdkconfig.waveshare.
 *
 * Include this header AFTER <sdkconfig.h> so that anything defined upstream
 * takes precedence.
 */
#pragma once

/* I2C */
#ifndef CONFIG_BSP_I2C_NUM
#define CONFIG_BSP_I2C_NUM 1
#endif
#ifndef CONFIG_BSP_I2C_CLK_SPEED_HZ
#define CONFIG_BSP_I2C_CLK_SPEED_HZ 400000
#endif

/* I2S */
#ifndef CONFIG_BSP_I2S_NUM
#define CONFIG_BSP_I2S_NUM 1
#endif

/* Display */
#ifndef CONFIG_BSP_LCD_DPI_BUFFER_NUMS
#define CONFIG_BSP_LCD_DPI_BUFFER_NUMS 3
#endif
#ifndef CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH
#define CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH 1
#endif

/* LCD type - default to 10.1 inch if nothing else is selected */
#if !defined(CONFIG_BSP_LCD_TYPE_800_1280_10_1_INCH) && \
    !defined(CONFIG_BSP_LCD_TYPE_800_1280_8_INCH) && \
    !defined(CONFIG_BSP_LCD_TYPE_720_1280_7_INCH)
#define CONFIG_BSP_LCD_TYPE_800_1280_10_1_INCH 1
#endif

/* LCD color format - default to RGB565 */
#if !defined(CONFIG_BSP_LCD_COLOR_FORMAT_RGB565) && \
    !defined(CONFIG_BSP_LCD_COLOR_FORMAT_RGB888)
#define CONFIG_BSP_LCD_COLOR_FORMAT_RGB565 1
#endif

/* SD card */
#ifndef CONFIG_BSP_SD_MOUNT_POINT
#define CONFIG_BSP_SD_MOUNT_POINT "/sd"
#endif

/* SPIFFS - defaults not used by our app but BSP references them */
#ifndef CONFIG_BSP_SPIFFS_MOUNT_POINT
#define CONFIG_BSP_SPIFFS_MOUNT_POINT "/spiffs"
#endif
#ifndef CONFIG_BSP_SPIFFS_PARTITION_LABEL
#define CONFIG_BSP_SPIFFS_PARTITION_LABEL "storage"
#endif
#ifndef CONFIG_BSP_SPIFFS_MAX_FILES
#define CONFIG_BSP_SPIFFS_MAX_FILES 5
#endif

/* BSP_CONFIG_NO_GRAPHIC_LIB is set via the library's build flags */

/* BSP_ERROR_CHECK behaviour - leave CONFIG_BSP_ERROR_CHECK undefined so the
 * non-assert "return err_rc_" path is taken. The upstream assert path relies
 * on CONFIG_COMPILER_OPTIMIZATION_ASSERT settings that vary between builds. */
