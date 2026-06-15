# Hardware — Waveshare ESP32-S3-Touch-LCD-1.85B

> Source of truth: `main/boards/waveshare/esp32-s3-touch-lcd-1.85b/config.h` + the official Waveshare wiki (mirrored in `doc/WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B.md`).
> Always cross-check with `config.h` if the wiki and code disagree.

## SoC & memory

| Item | Value |
|---|---|
| Chip | ESP32-S3R8 (dual Xtensa LX7 @ 240 MHz) |
| Internal SRAM | 512 KB |
| PSRAM | **8 MB** (octal) |
| Flash | **16 MB** (quad) |
| Display rotation | only `MIRROR_X/Y=false`, `SWAP_XY=false` work — already corrected |

## QSPI LCD (ST77916, 360x360)

| Signal | GPIO | Notes |
|---|---|---|
| PCLK | 40 | QSPI clock |
| CS   | 21 | |
| DATA0 | 46 | |
| DATA1 | 45 | |
| DATA2 | 42 | |
| DATA3 | 41 | |
| RST   | 3  | **must be a real GPIO**, never `GPIO_NUM_NC` (causes black screen) |
| BL    | 5  | PWM backlight |

Bus config macro: `TAIJIPI_ST77916_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_trans_sz)`.

## I2C bus (shared SCL/SDA, 7-bit addresses)

SCL = GPIO10, SDA = GPIO11, port = `I2C_NUM_0` (shared with LCD touch path).

| Device | Address | Driver / use |
|---|---|---|
| CST816S touch | 0x15 | capacitive touch, RST=GPIO1, INT=GPIO4 |
| QMI8658 IMU | 0x6B | 6-axis accel+gyro |
| ES8311 codec | 0x18 (audio) | playback, I2S to GPIO47/48/38/2/9 |
| ES7210 AEC | 0x20 (audio) | dual-mic + echo cancel |
| BQ27220 fuel gauge | 0x55 | battery |
| PCF85363 RTC | 0x51 | real-time clock, INT=GPIO6 |
| **No TCA9554** on the 1.85B variant | — | do **not** call the IO-expander init from sibling BSPs |

> Avoid external I2C devices at 0x15 / 0x6B / 0x18 / 0x20 / 0x51 / 0x55.

## Audio (I2S)

| Role | Pins |
|---|---|
| MCLK | GPIO2 |
| BCLK | GPIO48 |
| LRCK | GPIO38 |
| DOUT (speaker data) | GPIO47 |
| DIN (mic data) | GPIO39 |
| PA enable | GPIO9 |
| MIC WS | GPIO15 (note: shared with SDMMC CLK in some boards — check before reusing) |

## SDMMC (SD card)

CLK=15, CMD=14, D0=16, D1=17, D2=12, D3=13. **Unused on the compass UI** but reserved.

## USB & UART

- USB-C: USB_N=GPIO19, USB_P=GPIO20 (native USB, used for download + USB-Serial/JTAG log).
- UART0 header: U0TXD=GPIO43, U0RXD=GPIO44.

## Buttons

- BOOT: GPIO0 (strapping pin, do not reuse)
- PWR: **GPIO7** (NOT GPIO6 — that one is `RTC_INT`; the initial code used 6 and crashed)
- PWR control: GPIO6 (`PWR_Control_PIN`)

## Power

- 3.7 V LiPo on MX1.25 connector. BQ27220 reports capacity.

## Known wiring pitfalls (from `doc/TROUBLESHOOTING.md`)

1. `QSPI_PIN_NUM_LCD_RST = GPIO_NUM_NC` → black screen. Use GPIO3.
2. Touch on `I2C_NUM_1` / wrong SDA/SCL → touch dead, sometimes blocks display init.
3. `PWR_BUTTON_GPIO = GPIO_NUM_6` collides with `RTC_INT` → random reboots. Use GPIO7.
4. Initialising TCA9554 (not present) → boot loop. The 1.85B BSP has the init stubbed.
5. Python env mismatch (`python` vs `python3`) → `idf.py fullclean && source export.sh` then `python3 $IDF_PATH/tools/idf.py build`.
6. `/dev/cu.usbmodem*` permission denied → `sudo chmod 777 /dev/cu.usbmodem101` (macOS) or `usermod -aG dialout` (Linux).
