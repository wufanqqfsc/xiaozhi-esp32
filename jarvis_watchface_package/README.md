# Dynamic Neon JARVIS Watchface

LVGL-based dynamic JARVIS HUD watchface for the Waveshare ESP32-S3 Touch LCD 1.85B board.

## What It Shows

- Full-screen dark neon watchface style UI
- Rotating scan arcs and pulsing rings
- Animated orbit dots around the center
- Large glowing `JARVIS` text in the middle
- Dynamic scan bars below the JARVIS text
- Bottom system status strip

## Hardware

- Waveshare ESP32-S3 Touch LCD 1.85B
- ESP32-S3 with 8MB PSRAM
- USB-Serial/JTAG flashing

## Build And Flash

Open an ESP-IDF PowerShell terminal, then run from this folder:

```powershell
idf.py build
idf.py -p COM5 flash
```

If your board uses a different serial port, replace `COM5`.

## Files Of Interest

- `main/main.c`: LVGL watchface UI and animation logic
- `main/CMakeLists.txt`: app component build settings
- `partitions.csv`: simple 16MB flash partition layout
- `sdkconfig`: ESP-IDF and LVGL configuration

## Notes

This package intentionally does not include build outputs. Rebuild locally with ESP-IDF before flashing.
