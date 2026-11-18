#pragma once
#include <Arduino.h>

// Parsed /payload from the daemon (schema: docs/payload.md).
struct Card {
    char name[12] = "";
    uint8_t session = 0, weekly = 0;      // percent used
    int16_t sessionReset = -1, weeklyReset = -1;   // minutes, -1 = unknown
    bool off = false;                     // provider not detected on the computer
};

struct Coin {
    char sym[8] = "";
    double price = 0;
    float change24h = 0;
};

struct Data {
    bool multi = false;
    uint8_t cardCount = 0;
    Card cards[2];
    uint8_t coinCount = 0;
    Coin coins[4];
    uint32_t localTime = 0;       // seconds of local wall-clock at receivedAt
    uint32_t receivedAt = 0;      // millis() of last good poll, 0 = never
};
