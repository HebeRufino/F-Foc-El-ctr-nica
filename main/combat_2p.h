#pragma once
#include <stdbool.h>
#include "buttons.h"

void pvp_start(void);                    // call once when the link comes up
bool pvp_active(void);
void pvp_update(void);                   // call every loop iteration
void pvp_handle_button(button_id_t id);
void pvp_draw(void);