/* Task ai RED: engine board parsing and move-diff fixtures.
 * The startpos transcript is OBSERVED engine output (never invented);
 * board1 is synthesized in-test from it (labels the assumption).
 * Expected to FAIL to compile: chess_engine.h does not exist yet. */
#include <stdio.h>
#include <string.h>

#include "chess_core.h"
#include "chess_engine.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      failures++;                                                            \
    }                                                                        \
  } while (0)

/* Observed: first board printed by a fresh engine (startpos). */
static const char kBoard0[] =
    "rnbqkbnr\n"
    "++++++++\n"
    "........\n"
    "........\n"
    "........\n"
    "........\n"
    "********\n"
    "RNBQKBNR\n";

static void test_parse_startpos(void) {
  chess_piece board[64];
  chess_position ref;
  unsigned sq;
  CHECK(chess_engine_parse_board(kBoard0, sizeof(kBoard0) - 1, board) == 0);
  CHECK(chess_position_from_fen(
            &ref, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  for (sq = 0; sq < 64u; sq++) {
    if (board[sq].type != ref.board[sq].type ||
        (board[sq].type != CHESS_EMPTY &&
         board[sq].color != ref.board[sq].color)) {
      printf("FAIL %s:%d: square %u mismatch\n", __FILE__, __LINE__, sq);
      failures++;
    }
  }
}

static void test_parse_rejects_garbage(void) {
  chess_piece board[64];
  char bad[72];
  memset(bad, '.', sizeof(bad));
  bad[10] = 'X'; /* unknown glyph */
  CHECK(chess_engine_parse_board(bad, sizeof(bad), board) != 0);
  CHECK(chess_engine_parse_board(kBoard0, 71, board) != 0); /* short */
  CHECK(chess_engine_parse_board(NULL, 72, board) != 0);
  CHECK(chess_engine_parse_board(kBoard0, sizeof(kBoard0) - 1, NULL) != 0);
}

static void apply_text_move(const char before[72], char after[72],
                            unsigned from_sq, unsigned to_sq, char piece) {
  unsigned f = (from_sq % 8u) + (7u - from_sq / 8u) * 9u;
  unsigned t = (to_sq % 8u) + (7u - to_sq / 8u) * 9u;
  memcpy(after, before, 72);
  after[f] = '.';
  after[t] = piece;
}

static void test_diff_quiet_and_capture(void) {
  chess_piece prev[64];
  chess_piece cur[64];
  char b1[72];
  chess_move m;
  /* Synthetic f2f3 (12 -> 20) on top of the observed board. */
  memcpy(b1, kBoard0, 72);
  CHECK(chess_engine_parse_board(kBoard0, 72, prev) == 0);
  apply_text_move(kBoard0, b1, 12, 20, '*');
  CHECK(chess_engine_parse_board(b1, 72, cur) == 0);
  CHECK(chess_engine_diff_move(prev, cur, CHESS_WHITE, &m) == 0);
  CHECK(m.from == 12 && m.to == 20 && m.promotion == CHESS_EMPTY);
  /* Synthetic exd5 capture: white pawn e4(28) takes d5(35). */
  {
    chess_position ref;
    char b2[72];
    char b3[72];
    CHECK(chess_position_from_fen(
              &ref,
              "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2") ==
          CHESS_OK);
    /* Render the ref position into engine text by hand. */
    memcpy(b2, kBoard0, 72);
    apply_text_move(kBoard0, b2, 12, 28, '*'); /* e2e4 */
    memcpy(b3, b2, 72);
    apply_text_move(b2, b3, 51, 35, '+');      /* d7d5 */
    CHECK(chess_engine_parse_board(b3, 72, prev) == 0);
    apply_text_move(b3, b2, 28, 35, '*');      /* exd5 */
    CHECK(chess_engine_parse_board(b2, 72, cur) == 0);
    CHECK(chess_engine_diff_move(prev, cur, CHESS_WHITE, &m) == 0);
    CHECK(m.from == 28 && m.to == 35 && m.promotion == CHESS_EMPTY);
  }
}

static void test_diff_castle_and_promotion(void) {
  chess_piece prev[64];
  chess_piece cur[64];
  char b1[72];
  chess_move m;
  /* Synthetic O-O: Ke1(4)->g1(6), Rh1(7)->f1(5). */
  memcpy(b1, kBoard0, 72);
  CHECK(chess_engine_parse_board(kBoard0, 72, prev) == 0);
  apply_text_move(kBoard0, b1, 4, 6, 'K');
  apply_text_move(b1, b1, 7, 5, 'R');
  CHECK(chess_engine_parse_board(b1, 72, cur) == 0);
  CHECK(chess_engine_diff_move(prev, cur, CHESS_WHITE, &m) == 0);
  CHECK(m.from == 4 && m.to == 6);
  /* Synthetic promotion: white pawn b7(49) becomes queen on b8(57). */
  {
    char b2[72];
    char b3[72];
    memcpy(b3,
           "....k...\n"
           ".P......\n"
           "........\n"
           "........\n"
           "........\n"
           "........\n"
           "........\n"
           "....K...\n",
           72);
    CHECK(chess_engine_parse_board(b3, 72, prev) == 0);
    memcpy(b2, b3, 72);
    apply_text_move(b3, b2, 49, 57, 'Q');
    CHECK(chess_engine_parse_board(b2, 72, cur) == 0);
    CHECK(chess_engine_diff_move(prev, cur, CHESS_WHITE, &m) == 0);
    CHECK(m.from == 49 && m.to == 57 && m.promotion == CHESS_QUEEN);
  }
}

static void test_try_apply_matches(void) {
  chess_position pos;
  chess_piece want[64];
  char b1[72];
  chess_move m = {12, 28, CHESS_EMPTY}; /* e2e4 */
  CHECK(chess_position_from_fen(
            &pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  memcpy(b1, kBoard0, 72);
  apply_text_move(kBoard0, b1, 12, 28, '*');
  CHECK(chess_engine_parse_board(b1, 72, want) == 0);
  CHECK(chess_engine_try_apply(&pos, m, want) == 0);
  /* Wrong target placement must fail the dry run. */
  want[28].type = CHESS_KNIGHT;
  CHECK(chess_engine_try_apply(&pos, m, want) != 0);
  /* Illegal move must fail the dry run. */
  m.to = 36;
  want[28].type = CHESS_EMPTY;
  want[36].type = CHESS_PAWN;
  want[36].color = CHESS_WHITE;
  CHECK(chess_engine_try_apply(&pos, m, want) != 0);
}

int main(void) {
  test_parse_startpos();
  test_parse_rejects_garbage();
  test_diff_quiet_and_capture();
  test_diff_castle_and_promotion();
  test_try_apply_matches();

  if (failures == 0) {
    printf("PASS: all test_engine_parse checks passed\n");
    return 0;
  }
  printf("FAILURES: %d\n", failures);
  return 1;
}
