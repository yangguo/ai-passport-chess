/* Task 6 RED: three-button selection model event sequences.
 * Expected to FAIL to compile: chess_ui_model.h does not exist yet. */
#include <stdio.h>
#include <string.h>

#include "chess_core.h"
#include "chess_ui_model.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      failures++;                                                            \
    }                                                                        \
  } while (0)

#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
#define PROMO_FEN "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1"

/* Load the mover's legal set from the core (the app feeds these in). */
static size_t load_moves(const char *fen, chess_move *out) {
  chess_position pos;
  size_t count = 0;
  if (chess_position_from_fen(&pos, fen) != CHESS_OK) {
    return 0;
  }
  if (chess_generate_legal(&pos, out, CHESS_MAX_MOVES, &count) != CHESS_OK) {
    return 0;
  }
  return count;
}

static chess_ui_command send(chess_ui_model *m, chess_ui_event ev) {
  chess_ui_command cmd;
  memset(&cmd, 0, sizeof(cmd));
  CHECK(chess_ui_model_event(m, ev, &cmd) == CHESS_OK);
  return cmd;
}

static void test_wrap_around(void) {
  chess_ui_model m;
  chess_move moves[CHESS_MAX_MOVES];
  size_t n = load_moves(START_FEN, moves);
  CHECK(n == 20);
  CHECK(chess_ui_model_init(&m) == CHESS_OK);
  CHECK(chess_ui_model_set_moves(&m, moves, n) == CHESS_OK);
  /* Pieces ascending: b1(1) g1(6) a2(8) ... h2(15): 10 pieces. */
  CHECK(m.npieces == 10);
  CHECK(m.pieces[0] == 1);
  send(&m, CHESS_EVT_UP); /* wrap to last */
  CHECK(m.piece_idx == 9 && m.pieces[9] == 15);
  send(&m, CHESS_EVT_DOWN); /* wrap back to first */
  CHECK(m.piece_idx == 0);
  send(&m, CHESS_EVT_DOWN);
  CHECK(m.piece_idx == 1 && m.pieces[1] == 6);
}

static void test_empty_candidates_never_modulo(void) {
  chess_ui_model m;
  chess_ui_command cmd;
  CHECK(chess_ui_model_init(&m) == CHESS_OK);
  CHECK(chess_ui_model_set_moves(&m, NULL, 0) == CHESS_OK);
  cmd = send(&m, CHESS_EVT_UP);
  CHECK(cmd.kind == CHESS_CMD_NONE);
  cmd = send(&m, CHESS_EVT_DOWN);
  CHECK(cmd.kind == CHESS_CMD_NONE);
  cmd = send(&m, CHESS_EVT_OK);
  CHECK(cmd.kind == CHESS_CMD_NONE);
  /* LONG still opens pause (per the state table); the point is no
   * modulo-by-zero and no phantom commands on any event. */
  cmd = send(&m, CHESS_EVT_LONG);
  CHECK(cmd.kind == CHESS_CMD_NONE);
  CHECK(m.screen == CHESS_SCREEN_PAUSE);
  cmd = send(&m, CHESS_EVT_LONG);
  CHECK(cmd.kind == CHESS_CMD_RESUME);
  CHECK(m.screen == CHESS_SCREEN_SELECT_PIECE);
}

static void test_submit_normal_move(void) {
  chess_ui_model m;
  chess_move moves[CHESS_MAX_MOVES];
  size_t n = load_moves(START_FEN, moves);
  chess_ui_command cmd;
  CHECK(chess_ui_model_init(&m) == CHESS_OK);
  CHECK(chess_ui_model_set_moves(&m, moves, n) == CHESS_OK);
  /* e2(12) is piece index 6 in the sorted list. */
  {
    int i;
    for (i = 0; i < 6; i++) {
      send(&m, CHESS_EVT_DOWN);
    }
  }
  CHECK(m.pieces[m.piece_idx] == 12);
  cmd = send(&m, CHESS_EVT_OK);
  CHECK(cmd.kind == CHESS_CMD_NONE);
  CHECK(m.screen == CHESS_SCREEN_SELECT_TARGET);
  CHECK(m.ntargets == 2); /* e3, e4 */
  cmd = send(&m, CHESS_EVT_OK); /* first target: e3 */
  CHECK(cmd.kind == CHESS_CMD_SUBMIT_MOVE);
  CHECK(cmd.move.from == 12 && cmd.move.to == 20);
  CHECK(cmd.move.promotion == CHESS_EMPTY);
}

static void test_promotion_flow_and_cancel(void) {
  chess_ui_model m;
  chess_move moves[CHESS_MAX_MOVES];
  size_t n = load_moves(PROMO_FEN, moves);
  chess_ui_command cmd;
  CHECK(chess_ui_model_init(&m) == CHESS_OK);
  CHECK(chess_ui_model_set_moves(&m, moves, n) == CHESS_OK);
  /* Pieces: e1(4), b7(49). */
  send(&m, CHESS_EVT_DOWN);
  CHECK(m.pieces[m.piece_idx] == 49);
  send(&m, CHESS_EVT_OK);
  CHECK(m.screen == CHESS_SCREEN_SELECT_TARGET);
  /* Promotion target deduplicated: b8 appears once, not four times. */
  CHECK(m.ntargets == 1 && m.targets[0] == 57);
  send(&m, CHESS_EVT_OK);
  CHECK(m.screen == CHESS_SCREEN_PROMOTION);
  /* Q -> R -> B -> N -> wrap to Q. */
  CHECK(m.promos[m.promo_idx] == CHESS_QUEEN);
  send(&m, CHESS_EVT_DOWN);
  CHECK(m.promos[m.promo_idx] == CHESS_ROOK);
  send(&m, CHESS_EVT_DOWN);
  send(&m, CHESS_EVT_DOWN);
  CHECK(m.promos[m.promo_idx] == CHESS_KNIGHT);
  send(&m, CHESS_EVT_DOWN);
  CHECK(m.promos[m.promo_idx] == CHESS_QUEEN);
  /* LONG cancels back to targets; nothing submitted. */
  cmd = send(&m, CHESS_EVT_LONG);
  CHECK(cmd.kind == CHESS_CMD_NONE);
  CHECK(m.screen == CHESS_SCREEN_SELECT_TARGET);
  CHECK(m.has_preselect);
  CHECK(m.preselect.from == 49 && m.preselect.to == 57);
  /* Moving focus clears the preselect. */
  send(&m, CHESS_EVT_LONG); /* back to pieces, keeps preselect */
  CHECK(m.screen == CHESS_SCREEN_SELECT_PIECE);
  CHECK(m.has_preselect);
  send(&m, CHESS_EVT_DOWN);
  CHECK(!m.has_preselect);
}

static void test_long_then_release_single_action(void) {
  chess_ui_model m;
  chess_move moves[CHESS_MAX_MOVES];
  size_t n = load_moves(START_FEN, moves);
  chess_ui_command cmd;
  CHECK(chess_ui_model_init(&m) == CHESS_OK);
  CHECK(chess_ui_model_set_moves(&m, moves, n) == CHESS_OK);
  send(&m, CHESS_EVT_DOWN); /* focus g1 */
  cmd = send(&m, CHESS_EVT_LONG); /* -> pause */
  CHECK(cmd.kind == CHESS_CMD_NONE);
  CHECK(m.screen == CHESS_SCREEN_PAUSE);
  cmd = send(&m, CHESS_EVT_RELEASE); /* swallowed: exactly one action */
  CHECK(cmd.kind == CHESS_CMD_NONE);
  CHECK(m.screen == CHESS_SCREEN_PAUSE);
  cmd = send(&m, CHESS_EVT_OK); /* resume is the first pause item */
  CHECK(cmd.kind == CHESS_CMD_RESUME);
  CHECK(m.screen == CHESS_SCREEN_SELECT_PIECE);
}

static void test_preselect_claim_never_submits(void) {
  chess_ui_model m;
  chess_move moves[CHESS_MAX_MOVES];
  size_t n = load_moves(PROMO_FEN, moves);
  chess_ui_command cmd;
  int submits = 0;
  CHECK(chess_ui_model_init(&m) == CHESS_OK);
  CHECK(chess_ui_model_set_moves(&m, moves, n) == CHESS_OK);
  send(&m, CHESS_EVT_DOWN); /* b7 */
  send(&m, CHESS_EVT_OK); /* targets */
  send(&m, CHESS_EVT_OK); /* promotion */
  send(&m, CHESS_EVT_LONG); /* back to targets, preselect armed */
  send(&m, CHESS_EVT_LONG); /* back to pieces */
  send(&m, CHESS_EVT_LONG); /* pause menu */
  CHECK(m.screen == CHESS_SCREEN_PAUSE);
  /* Items with preselect: resume, claim, preselect-claim, resign, ... */
  send(&m, CHESS_EVT_DOWN);
  send(&m, CHESS_EVT_DOWN);
  CHECK(m.pause_idx == 2);
  cmd = send(&m, CHESS_EVT_OK);
  CHECK(cmd.kind == CHESS_CMD_CLAIM_PRESELECT);
  CHECK(cmd.move.from == 49 && cmd.move.to == 57);
  CHECK(cmd.move.promotion == CHESS_QUEEN);
  CHECK(submits == 0);
  CHECK(m.screen == CHESS_SCREEN_PAUSE);
  /* The claim is one-shot: the entry is gone afterwards. */
  CHECK(!m.has_preselect);
}

static void test_confirm_defaults_cancel(void) {
  chess_ui_model m;
  chess_move moves[CHESS_MAX_MOVES];
  size_t n = load_moves(START_FEN, moves);
  chess_ui_command cmd;
  CHECK(chess_ui_model_init(&m) == CHESS_OK);
  CHECK(chess_ui_model_set_moves(&m, moves, n) == CHESS_OK);
  send(&m, CHESS_EVT_LONG); /* pause */
  /* Items: resume, claim, resign, new, home. Focus resign (index 2). */
  send(&m, CHESS_EVT_DOWN);
  send(&m, CHESS_EVT_DOWN);
  cmd = send(&m, CHESS_EVT_OK);
  CHECK(cmd.kind == CHESS_CMD_NONE);
  CHECK(m.screen == CHESS_SCREEN_CONFIRM);
  CHECK(!m.confirm_yes); /* default: cancel */
  cmd = send(&m, CHESS_EVT_OK);
  CHECK(cmd.kind == CHESS_CMD_NONE); /* cancelled, nothing emitted */
  CHECK(m.screen == CHESS_SCREEN_PAUSE);
  /* Re-enter, toggle to yes, confirm executes. */
  cmd = send(&m, CHESS_EVT_OK);
  CHECK(m.screen == CHESS_SCREEN_CONFIRM);
  send(&m, CHESS_EVT_DOWN);
  CHECK(m.confirm_yes);
  cmd = send(&m, CHESS_EVT_OK);
  CHECK(cmd.kind == CHESS_CMD_RESIGN);
}

static void test_error_page_and_return(void) {
  chess_ui_model m;
  chess_move moves[CHESS_MAX_MOVES];
  size_t n = load_moves(START_FEN, moves);
  chess_ui_command cmd;
  CHECK(chess_ui_model_init(&m) == CHESS_OK);
  CHECK(chess_ui_model_set_moves(&m, moves, n) == CHESS_OK);
  CHECK(chess_ui_model_set_error(&m, 0x1234) == CHESS_OK);
  CHECK(m.screen == CHESS_SCREEN_ERROR);
  CHECK(m.error_code == 0x1234);
  send(&m, CHESS_EVT_DOWN); /* -> new game */
  send(&m, CHESS_EVT_UP); /* wrap back to retry */
  cmd = send(&m, CHESS_EVT_OK);
  CHECK(cmd.kind == CHESS_CMD_ERROR_RETRY);
  CHECK(m.screen == CHESS_SCREEN_ERROR);
  cmd = send(&m, CHESS_EVT_LONG); /* back to the upper page */
  CHECK(cmd.kind == CHESS_CMD_ERROR_BACK);
  /* Fresh moves clear the error back to selection. */
  CHECK(chess_ui_model_set_moves(&m, moves, n) == CHESS_OK);
  CHECK(m.screen == CHESS_SCREEN_SELECT_PIECE);
}

static void test_null_args(void) {
  chess_ui_model m;
  chess_ui_command cmd;
  chess_move moves[CHESS_MAX_MOVES];
  size_t n = load_moves(START_FEN, moves);
  CHECK(chess_ui_model_init(NULL) == CHESS_ERR_NULL);
  CHECK(chess_ui_model_init(&m) == CHESS_OK);
  CHECK(chess_ui_model_set_moves(NULL, moves, n) == CHESS_ERR_NULL);
  CHECK(chess_ui_model_set_moves(&m, moves, n) == CHESS_OK);
  CHECK(chess_ui_model_event(NULL, CHESS_EVT_OK, &cmd) == CHESS_ERR_NULL);
  CHECK(chess_ui_model_event(&m, CHESS_EVT_OK, NULL) == CHESS_ERR_NULL);
  CHECK(chess_ui_model_set_error(NULL, 1) == CHESS_ERR_NULL);
}

int main(void) {
  test_wrap_around();
  test_empty_candidates_never_modulo();
  test_submit_normal_move();
  test_promotion_flow_and_cancel();
  test_long_then_release_single_action();
  test_preselect_claim_never_submits();
  test_confirm_defaults_cancel();
  test_error_page_and_return();
  test_null_args();

  if (failures == 0) {
    printf("PASS: all test_ui_model checks passed\n");
    return 0;
  }
  printf("FAILURES: %d\n", failures);
  return 1;
}
