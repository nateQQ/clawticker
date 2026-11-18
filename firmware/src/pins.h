#pragma once
// DIYMORE ES3C28P: fixed PCB pinout (ILI9341V panel, FT6336G touch).
// Source: LCD wiki manual for the E32C28P module.
#define LCD_CS    10
#define LCD_SCLK  12
#define LCD_MOSI  11
#define LCD_DC    46
#define LCD_BL    45   // backlight MOSFET, PWM
#define TOUCH_SDA 16
#define TOUCH_SCL 15
#define TOUCH_RST 18
#define TOUCH_INT 17
#define TOUCH_ADDR 0x38
#define BOOT_BTN   0

#define SCREEN_W 320
#define SCREEN_H 240
