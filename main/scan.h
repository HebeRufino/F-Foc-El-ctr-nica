#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    char ssid[33];
    int8_t rssi;
} scan_result_t;

esp_err_t scan_init(void);                    
int scan_run(scan_result_t *out, int max);    