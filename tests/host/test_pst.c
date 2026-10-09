/* Piece-square table regression: search without opening book, PST consistency. */
#include <stdio.h>
#include <string.h>

#include "chess_core.h"
#include "mcu-max.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      failures++;                                                            \
    }                                                                        \
  } while (0)

static unsigned core_from_mcumax(mcumax_square sq) {
  unsigned rank = (unsigned)(sq >> 4);
  unsigned file = (unsigned)(sq & 7u);
  return (7u - rank) * 8u + file;
}

static bool mcumax_move_to_core(mcumax_move m, chess_move *out) {
  if (m.from == MCUMAX_SQUARE_INVALID || m.to == MCUMAX_SQUARE_INVALID) {
    return false;
  }
  out->from = core_from_mcumax(m.from);
  out->to = core_from_mcumax(m.to);
  out->promotion = CHESS_EMPTY;
  return true;
}

static bool first_move_allowed(const chess_move *m) {
  static const struct {
    unsigned from;
    unsigned to;
  } k_allowed[] = {
      {12, 28}, /* e2e4 */
      {11, 27}, /* d2d4 */
      {6, 21},  /* g1f3 */
      {1, 18},  /* b1c3 */
      {10, 26}, /* c2c4 */
  };
  size_t i;
  for (i = 0; i < sizeof(k_allowed) / sizeof(k_allowed[0]); i++) {
    if (m->from == k_allowed[i].from && m->to == k_allowed[i].to) {
      return true;
    }
  }
  return false;
}

static void check_pst_scratch_equals(const char *fen) {
  mcumax_set_fen_position(fen);
  CHECK(mcumax_eval_pst_from_scratch() == mcumax_eval_pst_score());
}

static void test_opening_without_book(void) {
  chess_position pos;
  chess_move m;
  mcumax_move reply;

  CHECK(chess_position_from_fen(
            &pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  mcumax_set_fen_position(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  reply = mcumax_search_best_move(200000u, 4u);
  CHECK(mcumax_move_to_core(reply, &m));
  CHECK(m.from != 10 || m.to != 18); /* not c2c3 */
  CHECK(first_move_allowed(&m));

  mcumax_set_fen_position(
      "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
  reply = mcumax_search_best_move(200000u, 4u);
  CHECK(mcumax_move_to_core(reply, &m));
  CHECK(!(m.from == 50 && m.to == 42)); /* not c7c6 */
}

static void test_pst_positions(void) {
  check_pst_scratch_equals(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  check_pst_scratch_equals(
      "rnbqkb1r/pppp1ppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2");
  check_pst_scratch_equals(
      "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
  check_pst_scratch_equals(
      "rnbqkbnr/ppp2ppp/4p3/3P4/8/8/PPP1PPPP/RNBQKBNR w KQkq e6 0 3");
  check_pst_scratch_equals(
      "8/4P3/8/8/8/8/8/4K2k w - - 0 1");
}

static void test_pst_symmetric(void) {
  int32_t w;
  int32_t b;

  /* Same material; STM flip must negate side-to-move PST total. */
  mcumax_set_fen_position("4k3/8/8/4N3/4n3/8/8/4K3 w - - 0 1");
  w = mcumax_eval_pst_from_scratch();
  mcumax_set_fen_position("4k3/8/8/4N3/4n3/8/8/4K3 b - - 0 1");
  b = mcumax_eval_pst_from_scratch();
  CHECK(w == -b);
}

static void test_pst_quiet_move_delta(void) {
  int32_t before;
  int32_t after;
  mcumax_move m = {0x76, 0x55}; /* Ng1f3 */

  mcumax_set_fen_position(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  before = mcumax_eval_pst_from_scratch();
  CHECK(before == 0);
  CHECK(mcumax_play_move(m));
  after = mcumax_eval_pst_from_scratch();
  CHECK(after < 0);
}

int main(void) {
  test_opening_without_book();
  test_pst_positions();
  test_pst_symmetric();
  test_pst_quiet_move_delta();
  if (failures != 0) {
    printf("FAIL: %d test_pst check(s) failed\n", failures);
    return 1;
  }
  printf("PASS: all test_pst checks passed\n");
  return 0;
}
