#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "net.h"

static const char *TAG = "net";

#define NET_CHANNEL       1
#define MAGIC             0xF0
#define PEER_TIMEOUT_MS   3000
#define INVITE_TIMEOUT_MS 5000

enum { MSG_HELLO, MSG_INVITE, MSG_ACCEPT, MSG_LEAVE, MSG_MOVE };

typedef struct __attribute__((packed)) {
    uint8_t magic, type, round, move;
} msg_t;

static const uint8_t BCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static struct {
    uint8_t mac[6];
    int8_t rssi;
    int64_t seen_ms;
} s_peers[NET_MAX_PEERS];

static net_state_t s_state = NET_IDLE;
static uint8_t s_opp[6];
static bool s_discovery;
static int s_accept_burst;
static int64_t s_invite_start;
static bool s_my_have, s_opp_have;
static uint8_t s_my_round, s_my_move, s_opp_round, s_opp_move;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static bool is_opp(const uint8_t *mac) { return memcmp(mac, s_opp, 6) == 0; }

static void ensure_peer(const uint8_t *mac)
{
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t p = {0};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;                 // 0 = use the current channel
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    esp_now_add_peer(&p);
}

static esp_err_t send_to(const uint8_t *mac, uint8_t type, uint8_t round, uint8_t move)
{
    const msg_t m = {MAGIC, type, round, move};
    ensure_peer(mac);
    return esp_now_send(mac, (const uint8_t *)&m, sizeof(m));
}

/* ---- called with s_mux held ---- */
static void peer_seen(const uint8_t *mac, int8_t rssi)
{
    int free_i = -1, oldest = 0;
    int64_t now = now_ms();
    for (int i = 0; i < NET_MAX_PEERS; i++) {
        if (s_peers[i].seen_ms && memcmp(s_peers[i].mac, mac, 6) == 0) {
            s_peers[i].rssi = rssi;
            s_peers[i].seen_ms = now;
            return;
        }
        if (!s_peers[i].seen_ms || now - s_peers[i].seen_ms > PEER_TIMEOUT_MS) free_i = i;
        if (s_peers[i].seen_ms < s_peers[oldest].seen_ms) oldest = i;
    }
    int i = free_i >= 0 ? free_i : oldest;
    memcpy(s_peers[i].mac, mac, 6);
    s_peers[i].rssi = rssi;
    s_peers[i].seen_ms = now;
}

static void link_up(void)
{
    s_state = NET_LINKED;
    s_my_have = s_opp_have = false;
    s_accept_burst = 5;
}
/* -------------------------------- */

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len != sizeof(msg_t)) return;
    const msg_t *m = (const msg_t *)data;
    if (m->magic != MAGIC) return;
    const uint8_t *src = info->src_addr;

    portENTER_CRITICAL(&s_mux);
    switch (m->type) {
    case MSG_HELLO:
        peer_seen(src, info->rx_ctrl->rssi);
        break;
    case MSG_INVITE:
        if (s_state == NET_IDLE && s_discovery) {
            memcpy(s_opp, src, 6);
            s_state = NET_INVITED;
        } else if (s_state == NET_INVITING && is_opp(src)) {
            link_up();                         // we invited each other at once
        } else if (s_state == NET_LINKED && is_opp(src)) {
            s_accept_burst = 5;                // our ACCEPT got lost, repeat it
        }
        break;
    case MSG_ACCEPT:
        if (s_state == NET_INVITING && is_opp(src)) link_up();
        break;
    case MSG_LEAVE:
        if (s_state != NET_IDLE && is_opp(src)) s_state = NET_IDLE;
        break;
    case MSG_MOVE:
        if (s_state == NET_LINKED && is_opp(src) && m->move < 3) {
            s_opp_round = m->round;
            s_opp_move = m->move;
            s_opp_have = true;
        }
        break;
    }
    portEXIT_CRITICAL(&s_mux);
}

static void net_task(void *arg)
{
    int64_t last_hello = 0, last_inv = 0, last_move = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        const int64_t now = now_ms();

        portENTER_CRITICAL(&s_mux);
        const net_state_t st = s_state;
        const bool disc = s_discovery;
        uint8_t opp[6];
        memcpy(opp, s_opp, 6);
        const int64_t inv_start = s_invite_start;
        const int burst = s_accept_burst;
        if (s_accept_burst > 0) s_accept_burst--;
        const bool mv = s_my_have;
        const uint8_t r = s_my_round, v = s_my_move;
        portEXIT_CRITICAL(&s_mux);

        if (st == NET_IDLE && disc && now - last_hello >= 500) {
            last_hello = now;
            send_to(BCAST, MSG_HELLO, 0, 0);
        }
        if (st == NET_INVITING) {
            if (now - inv_start > INVITE_TIMEOUT_MS) {
                send_to(opp, MSG_LEAVE, 0, 0);
                portENTER_CRITICAL(&s_mux);
                if (s_state == NET_INVITING) s_state = NET_IDLE;
                portEXIT_CRITICAL(&s_mux);
            } else if (now - last_inv >= 300) {
                last_inv = now;
                send_to(opp, MSG_INVITE, 0, 0);
            }
        }
        if (st == NET_LINKED && burst > 0) {
            send_to(opp, MSG_ACCEPT, 0, 0);
        }
        // Keep resending our latest move until the next one replaces it.
        if (st == NET_LINKED && mv && now - last_move >= 150) {
            last_move = now;
            send_to(opp, MSG_MOVE, r, v);
        }
    }
}

esp_err_t net_init(void)
{
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "ps failed");   // keep radio awake
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp_now_init failed");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(on_recv), TAG, "recv cb failed");

    esp_now_peer_info_t b = {0};
    memcpy(b.peer_addr, BCAST, 6);
    b.ifidx = WIFI_IF_STA;
    ESP_RETURN_ON_ERROR(esp_now_add_peer(&b), TAG, "broadcast peer failed");

    net_apply_channel();
    ESP_RETURN_ON_FALSE(xTaskCreate(net_task, "net", 3072, NULL, 5, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "task create failed");
    ESP_LOGI(TAG, "ESP-NOW ready on channel %d", NET_CHANNEL);
    return ESP_OK;
}

void net_apply_channel(void)
{
    esp_wifi_set_channel(NET_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

void net_discovery(bool on)
{
    portENTER_CRITICAL(&s_mux);
    s_discovery = on;
    portEXIT_CRITICAL(&s_mux);
}

int net_get_peers(net_peer_t *out, int max)
{
    int n = 0;
    const int64_t now = now_ms();
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < NET_MAX_PEERS && n < max; i++) {
        if (s_peers[i].seen_ms && now - s_peers[i].seen_ms <= PEER_TIMEOUT_MS) {
            memcpy(out[n].mac, s_peers[i].mac, 6);
            out[n].rssi = s_peers[i].rssi;
            n++;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}

esp_err_t net_invite(const uint8_t mac[6])
{
    esp_err_t ret = ESP_OK;
    portENTER_CRITICAL(&s_mux);
    if (s_state != NET_IDLE) {
        ret = ESP_ERR_INVALID_STATE;
    } else {
        memcpy(s_opp, mac, 6);
        s_state = NET_INVITING;
        s_invite_start = now_ms();
    }
    portEXIT_CRITICAL(&s_mux);
    return ret;
}

bool net_accept(void)
{
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (s_state == NET_INVITED) {
        link_up();
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

void net_leave(void)
{
    uint8_t opp[6];
    bool was_active;
    portENTER_CRITICAL(&s_mux);
    was_active = s_state != NET_IDLE;
    memcpy(opp, s_opp, 6);
    s_state = NET_IDLE;
    s_my_have = s_opp_have = false;
    portEXIT_CRITICAL(&s_mux);
    if (was_active) send_to(opp, MSG_LEAVE, 0, 0);
}

net_state_t net_state(void)
{
    return s_state;
}

void net_opponent(uint8_t mac_out[6])
{
    portENTER_CRITICAL(&s_mux);
    memcpy(mac_out, s_opp, 6);
    portEXIT_CRITICAL(&s_mux);
}

esp_err_t net_send_move(uint8_t round, uint8_t move)
{
    uint8_t opp[6];
    portENTER_CRITICAL(&s_mux);
    s_my_round = round;
    s_my_move = move;
    s_my_have = true;
    memcpy(opp, s_opp, 6);
    portEXIT_CRITICAL(&s_mux);
    return send_to(opp, MSG_MOVE, round, move);    // the task keeps resending it
}

bool net_get_opp_move(uint8_t round, uint8_t *move)
{
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (s_opp_have && s_opp_round == round) {
        *move = s_opp_move;
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}