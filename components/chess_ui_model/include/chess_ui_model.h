/* Three-button selection model (Task 6): deterministic, pure C11.
 * The app feeds the mover's legal list on every position change and
 * translates reducer commands (submit, claim, resign, navigation).
 * The model never mutates a chess position and never touches LVGL/BSP.
 *
 * Conventions (see docs/ui-interaction.md):
 * - UP/DOWN cycle with wrap-around; empty candidate lists ignore input
 *   (terminal routing is the app's job: never modulo an empty array).
 * - SELECT_TARGET lists each promotion square once; the piece choice
 *   lives in PROMOTION (fixed Q,R,B,N order, starts at queen).
 * - LONG backs out without submitting; from TARGET/PROMOTION it arms a
 *   one-shot preselect claim for the PAUSE menu. Focus moves clear it.
 * - RELEASE is always a no-op: the app must already have dropped any
 *   post-LONG release CLICK so one physical press is one logical action.
 * - CONFIRM defaults to cancel; ERROR offers retry/new-game and a
 *   long-press return to the upper page.
 */
#ifndef CHESS_UI_MODEL_H
#define CHESS_UI_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "chess_core.h"

typedef enum chess_ui_event {
  CHESS_EVT_UP = 0,
  CHESS_EVT_DOWN = 1,
  CHESS_EVT_OK = 2,
  CHESS_EVT_LONG = 3,
  CHESS_EVT_RELEASE = 4
} chess_ui_event;

typedef enum chess_ui_screen {
  CHESS_SCREEN_SELECT_PIECE = 0,
  CHESS_SCREEN_SELECT_TARGET = 1,
  CHESS_SCREEN_PROMOTION = 2,
  CHESS_SCREEN_PAUSE = 3,
  CHESS_SCREEN_CONFIRM = 4,
  CHESS_SCREEN_ERROR = 5
} chess_ui_screen;

typedef enum chess_ui_cmd {
  CHESS_CMD_NONE = 0,
  CHESS_CMD_SUBMIT_MOVE = 1,
  CHESS_CMD_CLAIM_CURRENT = 2,
  CHESS_CMD_CLAIM_PRESELECT = 3,
  CHESS_CMD_RESIGN = 4,
  CHESS_CMD_NEW_GAME = 5,
  CHESS_CMD_GO_HOME = 6,
  CHESS_CMD_RESUME = 7,
  CHESS_CMD_ERROR_RETRY = 8,
  CHESS_CMD_ERROR_BACK = 9,
  CHESS_CMD_BRIGHTNESS = 10
} chess_ui_cmd;

typedef struct chess_ui_command {
  chess_ui_cmd kind;
  chess_move move; /* set for SUBMIT_MOVE and CLAIM_PRESELECT */
} chess_ui_command;

#define CHESS_MODEL_MAX_MOVES 256u
#define CHESS_MODEL_MAX_CANDIDATES 64u
#define CHESS_MODEL_MAX_PAUSE_ITEMS 7u

typedef struct chess_ui_model {
  chess_ui_screen screen;
  chess_move legal[CHESS_MODEL_MAX_MOVES];
  size_t nlegal;
  uint8_t pieces[CHESS_MODEL_MAX_CANDIDATES];
  size_t npieces;
  size_t piece_idx;
  uint8_t targets[CHESS_MODEL_MAX_CANDIDATES];
  size_t ntargets;
  size_t target_idx;
  uint8_t selected_piece; /* CHESS_NO_SQUARE when none */
  chess_piece_type promos[4];
  size_t promo_idx;
  bool has_preselect;
  chess_move preselect;
  chess_ui_cmd pause_items[CHESS_MODEL_MAX_PAUSE_ITEMS];
  size_t npause;
  size_t pause_idx;
  chess_ui_cmd confirm_action; /* CHESS_CMD_RESIGN or CHESS_CMD_NEW_GAME */
  chess_ui_screen confirm_return; /* PAUSE or ERROR */
  bool confirm_yes; /* default false: cancel */
  uint16_t error_code;
  size_t error_idx; /* 0 retry, 1 new game */
} chess_ui_model;

chess_error chess_ui_model_init(chess_ui_model *m);

/* Replace the legal list (e.g. new position arrived): reselection starts
 * at SELECT_PIECE, preselect/confirm/error state is cleared. Too many
 * moves leaves the model untouched (CHESS_ERR_BUFFER_TOO_SMALL). */
chess_error chess_ui_model_set_moves(chess_ui_model *m,
                                     const chess_move *moves, size_t n);

/* Enter the error page with a display code. */
chess_error chess_ui_model_set_error(chess_ui_model *m, uint16_t code);

/* Feed one logical button event; the resulting command lands in *out
 * (CHESS_CMD_NONE when the event only navigates). */
chess_error chess_ui_model_event(chess_ui_model *m, chess_ui_event ev,
                                 chess_ui_command *out);

#endif /* CHESS_UI_MODEL_H */
