/* chess_ai always reloads FEN; moved/castling flags must match on every load. */
#include <stdio.h>
#include <string.h>

#include "chess_ai.h"
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

static void suggest_matches_fresh_fen(const char *fen, uint32_t nodes,
                                      unsigned depth) {
  chess_position pos_a;
  chess_position pos_b;
  chess_move ma;
  chess_move mb;

  CHECK(chess_position_from_fen(&pos_a, fen) == CHESS_OK);
  CHECK(chess_position_from_fen(&pos_b, fen) == CHESS_OK);
  CHECK(chess_ai_suggest(&pos_a, nodes, depth, &ma) == 0);
  CHECK(chess_ai_suggest(&pos_b, nodes, depth, &mb) == 0);
  CHECK(ma.from == mb.from && ma.to == mb.to && ma.promotion == mb.promotion);
  CHECK(move_is_legal(&pos_a, ma));
}

static bool valid_moves_lists_equal(const char *fen) {
  mcumax_move a[96];
  mcumax_move b[96];
  uint32_t na;
  uint32_t nb;
  size_t i;

  mcumax_set_fen_position(fen);
  na = mcumax_search_valid_moves(a, 96);
  mcumax_set_fen_position(fen);
  nb = mcumax_search_valid_moves(b, 96);
  if (na != nb) {
    return false;
  }
  for (i = 0; i < na; i++) {
    if (a[i].from != b[i].from || a[i].to != b[i].to) {
      return false;
    }
  }
  return true;
}

static bool valid_moves_contain(uint32_t n, const mcumax_move *moves,
                                uint8_t from, uint8_t to) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (moves[i].from == from && moves[i].to == to) {
      return true;
    }
  }
  return false;
}

static void test_castling_kqkq_start_available(void) {
  const char *fen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  CHECK(valid_moves_lists_equal(fen));
  mcumax_set_fen_position(fen);
  /* Virgin king and h1 rook: castling rights KQkq were applied from FEN. */
  CHECK((mcumax_get_board_byte(0x74) & 0x20u) == 0u);
  CHECK((mcumax_get_board_byte(0x77) & 0x20u) == 0u);
  suggest_matches_fresh_fen(fen, 120000u, 4u);
}

static void test_partial_castling_rights_consistent(void) {
  const char *fen =
      "1rbqkb1r/1ppppppp/8/p7/1n2QP2/2NP3P/PPP3P1/1RB1KBNn w k - 1 9";
  mcumax_move moves[96];
  uint32_t n;

  CHECK(valid_moves_lists_equal(fen));
  mcumax_set_fen_position(fen);
  n = mcumax_search_valid_moves(moves, 96);
  CHECK(!valid_moves_contain(n, moves, 0x74, 0x76));
  suggest_matches_fresh_fen(fen, 150000u, 5u);
}

static void test_en_passant_available(void) {
  const char *fen =
      "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
  mcumax_move moves[96];
  uint32_t n;

  CHECK(valid_moves_lists_equal(fen));
  mcumax_set_fen_position(fen);
  n = mcumax_search_valid_moves(moves, 96);
  CHECK(n > 0);
  suggest_matches_fresh_fen(fen, 100000u, 4u);
}

static void test_promoted_knight_fen_reload(void) {
  const char *fen = "7k/4N3/8/8/8/8/8/4K3 w - - 0 1";

  CHECK(valid_moves_lists_equal(fen));
  suggest_matches_fresh_fen(fen, 80000u, 4u);
}

int main(void) {
  chess_ai_engine_init();
  test_castling_kqkq_start_available();
  test_partial_castling_rights_consistent();
  test_en_passant_available();
  test_promoted_knight_fen_reload();
  if (failures) {
    printf("%d test(s) failed\n", failures);
    return 1;
  }
  printf("ok ai_fen_reload\n");
  return 0;
}
