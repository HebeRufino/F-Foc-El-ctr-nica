#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"

#include "combat.h"
#include "display.h"

static const char *TAG = "combat";

#define PLAYER_LIVES  5
#define ENEMY_LIVES   3
#define MAX_DEFEATED  32

typedef enum { ST_IDLE, ST_CHOOSE, ST_RESULT } state_t;
typedef enum { MOVE_ROCK, MOVE_PAPER, MOVE_SCISSORS } move_t;
typedef enum { OUT_DRAW, OUT_WIN, OUT_LOSE } outcome_t;

static const char *s_move_name[] = {"ROCK", "PAPER", "SCIS"};

static state_t s_state = ST_IDLE;
static char s_ssid[33];
static uint8_t s_bssid[6];
static int8_t s_rssi;
static int s_enemy;
static int s_player_lives, s_enemy_lives;
static move_t s_player_move, s_enemy_move;
static outcome_t s_outcome;
static bool s_over;                       // last round ended the fight

static uint8_t s_defeated[MAX_DEFEATED][6];
static int s_defeated_n;

bool combat_is_defeated(const uint8_t bssid[6])
{
    for (int i = 0; i < s_defeated_n; i++) {
        if (memcmp(s_defeated[i], bssid, 6) == 0) return true;
    }
    return false;
}

static void mark_defeated(void)
{
    if (!combat_is_defeated(s_bssid) && s_defeated_n < MAX_DEFEATED) {
        memcpy(s_defeated[s_defeated_n++], s_bssid, 6);
    }
}

void start_combat(const char *ssid, int8_t rssi, const uint8_t bssid[6])
{
    strlcpy(s_ssid, ssid[0] ? ssid : "<hidden>", sizeof(s_ssid));
    memcpy(s_bssid, bssid, 6);
    s_rssi = rssi;
    s_enemy = esp_random() % 2;
    s_player_lives = PLAYER_LIVES;
    s_enemy_lives = ENEMY_LIVES;
    s_over = false;
    s_state = ST_CHOOSE;
    ESP_LOGI(TAG, "combat vs '%s' (rssi %d), enemy %d", s_ssid, s_rssi, s_enemy);
}

bool combat_active(void)
{
    return s_state != ST_IDLE;
}

static void play_round(move_t player)
{
    s_player_move = player;
    s_enemy_move = (move_t)(esp_random() % 3);
    s_outcome = (outcome_t)(((int)player - (int)s_enemy_move + 3) % 3);

    if (s_outcome == OUT_WIN)  s_enemy_lives--;
    if (s_outcome == OUT_LOSE) s_player_lives--;

    if (s_enemy_lives <= 0) {
        s_over = true;
        mark_defeated();
    } else if (s_player_lives <= 0) {
        s_over = true;
    }
    s_state = ST_RESULT;
    ESP_LOGI(TAG, "%s vs %s, lives you=%d foe=%d", s_move_name[player],
             s_move_name[s_enemy_move], s_player_lives, s_enemy_lives);
}

void combat_handle_button(button_id_t id)
{
    switch (s_state) {
    case ST_CHOOSE:
        if (id == BTN_KEY1)      play_round(MOVE_ROCK);
        else if (id == BTN_KEY2) play_round(MOVE_PAPER);
        else if (id == BTN_KEY3) play_round(MOVE_SCISSORS);
        else if (id == BTN_KEY4) s_state = ST_IDLE;      // flee: nobody is defeated
        break;
    case ST_RESULT:
        if (id == BTN_KEY1) {
            s_state = s_over ? ST_IDLE : ST_CHOOSE;      // fight over -> back to list
        }
        break;
    default:
        break;
    }
}

void combat_draw(void)
{
    char line[24];

    display_clear();
    display_enemy(s_enemy);

    snprintf(line, sizeof(line), "YOU %d", s_player_lives);
    display_text(0, 0, line, false);
    snprintf(line, sizeof(line), "FOE %d", s_enemy_lives);
    display_text(98, 0, line, false);

    if (s_state == ST_CHOOSE) {
        snprintf(line, sizeof(line), "VS %.18s", s_ssid);
        display_text(0, 40, line, false);
        display_text(0, 48, "PICK A MOVE  4:FLEE", false);
        display_text(0, 56, "1:ROCK 2:PAPER 3:SCIS", false);
    } else if (s_state == ST_RESULT) {
        snprintf(line, sizeof(line), "YOU:%s FOE:%s", s_move_name[s_player_move],
                 s_move_name[s_enemy_move]);
        display_text(0, 40, line, false);

        const char *msg;
        if (s_over) {
            msg = s_enemy_lives <= 0 ? "WIFI HACKED!" : "YOU DIED";
        } else {
            msg = s_outcome == OUT_WIN ? "HIT!" : s_outcome == OUT_LOSE ? "OUCH" : "DRAW";
        }
        display_text(0, 48, msg, true);
        display_text(0, 56, s_over ? "1:BACK TO LIST" : "1:NEXT ROUND", false);
    }
}