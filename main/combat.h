#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "buttons.h"

void start_combat(const char *ssid, int8_t rssi, const uint8_t bssid[6]);
bool combat_active(void);
bool combat_is_defeated(const uint8_t bssid[6]);
void combat_handle_button(button_id_t id);
void combat_draw(void);