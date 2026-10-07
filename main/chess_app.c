/* Chess application owner. Big temporaries are file-static: the main
 * task stack is small and a chess_save alone is ~5.5 KiB. */
#include "chess_app.h"

#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_display.h"
#include "chess_ai_task.h"
#include "chess_board_draw.h"
#include "chess_core.h"
#include "chess_font.h"
#include "chess_i18n.h"
#include "chess_storage.h"
#include "chess_ui.h"
#include "chess_ui_model.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "chess-app";

#define INPUT_QUEUE_DEPTH 16

/* NVS backend constructor lives in chess_nvs_esp.c. */
chess_nvs_backend chess_nvs_esp_backend(void);

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

typedef enum app_screen {
    APP_BOARD = 0,
    APP_HOME,
    APP_OVER,
} app_screen_t;

/* Error codes shown on the model error page. */
#define ERR_LOAD_VERSION 0xE001u
#define ERR_LOAD_CORRUPT 0xE002u
#define ERR_LOAD_RETRY_FAILED 0xE003u

static QueueHandle_t s_queue;
static chess_game s_game;
static chess_ui_model s_model;
static chess_nvs_backend s_backend;
static bool s_backend_ready;
static app_screen_t s_screen;
static uint64_t s_generation;
static uint64_t s_seq;
static bool s_saved;
static bool s_battery_ok;
static bool s_suppress_click;
static char s_note[64];
static chess_move s_last_move;
static bool s_has_last_move;
static chess_view s_view;
static chess_save s_scratch;
static chess_language s_language = CHESS_LANGUAGE_ENGLISH;

static const char *app_text(chess_text_id text) {
    return chess_i18n_get(s_language, text);
}

/* Forward declarations: input/render paths precede their helpers. */
static const char *level_name(chess_ai_level level);
static void after_move_applied(chess_move m);
static void feed_model_moves(void);
static void maybe_request_ai(void);
/* AI owns the side opposite the saved human color. */
static uint8_t s_mode;
static chess_ai_level s_difficulty;
static chess_color s_human_color = CHESS_WHITE;
static chess_ai_level s_new_difficulty;
static bool s_thinking;
static bool s_cancel_await;
static uint64_t s_cancel_at_ms;
static bool s_cancel_blocked;
static unsigned s_home_mode; /* 0 main, 1 difficulty, 2 human color, 3 new-game mode */
static unsigned s_home_idx;
static bool s_new_game_setup;

static const char *cmd_label(chess_ui_cmd cmd) {
    switch (cmd) {
    case CHESS_CMD_RESUME:
        return app_text(CHESS_TEXT_RESUME);
    case CHESS_CMD_CLAIM_CURRENT:
        return app_text(CHESS_TEXT_CLAIM_DRAW);
    case CHESS_CMD_CLAIM_PRESELECT:
        return app_text(CHESS_TEXT_CLAIM_SELECTED);
    case CHESS_CMD_RESIGN:
        return app_text(CHESS_TEXT_RESIGN);
    case CHESS_CMD_NEW_GAME:
        return app_text(CHESS_TEXT_NEW_GAME);
    case CHESS_CMD_GO_HOME:
        return app_text(CHESS_TEXT_GO_HOME);
    default:
        return "?";
    }
}

static void square_name(uint8_t sq, char out[3]) {
    out[0] = (char)('a' + sq % 8u);
    out[1] = (char)('1' + sq / 8u);
    out[2] = '\0';
}

static char piece_letter(chess_piece_type t) {
    switch (t) {
    case CHESS_PAWN:
        return 'P';
    case CHESS_KNIGHT:
        return 'N';
    case CHESS_BISHOP:
        return 'B';
    case CHESS_ROOK:
        return 'R';
    case CHESS_QUEEN:
        return 'Q';
    case CHESS_KING:
        return 'K';
    default:
        return '?';
    }
}

static void feed_model_moves(void) {
    chess_move moves[CHESS_MAX_MOVES];
    size_t count = 0;
    if (chess_generate_legal(&s_game.position, moves, CHESS_MAX_MOVES,
                             &count) != CHESS_OK) {
        count = 0;
    }
    chess_ui_model_set_moves(&s_model, count > 0 ? moves : NULL, count);
}

/* Header: side + battery + save flag. Footer: selection + hints/notes. */
static void build_header_footer(void) {
    const chess_position *pos = &s_game.position;
    int soc = s_battery_ok ? bsp_battery_soc() : -1;
    char focus[8] = "-";
    char target[8] = "-";
    const char *piece = "";
    char sel[32];
    static char pbuf[8];

    if (s_language == CHESS_LANGUAGE_CHINESE) {
        if (soc < 0) {
            snprintf(s_view.header, sizeof(s_view.header), "%s%s %s",
                     app_text(pos->side_to_move == CHESS_WHITE ? CHESS_TEXT_WHITE
                                                               : CHESS_TEXT_BLACK),
                     app_text(CHESS_TEXT_TO_MOVE),
                     app_text(s_saved ? CHESS_TEXT_SAVED : CHESS_TEXT_UNSAVED));
        } else {
            snprintf(s_view.header, sizeof(s_view.header), "%s%s %d%% %s",
                     app_text(pos->side_to_move == CHESS_WHITE ? CHESS_TEXT_WHITE
                                                               : CHESS_TEXT_BLACK),
                     app_text(CHESS_TEXT_TO_MOVE), soc,
                     app_text(s_saved ? CHESS_TEXT_SAVED : CHESS_TEXT_UNSAVED));
        }
    } else {
        if (soc < 0) {
            snprintf(s_view.header, sizeof(s_view.header), "%s %s -- %s",
                     app_text(pos->side_to_move == CHESS_WHITE ? CHESS_TEXT_WHITE
                                                               : CHESS_TEXT_BLACK),
                     app_text(CHESS_TEXT_TO_MOVE),
                     app_text(s_saved ? CHESS_TEXT_SAVED : CHESS_TEXT_UNSAVED));
        } else {
            snprintf(s_view.header, sizeof(s_view.header), "%s %s %d%% %s",
                     app_text(pos->side_to_move == CHESS_WHITE ? CHESS_TEXT_WHITE
                                                               : CHESS_TEXT_BLACK),
                     app_text(CHESS_TEXT_TO_MOVE), soc,
                     app_text(s_saved ? CHESS_TEXT_SAVED : CHESS_TEXT_UNSAVED));
        }
    }
    if (s_thinking) {
        snprintf(s_view.header, sizeof(s_view.header), "%s %s",
                 app_text(CHESS_TEXT_THINKING),
                 level_name(s_difficulty));
    }

    if (s_model.screen == CHESS_SCREEN_SELECT_PIECE && s_model.npieces > 0) {
        uint8_t sq = s_model.pieces[s_model.piece_idx];
        square_name(sq, focus);
        snprintf(pbuf, sizeof(pbuf), "%c",
                 piece_letter(pos->board[sq].type));
        piece = pbuf;
        snprintf(sel, sizeof(sel), "%s %s", focus, piece);
    } else if ((s_model.screen == CHESS_SCREEN_SELECT_TARGET ||
                s_model.screen == CHESS_SCREEN_PROMOTION) &&
               s_model.selected_piece != CHESS_NO_SQUARE &&
               s_model.ntargets > 0) {
        uint8_t from = s_model.selected_piece;
        uint8_t to = s_model.targets[s_model.target_idx];
        square_name(from, focus);
        square_name(to, target);
        if (s_model.screen == CHESS_SCREEN_PROMOTION) {
            snprintf(sel, sizeof(sel), "%s>%s=%c", focus, target,
                     piece_letter(s_model.promos[s_model.promo_idx]));
        } else {
            snprintf(sel, sizeof(sel), "%s>%s", focus, target);
        }
        piece = "";
    } else {
        snprintf(sel, sizeof(sel), "-");
    }
    (void)piece;
    if (s_note[0] != '\0') {
        snprintf(s_view.footer, sizeof(s_view.footer), "%s\n%s", sel, s_note);
    } else if (s_model.screen == CHESS_SCREEN_PROMOTION) {
        snprintf(s_view.footer, sizeof(s_view.footer), "%s\n%s", sel,
                 app_text(CHESS_TEXT_PROMOTION_HINT));
    } else {
        snprintf(s_view.footer, sizeof(s_view.footer), "%s\n%s", sel,
                 app_text(CHESS_TEXT_MOVE_HINT));
    }
}

static void build_snapshot(void) {
    chess_snapshot *snap = &s_view.snapshot;
    unsigned sq;
    chess_color mover = s_game.position.side_to_move;
    uint8_t king = (mover == CHESS_WHITE) ? s_game.position.white_king
                                          : s_game.position.black_king;
    for (sq = 0; sq < 64u; sq++) {
        snap->cells[sq] = s_game.position.board[sq];
    }
    snap->selected = CHESS_NO_SQUARE;
    snap->ntargets = 0;
    snap->hover = CHESS_NO_SQUARE;
    snap->check = CHESS_NO_SQUARE;
    snap->side = mover;
    snap->bottom = s_mode == CHESS_MODE_AI ? s_human_color : CHESS_WHITE;
    if (chess_is_attacked(&s_game.position, king,
                           (chess_color)(mover ^ 1u))) {
        snap->check = king;
    }
    if (s_has_last_move) {
        snap->last_from = s_last_move.from;
        snap->last_to = s_last_move.to;
    } else {
        snap->last_from = CHESS_NO_SQUARE;
        snap->last_to = CHESS_NO_SQUARE;
    }
    if (s_mode == CHESS_MODE_AI && mover != s_human_color) {
        return; /* No human focus/targets on the engine's turn. */
    }
    if (s_model.screen == CHESS_SCREEN_SELECT_PIECE && s_model.npieces > 0) {
        snap->hover = s_model.pieces[s_model.piece_idx];
    } else if (s_model.screen == CHESS_SCREEN_SELECT_TARGET ||
               s_model.screen == CHESS_SCREEN_PROMOTION) {
        unsigned i;
        snap->selected = s_model.selected_piece;
        for (i = 0; i < s_model.ntargets && i < 64u; i++) {
            snap->targets[i] = s_model.targets[i];
        }
        snap->ntargets = (uint8_t)s_model.ntargets;
        if (s_model.ntargets > 0) {
            snap->hover = s_model.targets[s_model.target_idx];
        }
    }
}

static void build_pause_menu(void) {
    unsigned i;
    s_view.nmenu = 0;
    for (i = 0; i < s_model.npause && i < CHESS_MENU_MAX; i++) {
        s_view.menu[i] = cmd_label(s_model.pause_items[i]);
        s_view.nmenu++;
    }
    s_view.menu_idx = (unsigned)s_model.pause_idx;
}

static void render_all(void) {
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "render without LVGL lock");
        return;
    }
    s_view.language = s_language;
    if (s_screen == APP_BOARD || s_model.screen == CHESS_SCREEN_PAUSE ||
        s_model.screen == CHESS_SCREEN_CONFIRM) {
        if (s_model.screen == CHESS_SCREEN_SELECT_PIECE ||
            s_model.screen == CHESS_SCREEN_SELECT_TARGET ||
            s_model.screen == CHESS_SCREEN_PROMOTION) {
            build_snapshot();
            build_header_footer();
            s_view.screen = CHESS_VIEW_BOARD;
        } else if (s_model.screen == CHESS_SCREEN_PAUSE) {
            build_pause_menu();
            s_view.screen = CHESS_VIEW_PAUSE;
        } else if (s_model.screen == CHESS_SCREEN_CONFIRM) {
            snprintf(s_view.confirm_action, sizeof(s_view.confirm_action),
                     "%s%s", cmd_label(s_model.confirm_action),
                     s_language == CHESS_LANGUAGE_CHINESE ? "？" : "?");
            s_view.confirm_yes = s_model.confirm_yes;
            s_view.screen = CHESS_VIEW_CONFIRM;
        } else {
            s_view.screen = CHESS_VIEW_BOARD;
            build_snapshot();
            build_header_footer();
        }
    } else if (s_screen == APP_HOME) {
        if (s_home_mode == 0) {
            s_view.menu[0] = app_text(CHESS_TEXT_CONTINUE);
            s_view.menu[1] = app_text(CHESS_TEXT_NEW_LOCAL);
            s_view.menu[2] = app_text(CHESS_TEXT_NEW_AI);
            s_view.menu[3] = app_text(CHESS_TEXT_LANGUAGE);
            s_view.nmenu = 4;
        } else if (s_home_mode == 1) {
            s_view.menu[0] = app_text(CHESS_TEXT_EASY);
            s_view.menu[1] = app_text(CHESS_TEXT_NORMAL);
            s_view.menu[2] = app_text(CHESS_TEXT_HARD);
            s_view.nmenu = 3;
        } else if (s_home_mode == 2) {
            s_view.menu[0] = app_text(CHESS_TEXT_PLAY_WHITE);
            s_view.menu[1] = app_text(CHESS_TEXT_PLAY_BLACK);
            s_view.nmenu = 2;
        } else {
            s_view.menu[0] = app_text(CHESS_TEXT_NEW_LOCAL);
            s_view.menu[1] = app_text(CHESS_TEXT_NEW_AI);
            s_view.nmenu = 2;
        }
        s_view.menu_idx = s_home_idx;
        s_view.screen = CHESS_VIEW_HOME;
    } else {
        chess_status st = chess_game_status(&s_game);
        chess_text_id outcome =
            st == CHESS_STATUS_CHECKMATE_WHITE_WINS ||
                    st == CHESS_STATUS_RESIGN_WHITE_WINS
                ? CHESS_TEXT_WHITE_WINS
            : st == CHESS_STATUS_CHECKMATE_BLACK_WINS ||
                      st == CHESS_STATUS_RESIGN_BLACK_WINS
                ? CHESS_TEXT_BLACK_WINS
            : st == CHESS_STATUS_STALEMATE ? CHESS_TEXT_STALEMATE
            : st == CHESS_STATUS_DRAW_DEAD ? CHESS_TEXT_DEAD_POSITION
            : st == CHESS_STATUS_DRAW_FIVEFOLD ? CHESS_TEXT_FIVEFOLD_DRAW
            : st == CHESS_STATUS_DRAW_SEVENTY_FIVE ? CHESS_TEXT_SEVENTY_FIVE_DRAW
            : st == CHESS_STATUS_DRAW_CLAIMED_THREEFOLD ||
                      st == CHESS_STATUS_DRAW_CLAIMED_FIFTY
                ? CHESS_TEXT_DRAW_CLAIMED
            : st == CHESS_STATUS_DRAW_AGREED ? CHESS_TEXT_DRAW_AGREED
                                             : CHESS_TEXT_GAME_OVER;
        snprintf(s_view.over, sizeof(s_view.over), "%s", app_text(outcome));
        s_view.screen = CHESS_VIEW_OVER;
    }
    chess_ui_render(&s_view);
    bsp_lvgl_unlock();
}

static bool persist_game(void) {
    chess_save *save = &s_scratch;
    memset(save, 0, sizeof(*save));
    save->game = s_game;
    save->settings.difficulty = (uint8_t)s_difficulty;
    save->settings.language = (uint8_t)s_language;
    save->settings.brightness = 80;
    save->mode = s_mode;
    save->human_color = (uint8_t)s_human_color;
    save->seq = s_seq;
    if (!s_backend_ready ||
        chess_save_store(&s_backend, save) != CHESS_OK) {
        s_saved = false;
        snprintf(s_note, sizeof(s_note), "%s", app_text(CHESS_TEXT_SAVE_FAILED));
        return false;
    }
    s_seq++;
    s_saved = true;
    s_note[0] = '\0';
    return true;
}

static void start_fresh_game(void) {
    chess_game_init_fen(&s_game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR "
                                 "w KQkq - 0 1");
    s_has_last_move = false;
    s_note[0] = '\0';
    s_mode = CHESS_MODE_LOCAL;
    s_human_color = CHESS_WHITE;
    s_thinking = false;
    s_cancel_await = false;
    s_cancel_blocked = false;
    s_generation++;
    feed_model_moves();
    persist_game();
}

static void start_ai_game(chess_ai_level level, chess_color human_color) {
    start_fresh_game();
    s_mode = CHESS_MODE_AI;
    s_difficulty = level;
    s_human_color = human_color;
    ESP_LOGI(TAG, "AI game human=%s level=%u",
             human_color == CHESS_WHITE ? "white" : "black", (unsigned)level);
    persist_game();
    maybe_request_ai();
}

static const char *level_name(chess_ai_level level) {
    switch (level) {
    case CHESS_AI_EASY:
        return app_text(CHESS_TEXT_EASY);
    case CHESS_AI_NORMAL:
        return app_text(CHESS_TEXT_NORMAL);
    case CHESS_AI_HARD:
        return app_text(CHESS_TEXT_HARD);
    default:
        return "?";
    }
}

/* Ask the worker whenever the ongoing position belongs to the AI.
 * Also handles White's opening and resumed searches; no search
 * is already running or blocked. */
static void maybe_request_ai(void) {
    chess_ai_request req;
    if (s_mode != CHESS_MODE_AI || s_thinking || s_cancel_blocked) {
        return;
    }
    if (chess_game_status(&s_game) != CHESS_STATUS_ONGOING) {
        return;
    }
    if (s_game.position.side_to_move == s_human_color) {
        return;
    }
    if (chess_ai_task_busy()) {
        return;
    }
    memset(&req, 0, sizeof(req));
    req.position = s_game.position;
    req.generation = s_generation;
    req.level = s_difficulty;
    chess_ai_level_budgets(s_difficulty, &req.deadline_ms, &req.node_max,
                           &req.depth_max);
    req.easy_seed = (uint32_t)(s_generation * 2654435761u);
    if (!chess_ai_task_request(&req)) {
        return;
    }
    ESP_LOGI(TAG, "AI search side=%s gen=%llu",
             req.position.side_to_move == CHESS_WHITE ? "white" : "black",
             (unsigned long long)req.generation);
    s_thinking = true;
    s_note[0] = '\0';
}

static void open_pause(void) {
    chess_ui_command ignored;
    feed_model_moves();
    memset(&ignored, 0, sizeof(ignored));
    chess_ui_model_event(&s_model, CHESS_EVT_LONG, &ignored);
}

static bool drain_ai_results(void) {
    chess_ai_result_event res;
    bool any = false;
    while (chess_ai_task_take_result(&res)) {
        bool was_awaiting;
        any = true;
        s_thinking = false;
        was_awaiting = s_cancel_await;
        s_cancel_await = false;
        if (res.outcome == CHESS_AI_CANCELLED) {
            /* Any arrival releases the cancel hold; only a live
             * await reroutes to pause (a raced OK applies below). */
            s_cancel_blocked = false;
            if (was_awaiting) {
                open_pause();
                s_screen = APP_BOARD;
            }
            continue;
        }
        if (res.outcome == CHESS_AI_OK && res.has_move) {
            if (chess_ai_apply_checked(&s_game, res.move, res.generation,
                                       s_generation) == CHESS_OK) {
                after_move_applied(res.move);
                continue;
            }
            /* Stale/illegal/terminal: drop silently, already handled. */
            continue;
        }
        if ((res.outcome == CHESS_AI_TIMEOUT ||
             res.outcome == CHESS_AI_ENGINE_ERROR) &&
            res.has_fallback) {
            if (chess_ai_apply_checked(&s_game, res.fallback, res.generation,
                                       s_generation) == CHESS_OK) {
                snprintf(s_note, sizeof(s_note), "%s",
                         app_text(CHESS_TEXT_AI_FALLBACK));
                after_move_applied(res.fallback);
                continue;
            }
        }
        /* CANCELLED without await, NO_MOVE, or failed fallback:
         * nothing to apply; refresh below shows the true state. */
    }
    return any;
}

static uint64_t now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void after_move_applied(chess_move m) {
    s_last_move = m;
    s_has_last_move = true;
    s_generation++;
    feed_model_moves();
    persist_game();
    if (chess_game_status(&s_game) != CHESS_STATUS_ONGOING) {
        s_screen = APP_OVER;
        return;
    }
    maybe_request_ai();
}

static void handle_command(chess_ui_command cmd) {
    chess_error err;
    switch (cmd.kind) {
    case CHESS_CMD_NONE:
        break;
    case CHESS_CMD_SUBMIT_MOVE:
        if (s_mode == CHESS_MODE_AI &&
            s_game.position.side_to_move != s_human_color) {
            return;
        }
        err = chess_game_apply(&s_game, cmd.move);
        if (err == CHESS_OK) {
            after_move_applied(cmd.move);
        } else {
            snprintf(s_note, sizeof(s_note), "%s", app_text(CHESS_TEXT_REJECTED));
        }
        break;
    case CHESS_CMD_CLAIM_CURRENT:
        err = chess_game_claim_draw(&s_game, NULL);
        if (err == CHESS_OK) {
            persist_game();
            s_screen = APP_OVER;
        } else {
            snprintf(s_note, sizeof(s_note), "%s", app_text(CHESS_TEXT_NO_CLAIM));
        }
        break;
    case CHESS_CMD_CLAIM_PRESELECT:
        err = chess_game_claim_draw(&s_game, &cmd.move);
        if (err == CHESS_OK) {
            persist_game();
            s_screen = APP_OVER;
        } else {
            snprintf(s_note, sizeof(s_note), "%s", app_text(CHESS_TEXT_NO_CLAIM));
        }
        break;
    case CHESS_CMD_RESIGN:
        if (chess_game_resign(&s_game, s_mode == CHESS_MODE_AI
                                      ? s_human_color
                                      : s_game.position.side_to_move) ==
            CHESS_OK) {
            persist_game();
            s_screen = APP_OVER;
        }
        break;
    case CHESS_CMD_NEW_GAME:
        s_screen = APP_HOME;
        s_home_mode = 3;
        s_home_idx = 0;
        s_new_game_setup = true;
        break;
    case CHESS_CMD_GO_HOME:
        s_screen = APP_HOME;
        break;
    case CHESS_CMD_RESUME:
        s_screen = APP_BOARD;
        maybe_request_ai();
        break;
    case CHESS_CMD_ERROR_RETRY:
        /* Retry boot load (compat path); only reachable from boot. */
        s_note[0] = '\0';
        break;
    case CHESS_CMD_ERROR_BACK:
        s_screen = APP_HOME;
        break;
    }
}

/* Boot load: strict first; unknown-version and corrupt land on the
 * model error page (retry = compat load, new = fresh, back = home). */
static void boot_load(void) {
    chess_save *save = &s_scratch;
    chess_error err;
    if (!s_backend_ready) {
        start_fresh_game();
        return;
    }
    memset(save, 0, sizeof(*save));
    err = chess_save_load(&s_backend, save);
    if (err == CHESS_OK) {
        s_game = save->game;
        s_language = chess_i18n_normalize(save->settings.language);
        s_seq = save->seq + 1;
        s_saved = true;
        s_mode = (save->mode == CHESS_MODE_AI) ? CHESS_MODE_AI
                                                : CHESS_MODE_LOCAL;
        s_human_color = (chess_color)save->human_color;
        s_difficulty = (chess_ai_level)save->settings.difficulty;
        s_generation++;
        feed_model_moves();
        if (chess_game_status(&s_game) != CHESS_STATUS_ONGOING) {
            s_screen = APP_OVER;
        } else {
            maybe_request_ai();
        }
        return;
    }
    if (err == CHESS_ERR_NO_SAVE) {
        start_fresh_game();
        return;
    }
    if (err == CHESS_ERR_UNKNOWN_VERSION) {
        chess_ui_model_set_error(&s_model, ERR_LOAD_VERSION);
    } else {
        chess_ui_model_set_error(&s_model, ERR_LOAD_CORRUPT);
    }
    /* Fresh game underneath so NEW/BACK always work. */
    start_fresh_game();
    s_saved = false;
}

static chess_ui_event map_event(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev == BSP_BTN_LONG) {
        return CHESS_EVT_LONG;
    }
    if (ev != BSP_BTN_CLICK) {
        return CHESS_EVT_RELEASE;
    }
    switch (btn) {
    case BSP_BTN_UP:
        return CHESS_EVT_UP;
    case BSP_BTN_DOWN:
        return CHESS_EVT_DOWN;
    case BSP_BTN_OK:
    default:
        return CHESS_EVT_OK;
    }
}

static void on_input(bsp_btn_t btn, bsp_btn_ev_t ev) {
    chess_ui_command cmd;
    memset(&cmd, 0, sizeof(cmd));
    if (ev == BSP_BTN_LONG) {
        s_suppress_click = true;
    } else if (ev == BSP_BTN_CLICK && s_suppress_click) {
        s_suppress_click = false;
        ESP_LOGI(TAG, "in suppressed release-click");
        return;
    }
    /* Serial evidence for the V0 input gate (one line per action). */
    ESP_LOGI(TAG, "in btn=%d ev=%d screen=%d gen=%llu", (int)btn, (int)ev,
             (int)s_screen, (unsigned long long)s_generation);
    if (s_screen == APP_HOME) {
        /* New AI chooses difficulty, then human color. LONG backs out
         * without changing the saved game or its settings. */
        if (s_home_mode == 0) {
            if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
                if (s_home_idx == 1) {
                    start_fresh_game();
                } else if (s_home_idx == 2) {
                    s_new_game_setup = false;
                    s_home_mode = 1;
                    s_home_idx = 0;
                    render_all();
                    return;
                } else if (s_home_idx == 3) {
                    s_language = chess_i18n_toggle((uint8_t)s_language);
                    persist_game();
                    render_all();
                    return;
                }
                s_screen = APP_BOARD;
                maybe_request_ai();
            } else if (ev == BSP_BTN_CLICK) {
                s_home_idx = (s_home_idx + (btn == BSP_BTN_DOWN ? 1u : 3u)) % 4u;
            } else if (ev == BSP_BTN_LONG) {
                s_screen = APP_BOARD;
                maybe_request_ai();
            } else {
                return;
            }
        } else if (s_home_mode == 1) {
            if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
                if (s_home_idx < 3) {
                    s_new_difficulty = (chess_ai_level)s_home_idx;
                    s_home_mode = 2;
                    s_home_idx = 0;
                }
            } else if (ev == BSP_BTN_CLICK) {
                s_home_idx = (s_home_idx + (btn == BSP_BTN_DOWN ? 1u : 2u)) % 3u;
            } else if (ev == BSP_BTN_LONG) {
                s_home_mode = s_new_game_setup ? 3u : 0u;
                s_home_idx = s_new_game_setup ? 1u : 0u;
            } else {
                return;
            }
        } else if (s_home_mode == 2) {
            if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
                start_ai_game(s_new_difficulty,
                              s_home_idx == 0 ? CHESS_WHITE : CHESS_BLACK);
                s_screen = APP_BOARD;
                s_home_mode = 0;
                s_home_idx = 0;
                s_new_game_setup = false;
            } else if (ev == BSP_BTN_CLICK) {
                s_home_idx ^= 1u;
            } else if (ev == BSP_BTN_LONG) {
                s_home_mode = 1;
                s_home_idx = (unsigned)s_new_difficulty;
            } else {
                return;
            }
        } else {
            if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
                if (s_home_idx == 0) {
                    start_fresh_game();
                    s_screen = APP_BOARD;
                    s_home_mode = 0;
                    s_home_idx = 0;
                    s_new_game_setup = false;
                } else {
                    s_home_mode = 1;
                    s_home_idx = 0;
                }
            } else if (ev == BSP_BTN_CLICK) {
                s_home_idx ^= 1u;
            } else if (ev == BSP_BTN_LONG) {
                s_screen = APP_BOARD;
                s_home_mode = 0;
                s_home_idx = 0;
                s_new_game_setup = false;
                maybe_request_ai();
            } else {
                return;
            }
        }
        render_all();
        return;
    }
    if (s_thinking) {
        /* THINKING: everything ignored except LONG-cancel. */
        if (ev == BSP_BTN_LONG) {
            chess_ai_task_cancel();
            s_cancel_await = true;
            s_cancel_at_ms = now_ms();
        }
        return;
    }
    if (s_screen == APP_OVER) {
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
            handle_command((chess_ui_command){.kind = CHESS_CMD_NEW_GAME});
        } else if (ev == BSP_BTN_LONG) {
            s_screen = APP_HOME;
        } else {
            return;
        }
        render_all();
        return;
    }
    if (chess_ui_model_event(&s_model, map_event(btn, ev), &cmd) !=
        CHESS_OK) {
        return;
    }
    if (cmd.kind == CHESS_CMD_ERROR_RETRY) {
        /* Compat boot load, then resume where it lands. */
        chess_save *save = &s_scratch;
        memset(save, 0, sizeof(*save));
        if (s_backend_ready &&
            chess_save_load_compat(&s_backend, save) == CHESS_OK) {
            s_game = save->game;
            s_language = chess_i18n_normalize(save->settings.language);
            s_seq = save->seq + 1;
            s_saved = true;
            s_mode = save->mode;
            s_human_color = (chess_color)save->human_color;
            s_difficulty = (chess_ai_level)save->settings.difficulty;
            s_generation++;
            feed_model_moves();
            s_screen = APP_BOARD;
            maybe_request_ai();
        } else {
            chess_ui_model_set_error(&s_model, ERR_LOAD_RETRY_FAILED);
        }
        render_all();
        return;
    }
    handle_command(cmd);
    render_all();
}

void chess_app_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    input_event_t in;
    (void)user;
    if (s_queue == NULL) {
        return;
    }
    in.btn = btn;
    in.event = ev;
    (void)xQueueSend(s_queue, &in, 0);
}

void chess_app_start(void) {
    input_event_t in;
    s_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "事件队列创建失败");
        return;
    }
    s_backend = chess_nvs_esp_backend();
    s_backend_ready = true;
    s_screen = APP_BOARD;
    s_generation = 1;
    s_seq = 1;
    s_saved = true;
    s_suppress_click = false;
    s_note[0] = '\0';
    s_has_last_move = false;
    s_mode = CHESS_MODE_LOCAL;
    s_difficulty = CHESS_AI_NORMAL;
    s_thinking = false;
    s_cancel_await = false;
    s_cancel_blocked = false;
    s_home_mode = 0;
    s_home_idx = 0;
    chess_ui_model_init(&s_model);
    if (!chess_ai_task_start()) {
        ESP_LOGW(TAG, "AI worker failed to start; AI games unavailable");
    }
    boot_load();
    render_all();
    ESP_LOGI(TAG, "象棋应用就绪 gen=%llu", (unsigned long long)s_generation);
    for (;;) {
        if (xQueueReceive(s_queue, &in, pdMS_TO_TICKS(50)) == pdTRUE) {
            /* on_input renders on every consumed event. */
            on_input(in.btn, in.event);
            continue;
        }
        /* Idle poll: engine results and the cancel watchdog only. */
        if (!drain_ai_results()) {
            if (s_cancel_await && now_ms() - s_cancel_at_ms > 200u) {
                /* Cancel ACK overdue: say so and hold new searches
                 * until the worker lands. */
                s_cancel_await = false;
                s_cancel_blocked = true;
                snprintf(s_note, sizeof(s_note), "%s",
                         app_text(CHESS_TEXT_CANCEL_SLOW));
                render_all();
            }
            continue;
        }
        render_all();
    }
}
