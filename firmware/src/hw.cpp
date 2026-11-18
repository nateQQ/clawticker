#include "hw.h"
#include "pins.h"
#include <Arduino_GFX_Library.h>
#include <Wire.h>

static Arduino_DataBus *bus;
static Arduino_ILI9341 *panel;

void hw_init() {
    pinMode(BOOT_BTN, INPUT_PULLUP);
    bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
    // Native GRAM is 240x320 portrait. Rotation 3 gives the 320x240 landscape
    // layout with the module's USB ports on the right. ips=true is needed for
    // correct colors on this panel.
    panel = new Arduino_ILI9341(bus, GFX_NOT_DEFINED, 3, true, 240, 320);
    panel->begin();
    panel->fillScreen(0x0000);
    ledcAttach(LCD_BL, 5000, 8);
    hw_set_brightness(200);

    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(50);
}

void hw_draw(int x, int y, int w, int h, const uint16_t *pixels) {
    panel->draw16bitRGBBitmap(x, y, (uint16_t *)pixels, w, h);
}

void hw_set_brightness(uint8_t level) { ledcWrite(LCD_BL, level); }

// FT6336G: reg 0x02 = finger count, 0x03..0x06 = first touch X/Y (12 bit each).
// The controller reports in the panel's native portrait frame (240x320), so
// rotate into the 320x240 landscape frame used by the canvas.
static bool touchDown = false;
static void touchReleased() { touchDown = false; }

static int injX, injY;
static uint32_t injUntil = 0;
void hw_inject_touch(int x, int y, uint32_t ms) { injX = x; injY = y; injUntil = millis() + ms; }

bool hw_touch(int &x, int &y) {
    if (millis() < injUntil) { x = injX; y = injY; return true; }
    uint8_t buf[5];
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)5) != 5) return false;
    for (auto &b : buf) b = Wire.read();
    uint8_t fingers = buf[0] & 0x0F;
    if (fingers == 0 || fingers > 2) { touchReleased(); return false; }   // 0 = no touch, >2 = garbage
    int rx = ((buf[1] & 0x0F) << 8) | buf[2];
    int ry = ((buf[3] & 0x0F) << 8) | buf[4];
    x = constrain(SCREEN_W - 1 - ry, 0, SCREEN_W - 1);
    y = constrain(rx, 0, SCREEN_H - 1);
#ifdef TOUCH_DEBUG
    if (!touchDown) Serial.printf("touch raw=(%d,%d) screen=(%d,%d)\n", rx, ry, x, y);
#endif
    touchDown = true;
    return true;
}

bool hw_boot_pressed() { return digitalRead(BOOT_BTN) == LOW; }
