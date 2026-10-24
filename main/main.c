#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "scan.h"
#include "audio.h"
#include "buttons.h"
#include "display.h"
#include "combat.h"
#include "net.h"
#include "combat_2p.h"

static const char *TAG = "demo";

#define UI_PERIOD_MS 50

// Cosas de aps/scaneo
#define MAX_APS   10
#define LIST_ROWS 6
// Cosas de contrincantes/otros jugadores
static net_peer_t s_peers[NET_MAX_PEERS];
static int s_np;

static scan_result_t s_aps[MAX_APS];
static int s_n, s_sel, s_top;


static const int s_button_freq[BTN_COUNT] = {
    [BTN_UP] = 659,
    [BTN_DOWN] = 440,
    [BTN_LEFT] = 494,
    [BTN_RIGHT] = 587,
    [BTN_CENTER] = 784,
    [BTN_KEY1] = 262,
    [BTN_KEY2] = 330,
    [BTN_KEY3] = 392,
    [BTN_KEY4] = 523,
};

static void draw_key(int x, int y, int size, bool pressed)
{
    if (pressed) {
        display_fill_rect(x, y, size, size, true);
    } else {
        display_fill_rect(x, y, size, size, false);
        display_draw_rect(x, y, size, size);
    }
}

static void draw_ui(uint32_t pressed, int mic_level, bool audio_ok)
{
    char line[24];

    display_clear();
    display_text(2, 1, "HACKATHON BOARD", true);
    display_text(96, 1, "HOLA", true);

    display_text(0, 14, "MIC", false);
    display_draw_rect(22, 13, 82, 10);
    display_fill_rect(24, 15, mic_level * 78 / 100, 6, true);
    if (audio_ok) {
        snprintf(line, sizeof(line), "%3d", mic_level);
        display_text(107, 14, line, false);
    } else {
        display_text(107, 14, "ERR", false);
    }

    draw_key(15, 28, 10, pressed & (1u << BTN_UP));
    draw_key(4, 39, 10, pressed & (1u << BTN_LEFT));
    draw_key(15, 39, 10, pressed & (1u << BTN_CENTER));
    draw_key(26, 39, 10, pressed & (1u << BTN_RIGHT));
    draw_key(15, 50, 10, pressed & (1u << BTN_DOWN));

    display_text(50, 29, "KEYS", false);
    for (int i = 0; i < 4; i++) {
        bool on = pressed & (1u << (BTN_KEY1 + i));
        int x = 50 + i * 15;
        draw_key(x, 40, 13, on);
        char num[2] = {'1' + i, 0};
        display_text(x + 4, 43, num, on);
    }
}

// Inicio seleccion "enemigo"

static void draw_list(void)
{
    char line[24], name[34];

    s_np = net_get_peers(s_peers, NET_MAX_PEERS);
    const int total = s_np + s_n;
    if (s_sel >= total) s_sel = total ? total - 1 : 0;
    if (s_top > s_sel) s_top = s_sel;
    if (s_sel >= s_top + LIST_ROWS) s_top = s_sel - LIST_ROWS + 1;

    display_clear();
    display_text(0, 0, "WIFI + PLAYERS", true);

    for (int r = 0; r < LIST_ROWS; r++) {
        const int i = s_top + r;
        if (i >= total) break;

        char mark;
        int rssi;
        if (i < s_np) {                              // players first
            snprintf(name, sizeof(name), "PLAYER %02X:%02X", s_peers[i].mac[4], s_peers[i].mac[5]);
            mark = '#';
            rssi = s_peers[i].rssi;
        } else {
            const scan_result_t *ap = &s_aps[i - s_np];
            snprintf(name, sizeof(name), "%s", ap->ssid[0] ? ap->ssid : "<hidden>");
            mark = combat_is_defeated(ap->bssid) ? '*' : ' ';
            rssi = ap->rssi;
        }
        snprintf(line, sizeof(line), "%c%-13.13s %4d", mark, name, rssi);
        display_text(0, 10 + r * 9, line, i == s_sel);
    }
}

static void do_scan(bool display_ok)
{
    if (display_ok) {                       // the scan blocks for a couple of seconds
        display_clear();
        display_text(10, 28, "Scanning...", false);
        display_flush();
    }
    s_n = scan_run(s_aps, MAX_APS);
    net_apply_channel();
    s_sel = s_top = 0;
}

static void draw_invite(net_state_t st)
{
    char line[24];

    display_clear();
    if (st == NET_INVITING) {
        display_text(0, 20, "INVITING PLAYER...", false);
        display_text(0, 40, "4:CANCEL", false);
    } else {                                         // NET_INVITED
        uint8_t mac[6];
        net_opponent(mac);
        snprintf(line, sizeof(line), "PLAYER %02X:%02X", mac[4], mac[5]);
        display_text(0, 12, "CHALLENGE FROM", false);
        display_text(0, 24, line, false);
        display_text(0, 48, "1:ACCEPT 4:DECLINE", false);
    }
}

// static void draw_pvp_stub(void)        // replace with the real 2-player combat
// {
//     display_clear();
//     display_text(0, 20, "LINKED! PVP TODO", false);
//     display_text(0, 40, "4:LEAVE", false);
// }

static void handle_button(button_id_t id)
{
    switch (id) {
    case BTN_UP:
        if (s_sel > 0) s_sel--;
        if (s_sel < s_top) s_top = s_sel;
        break;
    case BTN_DOWN:
        if (s_sel < s_np + s_n - 1) s_sel++;
        if (s_sel >= s_top + LIST_ROWS) s_top = s_sel - LIST_ROWS + 1;
        break;
    case BTN_KEY1:
        if (s_sel < s_np) {
            net_invite(s_peers[s_sel].mac);
        } else if (s_sel - s_np < s_n) {
            const scan_result_t *ap = &s_aps[s_sel - s_np];
            if (!combat_is_defeated(ap->bssid)) {
                start_combat(ap->ssid, ap->rssi, ap->bssid);
            }
        }
        break;
    case BTN_KEY2:                          // rescan
        do_scan(true);
        break;
    default:
        break;
    }
}

// Fin seleccion de "enemigo"


void app_main(void)
{
    ESP_LOGI(TAG, "Hackathon board demo starting");

    bool display_ok = display_init() == ESP_OK;
    if (!display_ok) {
        ESP_LOGE(TAG, "display init failed - continuing without screen");
    }

    ESP_ERROR_CHECK(buttons_init());

    if (display_ok) {
        display_clear();
        display_text(10, 8, "Fo Foca Game", false);
        
        display_flush();
    }

    bool audio_ok = audio_init() == ESP_OK;
    if (audio_ok) {
        const int notes[] = {523, 659, 784};
        for (int i = 0; i < 3; i++) {
            audio_play_tone(notes[i], 120);
        }
    } else {
        ESP_LOGE(TAG, "audio init failed - continuing without sound");
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    QueueHandle_t events = buttons_event_queue();
    int log_div = 0;

    scan_init();
    ESP_ERROR_CHECK(net_init());

    while (1) {
        const net_state_t ns = net_state();
        if (ns == NET_LINKED && !pvp_active()) pvp_start();    // link just came up
        pvp_update();
        net_discovery(!combat_active() && !pvp_active() && ns == NET_IDLE);

        button_event_t ev;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(UI_PERIOD_MS)) == pdTRUE && ev.pressed) {
            if (pvp_active()) {
                pvp_handle_button(ev.id);
            } else if (ns == NET_INVITING) {
                if (ev.id == BTN_KEY4) net_leave();
            } else if (ns == NET_INVITED) {
                if (ev.id == BTN_KEY1) net_accept();
                else if (ev.id == BTN_KEY4) net_leave();
            } else if (combat_active()) {
                combat_handle_button(ev.id);
            } else {
                handle_button(ev.id);
            }
            if (audio_ok) audio_play_tone(s_button_freq[ev.id], 100);
        }

        if (display_ok) {
            if (pvp_active())                            pvp_draw();
            else if (ns == NET_INVITING || ns == NET_INVITED) draw_invite(ns);
            else if (combat_active())                    combat_draw();
            else                                         draw_list();
            display_flush();
        }
    }
}
