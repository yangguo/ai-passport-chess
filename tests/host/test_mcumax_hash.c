/* Transposition table: legality, mate-in-1, stale checkpoint after set_fen. */
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
  /* Historical search line at CLI default budget (no opening book). */
  CHECK(m.from == 10 && m.to == 18); /* c2c3 */
}

int main(void) {
  chess_ai_engine_init();
#if MCUMAX_HASH_BITS > 0
  CHECK(mcumax_hash_is_active());
#endif
  test_cli_default_start_move();
  test_mate_in_one();
  test_random_line_legal();
  if (failures) {
    printf("%d test(s) failed\n", failures);
    return 1;
  }
  printf("ok mcumax_hash\n");
  return 0;
}
