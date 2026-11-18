#pragma once
#include "data.h"

void net_start();                     // starts the Wi-Fi + polling task
bool net_wifi_connected();
bool net_get(Data &out);              // copy of latest data, false if never received
uint32_t net_age_ms();                // ms since last good poll (UINT32_MAX if never)
