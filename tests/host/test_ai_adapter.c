/* Task ai-adapter RED: suggest合法性 + 将杀兑现 + 只升后.
 * Expected to FAIL (link): chess_mcumax_adapter.c is a placeholder. */
#include <stdio.h>

#include "chess_ai.h"
#include "chess_core.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      failures++;                                                            \
    }                                                                        \
  } while (0)

static bool move_is_legal(const chess_position *pos, chess_move m) {
  chess_move legal[CHESS_MAX_MOVES];
  size_t count = 0;
  size_t i;
  if (chess_generate_legal(pos, legal, CHESS_MAX_MOVES, &count) != CHESS_OK) {
    return false;
  }
  for (i = 0; i < count; i++) {
    if (legal[i].from == m.from && legal[i].to == m.to &&
        legal[i].promotion == m.promotion) {
      return true;
    }
  }
  return false;
}

static void test_null_args(void) {
  chess_position pos;
  chess_move m;
  CHECK(chess_position_from_fen(
            &pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  CHECK(chess_ai_suggest(NULL, 100000, 3, &m) != 0);
  CHECK(chess_ai_suggest(&pos, 100000, 3, NULL) != 0);
}

static void test_suggest_startpos_legal(void) {
  chess_position pos;
  chess_move m;
  chess_game game;
  CHECK(chess_position_from_fen(
            &pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  CHECK(chess_ai_suggest(&pos, 200000, 3, &m) == 0);
  CHECK(move_is_legal(&pos, m));
  /* The suggestion must survive the real game path. */
  CHECK(chess_game_init(&game, &pos) == CHESS_OK);
  CHECK(chess_game_apply(&game, m) == CHESS_OK);
}

static void test_suggest_mate_in_one(void) {
  /* Black to move and mate: any reply that does not mate fails. */
  chess_position pos;
  chess_move m;
  chess_game game;
  CHECK(chess_position_from_fen(
            &pos, "rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2") ==
        CHESS_OK);
  CHECK(chess_ai_suggest(&pos, 200000, 4, &m) == 0);
  CHECK(chess_game_init(&game, &pos) == CHESS_OK);
  CHECK(chess_game_apply(&game, m) == CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_CHECKMATE_BLACK_WINS);
}

static void test_promotion_replies_are_queens(void) {
  /* White to move and promote: engine can only offer queens. */
  chess_position pos;
  chess_move m;
  CHECK(chess_position_from_fen(&pos, "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1") ==
        CHESS_OK);
  CHECK(chess_ai_suggest(&pos, 200000, 4, &m) == 0);
  CHECK(move_is_legal(&pos, m));
  if (m.to / 8u == 7u && pos.board[m.from].type == CHESS_PAWN) {
    CHECK(m.promotion == CHESS_QUEEN);
  }
}

int main(void) {
  test_null_args();
  test_suggest_startpos_legal();
  test_suggest_mate_in_one();
  test_promotion_replies_are_queens();

  if (failures == 0) {
    printf("PASS: all test_ai_adapter checks passed\n");
    return 0;
  }
  printf("FAILURES: %d\n", failures);
  return 1;
}
