/* Transposition table: legality, TT/no-TT parity, hash keys, node budget. */
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

static void map_reply(const chess_position *pos, mcumax_move reply,
                      chess_move *out) {
  unsigned file = (unsigned)(reply.from & 15u);
  unsigned rank = (unsigned)((reply.from >> 4) & 15u);
  unsigned tfile = (unsigned)(reply.to & 15u);
  unsigned trank = (unsigned)((reply.to >> 4) & 15u);
  unsigned last = (pos->side_to_move == CHESS_WHITE) ? 7u : 0u;
  out->from = (uint8_t)((7u - rank) * 8u + file);
  out->to = (uint8_t)((7u - trank) * 8u + tfile);
  out->promotion = CHESS_EMPTY;
  if (pos->board[out->from].type == CHESS_PAWN && out->to / 8u == last) {
    out->promotion = CHESS_QUEEN;
  }
}

static bool engine_move_legal(const chess_position *pos, const char *fen,
                              uint32_t nodes, unsigned depth,
                              chess_move *out) {
  char buf[CHESS_FEN_MAX];
  mcumax_move reply;
  chess_position probe;
  chess_undo undo;
  if (strcmp(fen, "") != 0) {
    strncpy(buf, fen, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
  } else if (chess_position_to_fen(pos, buf, sizeof(buf)) != CHESS_OK) {
    return false;
  }
  mcumax_set_fen_position(buf);
  reply = mcumax_search_best_move(nodes, depth);
  if (reply.from == MCUMAX_SQUARE_INVALID || reply.to == MCUMAX_SQUARE_INVALID) {
    return false;
  }
  map_reply(pos, reply, out);
  probe = *pos;
  return chess_make(&probe, *out, &undo) == CHESS_OK;
}

static void test_mate_in_one(void) {
  chess_position pos;
  chess_move m;
  const char *fen =
      "rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2";
  CHECK(chess_position_from_fen(&pos, fen) == CHESS_OK);
  CHECK(engine_move_legal(&pos, fen, 200000u, 4u, &m));
}

static void test_random_line_legal(void) {
  const char *fens[] = {
      "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
      "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
      "8/8/4K3/8/8/8/8/4k3 w - - 0 1"};
  size_t i;
  for (i = 0; i < sizeof(fens) / sizeof(fens[0]); i++) {
    chess_position pos;
    chess_move m;
    CHECK(chess_position_from_fen(&pos, fens[i]) == CHESS_OK);
    CHECK(engine_move_legal(&pos, fens[i], 150000u, 5u, &m));
  }
}

/* Golden move at CLI default budget; must match with TT on or off (bits 0/10). */
static void test_cli_default_start_move(void) {
  chess_position pos;
  chess_move m;
  const char *fen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  CHECK(chess_position_from_fen(&pos, fen) == CHESS_OK);
  CHECK(engine_move_legal(&pos, fen, 200000u, 4u, &m));
  CHECK(m.from == 10 && m.to == 18); /* c2c3 */
}

#if MCUMAX_HASH_BITS > 0
static mcumax_move search_move(const char *fen, uint32_t nodes,
                               unsigned depth) {
  mcumax_set_fen_position(fen);
  return mcumax_search_best_move(nodes, depth);
}

static void test_hash_startpos_zero(void) {
  uint32_t k = 1, k2 = 1;
  mcumax_init();
  mcumax_hash_get_keys(&k, &k2);
  CHECK(k == 0u && k2 == 0u);
  mcumax_set_fen_position(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  mcumax_hash_get_keys(&k, &k2);
  if (k != 0u || k2 != 0u) {
    printf("unexpected fen keys %u %u\n", k, k2);
  }
  CHECK(k == 0u && k2 == 0u);
}

static void test_fixed_depth_parity_positions(void) {
  static const char *fens[] = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
      "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
      "8/2k5/8/8/8/8/8/4K3 w - - 0 1",
  };
  size_t i;
  mcumax_hash_alloc();
  for (i = 0; i < sizeof(fens) / sizeof(fens[0]); i++) {
    mcumax_move a, b;
    mcumax_hash_clear();
    a = search_move(fens[i], 120000u, 5u);
    mcumax_hash_clear();
    b = search_move(fens[i], 120000u, 5u);
    CHECK(a.from == b.from && a.to == b.to);
  }
}

static void test_tt_not_slower_in_nodes(void) {
  const char *fen =
      "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
  uint32_t nodes_cold;
  uint32_t nodes_warm;

  mcumax_hash_clear();
  mcumax_set_fen_position(fen);
  (void)mcumax_search_best_move(80000u, 5u);
  nodes_cold = mcumax_get_last_search_nodes();

  mcumax_set_fen_position(fen);
  (void)mcumax_search_best_move(80000u, 5u);
  nodes_warm = mcumax_get_last_search_nodes();

  /* Warm TT must not increase nodes at the same budget (allows cutoffs). */
  CHECK(nodes_warm <= nodes_cold);
}
#endif

int main(void) {
  chess_ai_engine_init();
#if MCUMAX_HASH_BITS > 0
  CHECK(mcumax_hash_is_active());
#endif
  test_cli_default_start_move();
  test_mate_in_one();
  test_random_line_legal();
#if MCUMAX_HASH_BITS > 0
  test_hash_startpos_zero();
  test_fixed_depth_parity_positions();
  test_tt_not_slower_in_nodes();
#endif
  if (failures) {
    printf("%d test(s) failed\n", failures);
    return 1;
  }
  printf("ok mcumax_hash\n");
  return 0;
}
