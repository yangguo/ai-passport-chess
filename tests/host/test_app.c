/* Exercise the real application owner with only hardware/task boundaries
 * replaced. Codec, A/B save recovery, rules and UI reducer remain real. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../main/chess_app.c"

static uint8_t slots[2][CHESS_SAVE_SLOT_MAX];
static size_t lengths[2];
static chess_ai_request request;
static unsigned requests;
static bool busy;
static chess_ai_result_event result;
static bool result_ready;
static chess_view rendered;
static chess_save loaded;
static int64_t fake_time_us = 1000000;
static uint8_t backlight_percent;
static unsigned sleep_attempts;

static chess_error read_slot(void *ctx, int slot, uint8_t *out, size_t cap, size_t *len) {
    (void)ctx;
    if (!lengths[slot]) return CHESS_ERR_NO_SAVE;
    if (cap < lengths[slot]) return CHESS_ERR_BUFFER_TOO_SMALL;
    *len = lengths[slot]; memcpy(out, slots[slot], *len); return CHESS_OK;
}
static chess_error write_slot(void *ctx, int slot, const uint8_t *data, size_t len) {
    (void)ctx; assert(len <= sizeof(slots[slot]));
    memcpy(slots[slot], data, len); lengths[slot] = len; return CHESS_OK;
}
static chess_error commit_slot(void *ctx) { (void)ctx; return CHESS_OK; }
chess_nvs_backend chess_nvs_esp_backend(void) {
    return (chess_nvs_backend){read_slot, write_slot, commit_slot, NULL};
}
int bsp_battery_soc(void) { return 80; }
void bsp_display_backlight(uint8_t percent) { backlight_percent = percent; }
esp_err_t bsp_power_wait_for_wake_release(void) { return ESP_OK; }
esp_err_t bsp_power_enter_deep_sleep(void) { sleep_attempts++; return ESP_FAIL; }
bool bsp_lvgl_lock(int timeout) { (void)timeout; return true; }
void bsp_lvgl_unlock(void) {}
void chess_ui_render(const chess_view *view) { rendered = *view; }
int64_t esp_timer_get_time(void) { return fake_time_us; }
QueueHandle_t xQueueCreate(unsigned n, size_t size) { (void)n; (void)size; return NULL; }
int xQueueSend(QueueHandle_t q, const void *item, unsigned ticks) {
    (void)q; (void)item; (void)ticks; return 0;
}
int xQueueReceive(QueueHandle_t q, void *item, unsigned ticks) {
    (void)q; (void)item; (void)ticks; return 0;
}
bool chess_ai_task_start(void) { return true; }
bool chess_ai_task_busy(void) { return busy; }
bool chess_ai_task_request(const chess_ai_request *req) {
    if (busy) return false;
    request = *req; requests++; busy = true; return true;
}
void chess_ai_task_cancel(void) {}
bool chess_ai_task_take_result(chess_ai_result_event *out) {
    if (!result_ready) return false;
    *out = result; result_ready = false; busy = false; return true;
}

static void click(bsp_btn_t button) { on_input(button, BSP_BTN_CLICK); }
static void reset(void) {
    memset(slots, 0, sizeof(slots)); memset(lengths, 0, sizeof(lengths));
    requests = 0; busy = false; result_ready = false;
    s_backend = chess_nvs_esp_backend(); s_backend_ready = true;
    s_generation = 1; s_seq = 1; s_screen = APP_HOME;
    s_home_mode = 0; s_home_idx = 2; s_language = CHESS_LANGUAGE_ENGLISH;
    s_difficulty = CHESS_AI_NORMAL; s_suppress_click = false;
    s_brightness = 80; s_brightness_original = 80;
    chess_power_init(&s_power, (uint64_t)(fake_time_us / 1000));
    backlight_percent = 80; sleep_attempts = 0;
    chess_ui_model_init(&s_model); start_fresh_game();
    s_screen = APP_HOME;
}
static void choose_difficulty_color(unsigned level, bool black) {
    click(BSP_BTN_OK); /* New vs AI -> difficulty */
    for (unsigned i = 0; i < level; i++) click(BSP_BTN_DOWN);
    click(BSP_BTN_OK); /* difficulty -> color, without starting a game */
    assert(rendered.screen == CHESS_VIEW_HOME);
    assert(rendered.nmenu == 2);
    assert(requests == 0);
    if (black) click(BSP_BTN_DOWN);
    click(BSP_BTN_OK);
}
static void choose_color(bool black) { choose_difficulty_color(1, black); }
static void deliver_move(uint8_t from, uint8_t to) {
    result = (chess_ai_result_event){.outcome=CHESS_AI_OK, .has_move=true,
        .move={.from=from, .to=to, .promotion=CHESS_EMPTY},
        .generation=request.generation, .level=request.level};
    result_ready = true; assert(drain_ai_results()); render_all();
}
static void test_black_ai_opens_and_save_restores(void) {
    reset(); choose_color(true);
    assert(s_game.position.side_to_move == CHESS_WHITE);
    assert(requests == 1 && s_thinking);
    assert(request.book_enabled);
    assert(request.position.side_to_move == CHESS_WHITE);
    click(BSP_BTN_OK); /* human cannot move for White while AI is thinking */
    assert(s_game.position.side_to_move == CHESS_WHITE);
    assert(chess_save_load(&s_backend, &loaded) == CHESS_OK);
    assert(loaded.human_color == CHESS_BLACK);
    assert(loaded.settings.difficulty == CHESS_DIFF_MEDIUM);
    deliver_move(12, 28); /* e2-e4; real core validation */
    assert(s_game.position.side_to_move == CHESS_BLACK);
    assert(s_game.position.board[28].color == CHESS_WHITE);
    assert(requests == 1 && !s_thinking);
    s_mode = CHESS_MODE_LOCAL; s_difficulty = CHESS_AI_EASY;
    s_human_color = CHESS_WHITE;
    boot_load(); render_all();
    assert(!s_book_enabled);
    assert(s_mode == CHESS_MODE_AI && s_difficulty == CHESS_AI_NORMAL);
    assert(rendered.snapshot.bottom == CHESS_BLACK);
    assert(requests == 1); /* loaded Black turn belongs to the human */
    assert(chess_save_load(&s_backend, &loaded) == CHESS_OK);
    assert(loaded.human_color == CHESS_BLACK);
    handle_command((chess_ui_command){.kind=CHESS_CMD_SUBMIT_MOVE,
        .move={.from=52,.to=36,.promotion=CHESS_EMPTY}}); /* e7-e5 */
    assert(s_game.position.side_to_move == CHESS_WHITE);
    assert(requests == 2 && request.position.side_to_move == CHESS_WHITE);
}
static void test_white_human_opens(void) {
    reset(); choose_color(false);
    assert(s_game.position.side_to_move == CHESS_WHITE);
    assert(requests == 0 && !s_thinking);
    assert(chess_save_load(&s_backend, &loaded) == CHESS_OK);
    assert(loaded.human_color == CHESS_WHITE);
    handle_command((chess_ui_command){.kind=CHESS_CMD_SUBMIT_MOVE,
        .move={.from=12,.to=28,.promotion=CHESS_EMPTY}});
    assert(s_game.position.side_to_move == CHESS_BLACK);
    assert(requests == 1 && request.position.side_to_move == CHESS_BLACK);
}

static void test_brightness_settings_save_and_restore(void) {
    reset();
    s_screen = APP_BOARD;
    feed_model_moves();
    on_input(BSP_BTN_OK, BSP_BTN_LONG); /* pause */
    on_input(BSP_BTN_OK, BSP_BTN_CLICK); /* consumed release click */
    for (unsigned i = 0; i < 5; i++) click(BSP_BTN_DOWN);
    click(BSP_BTN_OK); /* brightness page */
    assert(rendered.screen == CHESS_VIEW_BRIGHTNESS);
    assert(rendered.brightness == 80);
    click(BSP_BTN_DOWN);
    on_input(BSP_BTN_OK, BSP_BTN_LONG); /* cancel the edit */
    assert(s_brightness == 80 && backlight_percent == 80);
    on_input(BSP_BTN_OK, BSP_BTN_CLICK); /* consumed release click */
    click(BSP_BTN_OK); /* reopen the still-selected brightness entry */
    click(BSP_BTN_UP);
    assert(rendered.brightness == 90);
    click(BSP_BTN_OK); /* save and return to pause */
    assert(rendered.screen == CHESS_VIEW_PAUSE);
    assert(s_brightness == 90);
    assert(chess_save_load(&s_backend, &loaded) == CHESS_OK);
    assert(loaded.settings.brightness == 90);
    s_brightness = 80;
    boot_load();
    assert(s_brightness == 90);
}

static void test_idle_dim_restore_and_defer_sleep_while_ai_busy(void) {
    reset();
    s_screen = APP_BOARD;
    feed_model_moves();
    chess_power_init(&s_power, 1000);
    fake_time_us = 31000000;
    poll_power_policy();
    assert(backlight_percent == 20);
    click(BSP_BTN_UP);
    assert(backlight_percent == 80);

    fake_time_us = 331000000;
    busy = true;
    poll_power_policy();
    assert(sleep_attempts == 0);
    busy = false;
    poll_power_policy();
    assert(sleep_attempts == 1);
    assert(s_saved);
}
int main(void) {
    test_brightness_settings_save_and_restore();
    test_idle_dim_restore_and_defer_sleep_while_ai_busy();
    test_black_ai_opens_and_save_restores(); test_white_human_opens();
    reset(); choose_color(true);
    result = (chess_ai_result_event){.outcome=CHESS_AI_CANCELLED,
        .generation=request.generation, .level=request.level};
    result_ready = true; s_cancel_await = true;
    assert(drain_ai_results());
    assert(s_model.screen == CHESS_SCREEN_PAUSE);
    handle_command((chess_ui_command){.kind=CHESS_CMD_SUBMIT_MOVE,
        .move={.from=12,.to=28,.promotion=CHESS_EMPTY}});
    assert(s_game.position.side_to_move == CHESS_WHITE);
    assert(s_game.position.board[12].type == CHESS_PAWN);
    handle_command((chess_ui_command){.kind=CHESS_CMD_RESUME});
    assert(requests == 2 && s_thinking);
    assert(request.position.side_to_move == CHESS_WHITE);
    /* A pending AI opening must restart after power-on, with the same
     * selected color and difficulty rather than silently reverting. */
    reset(); choose_difficulty_color(2, true);
    requests = 0; busy = false; s_thinking = false;
    s_human_color = CHESS_WHITE; s_mode = CHESS_MODE_LOCAL;
    s_difficulty = CHESS_AI_EASY;
    boot_load(); render_all();
    assert(requests == 1 && request.level == CHESS_AI_HARD);
    assert(request.position.side_to_move == CHESS_WHITE);
    assert(rendered.snapshot.bottom == CHESS_BLACK);
    assert(chess_save_load(&s_backend, &loaded) == CHESS_OK);
    assert(loaded.settings.difficulty == CHESS_DIFF_HARD);

    /* Backing out of color selection retains the old game and settings. */
    reset(); choose_difficulty_color(2, false);
    s_screen = APP_HOME; s_home_mode = 0; s_home_idx = 2;
    click(BSP_BTN_OK); click(BSP_BTN_OK); /* pending Easy -> colors */
    on_input(BSP_BTN_OK, BSP_BTN_LONG);
    assert(s_home_mode == 1 && s_difficulty == CHESS_AI_HARD);
    assert(chess_save_load(&s_backend, &loaded) == CHESS_OK);
    assert(loaded.human_color == CHESS_WHITE);
    assert(loaded.settings.difficulty == CHESS_DIFF_HARD);
    assert(requests == 0);
    /* New game starts with a mode chooser, not an immediate local reset.
     * Cancelling setup must leave the current AI game intact. */
    reset(); choose_color(true); deliver_move(12,28);
    uint64_t generation = s_generation;
    handle_command((chess_ui_command){.kind=CHESS_CMD_NEW_GAME}); render_all();
    assert(rendered.screen == CHESS_VIEW_HOME && rendered.nmenu == 2);
    assert(s_game.position.side_to_move == CHESS_BLACK);
    if (s_generation != generation) {
        fputs("generation changed after new-game prompt\n", stderr);
        return 1;
    }
    on_input(BSP_BTN_OK,BSP_BTN_LONG);
    on_input(BSP_BTN_OK,BSP_BTN_CLICK); /* suppressed long-release */
    assert(s_screen == APP_BOARD && s_mode == CHESS_MODE_AI);
    assert(s_human_color == CHESS_BLACK);
    if (s_generation != generation) {
        fputs("generation changed after cancel setup\n", stderr);
        return 1;
    }
    handle_command((chess_ui_command){.kind=CHESS_CMD_NEW_GAME}); render_all();
    click(BSP_BTN_DOWN); click(BSP_BTN_OK); /* choose AI, difficulty */
    assert(rendered.nmenu == 3);
    click(BSP_BTN_DOWN); click(BSP_BTN_DOWN); click(BSP_BTN_OK); /* Hard */
    assert(rendered.nmenu == 2);
    click(BSP_BTN_OK); /* White */
    assert(s_mode == CHESS_MODE_AI && s_difficulty == CHESS_AI_HARD);
    assert(s_human_color == CHESS_WHITE && rendered.snapshot.bottom == CHESS_WHITE);
    assert(!s_thinking && s_game.position.side_to_move == CHESS_WHITE);
    handle_command((chess_ui_command){.kind=CHESS_CMD_NEW_GAME});render_all();
    click(BSP_BTN_OK); /* local */
    assert(s_mode == CHESS_MODE_LOCAL && s_game.position.side_to_move == CHESS_WHITE);
    puts("PASS: player color, AI opening, input ownership and save restore");
    return 0;
}
