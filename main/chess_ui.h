/* Chess screen renderer (Task 8): one render entry takes a view value
 * and rebuilds the active screen. Call with the LVGL lock held.
 * Screens: BOARD (header + board + footer), PAUSE (menu list),
 * CONFIRM (cancel/confirm), OVER (result + hints), ERROR (code +
 * retry/new/back), HOME (continue/new local/new AI/language). Text is selected by
 * language; model and chess rules remain locale-neutral.
 */
#ifndef CHESS_UI_H
#define CHESS_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chess_board_draw.h"
#include "chess_core.h"
#include "chess_i18n.h"
#include "chess_ui_model.h"

#define CHESS_MENU_MAX 7
#define CHESS_TEXT_HEADER 48
#define CHESS_TEXT_FOOTER 96

typedef enum chess_view_screen {
    CHESS_VIEW_BOARD = 0,
    CHESS_VIEW_PAUSE,
    CHESS_VIEW_CONFIRM,
    CHESS_VIEW_OVER,
    CHESS_VIEW_ERROR,
    CHESS_VIEW_HOME,
    CHESS_VIEW_BRIGHTNESS
} chess_view_screen;

typedef struct chess_view {
    chess_view_screen screen;
    chess_language language;
    chess_snapshot snapshot; /* BOARD only */
    char header[CHESS_TEXT_HEADER];
    char footer[CHESS_TEXT_FOOTER];
    /* PAUSE: labels + focus. CONFIRM: action label + yes focus.
     * OVER: result line. ERROR: code. HOME: static. */
    const char *menu[CHESS_MENU_MAX];
    unsigned nmenu;
    unsigned menu_idx;
    char confirm_action[48];
    bool confirm_yes;
    char over[CHESS_TEXT_HEADER];
    uint16_t error_code;
    uint8_t brightness;
} chess_view;

void chess_ui_render(const chess_view *view);

#endif /* CHESS_UI_H */
