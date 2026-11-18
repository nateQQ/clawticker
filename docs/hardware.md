# Hardware notes

Board: **DIYMORE ES3C28P** (ESP32-S3-WROOM N16R8): 16 MB QSPI flash, 8 MB octal PSRAM, 2.8 inch 320x240 ILI9341V IPS panel, FT6336G capacitive touch. Pins are fixed on the PCB and come from the vendor manual (<https://www.lcdwiki.com/2.8inch_ESP32-S3_Display_E32C28P/E32N28P>).

| Function | GPIO |
|---|---|
| LCD CS / SCLK / MOSI / DC | 10 / 12 / 11 / 46 |
| LCD reset | tied to the chip reset |
| Backlight (PWM, via MOSFET) | 45 |
| Touch SDA / SCL / RST / INT | 16 / 15 / 18 / 17 |
| BOOT button | 0 |

Notes

- The panel needs color inversion on (`ips = true` in `hw.cpp`), otherwise colors are photo-negative.
- The firmware runs landscape (rotation 3). The touch controller reports in the native portrait frame, `hw.cpp` converts it.
- The board also has a speaker, microphone, RGB LED, TF slot and battery connector. ClawTicker does not use them.
- Build target: `pioarduino` platform with `board_build.arduino.memory_type = qio_opi` and the `default_16MB.csv` partition table (see `firmware/platformio.ini`).
- The touch-less **E32N28P** variant is not supported: the BOOT button can switch screens, but Settings and the Finished dismiss need touch.
