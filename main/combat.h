#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "buttons.h"

void start_combat(const char *ssid, int8_t rssi);
bool combat_active(void);
void combat_handle_button(button_id_t id);
void combat_draw(void);