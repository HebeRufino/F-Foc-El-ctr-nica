#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "combat_2p.h"
#include "display.h"
#include "net.h"

static const char *TAG = "pvp";

#define PVP_LIVES       3
#define RESULT_LOCK_MS  800      // keep the result on screen so the opponent's board can catch up
#define WAIT_TIMEOUT_MS 10000

typedef enum { ST_IDLE, ST_CHOOSE, ST_WAIT, ST_RESULT, ST_LOST } state_t;
typedef enum { OUT_DRAW, OUT_WIN, OUT_LOSE } outcome_t;

static const char *s_move_name[] = {"ROCK", "PAPER", "SCIS"};

static state_t s_state = ST_IDLE;
static uint8_t s_round, s_my_move, s_opp_move;
static int s_my_lives, s_opp_lives;
static outcome_t s_outcome;
static bool s_over;
static int64_t s_t0;
static const char *s_lost_msg;
static char s_opp_name[16];

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void pvp_start(void)
{
    uint8_t mac[6];
    net_opponent(mac);
    snprintf(s_opp_name, sizeof(s_opp_name), "PLAYER %02X:%02X", mac[4], mac[5]);
    s_round = 0;
    s_my_lives = s_opp_lives = PVP_LIVES;
    s_over = false;
    s_state = ST_CHOOSE;
    ESP_LOGI(TAG, "match vs %s", s_opp_name);
}

bool pvp_active(void)
{
    return s_state != ST_IDLE;
}

static void link_lost(const char *msg)
{
    s_lost_msg = msg;
    s_state = ST_LOST;
}

void pvp_update(void)
{
    if ((s_state == ST_CHOOSE || s_state == ST_WAIT) && net_state() != NET_LINKED) {
        link_lost("OPPONENT LEFT");
        return;
    }
    if (s_state != ST_WAIT) return;

    uint8_t m;
    if (net_get_opp_move(s_round, &m)) {
        s_opp_move = m;
        s_outcome = (outcome_t)(((int)s_my_move - (int)m + 3) % 3);
        if (s_outcome == OUT_WIN)  s_opp_lives--;
        if (s_outcome == OUT_LOSE) s_my_lives--;
        s_over = s_my_lives <= 0 || s_opp_lives <= 0;
        s_t0 = now_ms();
        s_state = ST_RESULT;
        ESP_LOGI(TAG, "round %u: %s vs %s, lives you=%d opp=%d", s_round, s_move_name[s_my_move],
                 s_move_name[m], s_my_lives, s_opp_lives);
    } else if (now_ms() - s_t0 > WAIT_TIMEOUT_MS) {
        net_leave();
        link_lost("NO RESPONSE");
    }
}

void pvp_handle_button(button_id_t id)
{
    switch (s_state) {
    case ST_CHOOSE:
        if (id == BTN_KEY1 || id == BTN_KEY2 || id == BTN_KEY3) {
            s_my_move = id - BTN_KEY1;                 // 0 rock, 1 paper, 2 scissors
            net_send_move(s_round, s_my_move);
            s_t0 = now_ms();
            s_state = ST_WAIT;
        } else if (id == BTN_KEY4) {
            net_leave();                               // forfeit
            s_state = ST_IDLE;
        }
        break;
    case ST_WAIT:
        if (id == BTN_KEY4) {
            net_leave();
            s_state = ST_IDLE;
        }
        break;
    case ST_RESULT:
        if (id == BTN_KEY1 && now_ms() - s_t0 >= RESULT_LOCK_MS) {
            if (s_over) {
                net_leave();
                s_state = ST_IDLE;
            } else {
                s_round++;
                s_state = ST_CHOOSE;
            }
        }
        break;
    case ST_LOST:
        if (id == BTN_KEY1) {
            net_leave();
            s_state = ST_IDLE;
        }
        break;
    default:
        break;
    }
}

void pvp_draw(void)
{
    char line[24];

    display_clear();
    snprintf(line, sizeof(line), "YOU %d", s_my_lives);
    display_text(0, 0, line, false);
    snprintf(line, sizeof(line), "OPP %d", s_opp_lives);
    display_text(98, 0, line, false);
    snprintf(line, sizeof(line), "VS %s", s_opp_name);
    display_text(0, 12, line, false);

    switch (s_state) {
    case ST_CHOOSE:
        snprintf(line, sizeof(line), "ROUND %u", s_round + 1);
        display_text(0, 28, line, false);
        display_text(0, 40, "PICK A MOVE 4:LEAVE", false);
        display_text(0, 56, "1:ROCK 2:PAPER 3:SCIS", false);
        break;
    case ST_WAIT:
        snprintf(line, sizeof(line), "YOU: %s", s_move_name[s_my_move]);
        display_text(0, 28, line, false);
        display_text(0, 40, "WAITING FOR OPP...", false);
        display_text(0, 56, "4:LEAVE", false);
        break;
    case ST_RESULT: {
        snprintf(line, sizeof(line), "YOU:%s OPP:%s", s_move_name[s_my_move], s_move_name[s_opp_move]);
        display_text(0, 28, line, false);

        const char *msg;
        if (s_over) {
            msg = s_my_lives > 0 ? "YOU WIN THE MATCH!" : "YOU LOST THE MATCH";
        } else {
            msg = s_outcome == OUT_WIN ? "HIT!" : s_outcome == OUT_LOSE ? "OUCH" : "DRAW";
        }
        display_text(0, 40, msg, true);
        display_text(0, 56, s_over ? "1:BACK TO LIST" : "1:NEXT ROUND", false);
        break;
    }
    case ST_LOST:
        display_text(0, 32, s_lost_msg, true);
        display_text(0, 56, "1:BACK TO LIST", false);
        break;
    default:
        break;
    }
}