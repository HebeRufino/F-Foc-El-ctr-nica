#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"

#include "combat.h"
#include "display.h"

static const char *TAG = "combat";

typedef enum { ST_IDLE, ST_CHOOSE, ST_RESULT } state_t;
typedef enum { MOVE_ROCK, MOVE_PAPER, MOVE_SCISSORS } move_t;
typedef enum { OUT_DRAW, OUT_WIN, OUT_LOSE } outcome_t;

static const char *s_move_name[] = {"ROCK", "PAPER", "SCIS"};
static const char *s_outcome_text[] = {"DRAW", "YOU WIN!", "YOU LOSE"};

static state_t s_state = ST_IDLE;
static char s_ssid[33];
static int8_t s_rssi;
static int s_enemy;                 // 0 or 1
static move_t s_player_move, s_enemy_move;
static outcome_t s_outcome;

void start_combat(const char *ssid, int8_t rssi)
{
    strlcpy(s_ssid, ssid[0] ? ssid : "<hidden>", sizeof(s_ssid));
    s_rssi = rssi;
    s_enemy = esp_random() % 2;     // picks enemy 0 or 1
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
    // 0 = draw, 1 = player wins, 2 = player loses
    s_outcome = (outcome_t)(((int)player - (int)s_enemy_move + 3) % 3);
    s_state = ST_RESULT;
    ESP_LOGI(TAG, "%s vs %s -> %s", s_move_name[player], s_move_name[s_enemy_move],
             s_outcome_text[s_outcome]);
}

void combat_handle_button(button_id_t id)
{
    switch (s_state) {
    case ST_CHOOSE:
        if (id == BTN_KEY1)      play_round(MOVE_ROCK);
        else if (id == BTN_KEY2) play_round(MOVE_PAPER);
        else if (id == BTN_KEY3) play_round(MOVE_SCISSORS);
        else if (id == BTN_KEY4) s_state = ST_IDLE;      // flee
        break;
    case ST_RESULT:
        if (id == BTN_KEY1)      s_state = ST_CHOOSE;    // same enemy, another round
        else if (id == BTN_KEY4) s_state = ST_IDLE;      // back to the list
        break;
    default:
        break;
    }
}

void combat_draw(void)
{
    char line[24];

    display_clear();
    display_enemy(s_enemy);         // enemy gets y = 0..39, text uses the bottom 24 px

    if (s_state == ST_CHOOSE) {
        snprintf(line, sizeof(line), "VS %.18s", s_ssid);
        display_text(0, 40, line, false);
        display_text(0, 48, "PICK A MOVE  4:FLEE", false);
        display_text(0, 56, "1:ROCK 2:PAPER 3:SCIS", false);
    } else if (s_state == ST_RESULT) {
        snprintf(line, sizeof(line), "YOU:%s FOE:%s", s_move_name[s_player_move],
                 s_move_name[s_enemy_move]);
        display_text(0, 40, line, false);
        display_text(0, 48, s_outcome_text[s_outcome], true);
        display_text(0, 56, "1:AGAIN  4:BACK", false);
    }
}