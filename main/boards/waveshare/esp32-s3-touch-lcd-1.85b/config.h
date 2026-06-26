#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/spi_master.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// ES8311(播放) + ES7210(双麦)，见 doc/WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B.md
#define AUDIO_I2S_GPIO_MCLK         GPIO_NUM_2
#define AUDIO_I2S_GPIO_BCLK         GPIO_NUM_48
#define AUDIO_I2S_GPIO_WS           GPIO_NUM_38
#define AUDIO_I2S_GPIO_DIN          GPIO_NUM_39
#define AUDIO_I2S_GPIO_DOUT         GPIO_NUM_47
#define AUDIO_CODEC_PA_PIN          GPIO_NUM_9
// 2026-06-25: 禁用参考输入。
// 原配置 true → AFE 输入 "MR" (1 mic + 1 ref)，但 ES7210 TDM 4 槽中第 2 槽实际是 MIC2 数据，
// 造成 AEC 把 MIC2 当作"回声"消除掉麦克风数据，导致唤醒词无法识别。
// 改为 false → AFE 输入 "M" (仅 1 mic)，跳过 AEC，唤醒词应能正常检测。
#define AUDIO_INPUT_REFERENCE       false
#define AUDIO_CODEC_ES8311_ADDR     ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR     ES7210_CODEC_DEFAULT_ADDR

#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define PWR_BUTTON_GPIO         GPIO_NUM_7
#define PWR_Control_PIN         GPIO_NUM_6

#define I2C_SCL_IO          GPIO_NUM_10       
#define I2C_SDA_IO          GPIO_NUM_11        

#define I2C_ADDRESS         ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000

#define DISPLAY_WIDTH       360
#define DISPLAY_HEIGHT      360
#define DISPLAY_MIRROR_X    false
#define DISPLAY_MIRROR_Y    false
#define DISPLAY_SWAP_XY     false

#define QSPI_LCD_H_RES           (360)
#define QSPI_LCD_V_RES           (360)
#define QSPI_LCD_BIT_PER_PIXEL   (16)

#define QSPI_LCD_HOST           SPI2_HOST
#define QSPI_PIN_NUM_LCD_PCLK   GPIO_NUM_40
#define QSPI_PIN_NUM_LCD_CS     GPIO_NUM_21
#define QSPI_PIN_NUM_LCD_DATA0  GPIO_NUM_46
#define QSPI_PIN_NUM_LCD_DATA1  GPIO_NUM_45
#define QSPI_PIN_NUM_LCD_DATA2  GPIO_NUM_42
#define QSPI_PIN_NUM_LCD_DATA3  GPIO_NUM_41
#define QSPI_PIN_NUM_LCD_RST    GPIO_NUM_3
#define QSPI_PIN_NUM_LCD_BL     GPIO_NUM_5

#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

#define TP_PORT          (I2C_NUM_0)
#define TP_PIN_NUM_SDA   (GPIO_NUM_11)
#define TP_PIN_NUM_SCL   (GPIO_NUM_10)
#define TP_PIN_NUM_RST   (GPIO_NUM_1)
#define TP_PIN_NUM_INT   (GPIO_NUM_4)

#define DISPLAY_BACKLIGHT_PIN           QSPI_PIN_NUM_LCD_BL
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// Micro SD (SDMMC 4-bit) - 见 doc/WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B.md
#define SD_MMC_CLK_PIN       GPIO_NUM_15
#define SD_MMC_CMD_PIN       GPIO_NUM_14
#define SD_MMC_D0_PIN        GPIO_NUM_16
#define SD_MMC_D1_PIN        GPIO_NUM_17
#define SD_MMC_D2_PIN        GPIO_NUM_12
#define SD_MMC_D3_PIN        GPIO_NUM_13
#define SD_MMC_BUS_WIDTH     4
#define SD_MMC_MOUNT_POINT   "/sdcard"

#define TAIJIPI_ST77916_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_trans_sz) \
    {                                                                             \
        .data0_io_num = d0,                                                       \
        .data1_io_num = d1,                                                       \
        .sclk_io_num = sclk,                                                      \
        .data2_io_num = d2,                                                       \
        .data3_io_num = d3,                                                       \
        .max_transfer_sz = max_trans_sz,                                          \
    }


#endif // _BOARD_CONFIG_H_
