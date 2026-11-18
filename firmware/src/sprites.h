#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SPRITE_COUNT 13
#define SPRITE_SIZE 20

typedef struct {
    const char *slug;
    uint8_t frames, colors;
    const uint8_t *palette;     // colors x (r, g, b, a)
    const uint16_t *hold_ms;    // per frame
    const uint8_t *pixels;      // frames x SIZE x SIZE palette indices
} sprite_t;

extern const sprite_t SPRITES[SPRITE_COUNT];

#ifdef __cplusplus
}
#endif
