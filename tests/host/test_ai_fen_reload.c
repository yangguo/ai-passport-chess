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

static void test_partial_castling_black_kingside(void) {
  const char *fen_k = "r3k2r/8/8/8/8/8/8/4K3 b k - 0 1";
  const char *fen_none = "r3k2r/8/8/8/8/8/8/4K3 b - - 0 1";
  mcumax_move moves[96];
  uint32_t n;

  CHECK(valid_moves_lists_equal(fen_k));
  mcumax_set_fen_position(fen_k);
  n = mcumax_search_valid_moves(moves, 96);
  CHECK(valid_moves_contain(n, moves, 0x04, 0x06));

  mcumax_set_fen_position(fen_none);
  n = mcumax_search_valid_moves(moves, 96);
  CHECK(!valid_moves_contain(n, moves, 0x04, 0x06));

  suggest_matches_fresh_fen(fen_k, 80000u, 4u);
}

static void test_en_passant_available(void) {
  /* 1.e4 a6 2.e5 d5 — White may capture en passant on d6. */
  const char *fen =
      "rnbqkbnr/pppp1ppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3";
  mcumax_move moves[96];
  uint32_t n;

  CHECK(valid_moves_lists_equal(fen));
  mcumax_set_fen_position(fen);
  n = mcumax_search_valid_moves(moves, 96);
  CHECK(valid_moves_contain(n, moves, 0x34, 0x23));
  suggest_matches_fresh_fen(fen, 100000u, 4u);
}

static void test_promotion_a7a8_then_knight_fen(void) {
  const char *fen_a7 = "7k/P7/8/8/8/8/8/K7 w - - 0 1";
  chess_position pos;
  chess_move m;
  chess_undo undo;
  char fen_knight[CHESS_FEN_MAX];
  mcumax_move moves[96];
  uint32_t n;

  mcumax_set_fen_position(fen_a7);
  n = mcumax_search_valid_moves(moves, 96);
  CHECK(valid_moves_contain(n, moves, 0x10, 0x00));

  CHECK(chess_position_from_fen(&pos, fen_a7) == CHESS_OK);
  m.from = 48;
  m.to = 56;
  m.promotion = CHESS_KNIGHT;
  CHECK(chess_make(&pos, m, &undo) == CHESS_OK);
  CHECK(chess_position_to_fen(&pos, fen_knight, sizeof(fen_knight)) == CHESS_OK);

  CHECK(valid_moves_lists_equal(fen_knight));
  suggest_matches_fresh_fen(fen_knight, 80000u, 4u);
}

int main(void) {
  chess_ai_engine_init();
  test_castling_kqkq_start_available();
  test_partial_castling_black_kingside();
  test_en_passant_available();
  test_promotion_a7a8_then_knight_fen();
  if (failures) {
    printf("%d test(s) failed\n", failures);
    return 1;
  }
  printf("ok ai_fen_reload\n");
  return 0;
}
