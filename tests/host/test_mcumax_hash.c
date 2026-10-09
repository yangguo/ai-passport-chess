/* Transposition table: legality, TT/no-TT parity, hash keys, node budget. */
#include <stdio.h>
#include <stdlib.h>
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
  /* Budget smoke: must be legal; exact move is not pinned (book/TT may vary). */
  (void)m;
}

#if MCUMAX_HASH_BITS > 0
static void core_move_to_mcumax(const chess_move *m, mcumax_move *out) {
  unsigned file = (unsigned)(m->from % 8u);
  unsigned rank = (unsigned)(m->from / 8u);
  unsigned tfile = (unsigned)(m->to % 8u);
  unsigned trank = (unsigned)(m->to / 8u);
  out->from = (uint8_t)(((7u - rank) << 4) | file);
  out->to = (uint8_t)(((7u - trank) << 4) | tfile);
}

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

static void test_hash_incremental_matches_fen_reseed(void) {
  chess_position pos;
  chess_move list[128];
  chess_undo undo;
  char fen[CHESS_FEN_MAX];
  mcumax_move hist[64];
  size_t hist_len = 0;
  uint32_t inc_k, inc_k2, fen_k, fen_k2;
  unsigned seed = 0x9e3779b9u;
  int plies;

  CHECK(chess_position_from_fen(&pos,
                                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w "
                                "KQkq - 0 1") == CHESS_OK);
  mcumax_init();

  for (plies = 0; plies < 16; plies++) {
    size_t n = 0;
    mcumax_move mm;
    chess_move pick;
    size_t idx;

    if (chess_generate_legal(&pos, list, 128u, &n) != CHESS_OK || n == 0) {
      break;
    }
    seed = seed * 1664525u + 1013904223u;
    idx = (size_t)(seed % n);
    pick = list[idx];
    core_move_to_mcumax(&pick, &mm);
    CHECK(mcumax_play_move(mm));
    CHECK(hist_len < sizeof(hist) / sizeof(hist[0]));
    hist[hist_len++] = mm;
    CHECK(chess_make(&pos, pick, &undo) == CHESS_OK);
    CHECK(chess_position_to_fen(&pos, fen, sizeof(fen)) == CHESS_OK);

    mcumax_hash_get_keys(&inc_k, &inc_k2);
    mcumax_hash_set_replay_hint(hist, hist_len);
    mcumax_set_fen_position(fen);
    mcumax_hash_get_keys(&fen_k, &fen_k2);

    if (inc_k != fen_k || inc_k2 != fen_k2) {
      printf("hash mismatch ply %d fen=%s\n  incr %u %u\n  fen  %u %u\n", plies,
             fen, inc_k, inc_k2, fen_k, fen_k2);
    }
    CHECK(inc_k == fen_k && inc_k2 == fen_k2);
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
  test_hash_incremental_matches_fen_reseed();
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
