#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB   // malloc, so big buffers land in PSRAM
#define LV_USE_SNAPSHOT 1                      // serial `shot` command
#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 16
#define LV_FONT_MONTSERRAT_14 1     // only for the gear / close symbols
#define LV_USE_LOG 0

#endif
