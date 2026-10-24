#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define NET_MAX_PEERS 6

typedef enum { NET_IDLE, NET_INVITING, NET_INVITED, NET_LINKED } net_state_t;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
} net_peer_t;

esp_err_t net_init(void);                 // call after scan_init()
void net_apply_channel(void);             // call after every Wi-Fi scan

void net_discovery(bool on);              // true while the 2-player screen is open
int  net_get_peers(net_peer_t *out, int max);

esp_err_t net_invite(const uint8_t mac[6]);
bool net_accept(void);                    // accept the pending invite
void net_leave(void);                     // decline, cancel or quit the match
net_state_t net_state(void);
void net_opponent(uint8_t mac_out[6]);

esp_err_t net_send_move(uint8_t round, uint8_t move);              // move 0..2
bool net_get_opp_move(uint8_t round, uint8_t *move);               // true once it arrived