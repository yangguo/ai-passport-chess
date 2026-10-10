/* Piece-square table regression: orientation, and search PST deltas
 * against an independent full-board sum. */
#include <stdio.h>
#include <string.h>

#include "chess_core.h"
#include "mcu-max.h"
#include "mcumax_pesto_tables.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      failures++;                                                            \
    }                                                                        \
  } while (0)

/* Engine color bits. White is 0x08, black is 0x10 (see mcu-max.c). */
#define RAW_WHITE 0x08u
#define RAW_BLACK 0x10u

enum {
  KIND_QUIET = 0,
  KIND_CAPTURE,
  KIND_EP,
  KIND_CASTLE,
  KIND_PROMO,
  KIND_UNDER,
  KIND_COUNT
};

static int kind_hits[2][KIND_COUNT];

static unsigned core_from_mcumax(mcumax_square sq) {
  unsigned rank = (unsigned)(sq >> 4);
  unsigned file = (unsigned)(sq & 7u);
  return (7u - rank) * 8u + file;
}

static unsigned mcu_from_core(unsigned sq) {
  unsigned rank = sq / 8u;
  unsigned file = sq % 8u;
  return ((7u - rank) << 4) | file;
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

/* Independent of mcumax_pst_lookup. a8=0 index, white flip 0, black ^56. */
static unsigned test_pesto_index(unsigned sq, unsigned piece) {
  unsigned file = sq & 7u;
  unsigned rank = sq >> 4; /* 0 = rank 8 */
  unsigned white_view = (rank << 3) | file;
  unsigned flip = (piece & RAW_BLACK) ? 56u : 0u;
  return white_view ^ flip;
}

static int test_plane(unsigned type) {
  switch (type & 7u) {
  case 1:
  case 2:
    return 0;
  case 3:
    return 1;
  case 5:
    return 2;
  case 6:
    return 3;
  case 7:
    return 4;
  case 4:
    return 5;
  default:
    return -1;
  }
}

static int32_t test_interp(int8_t mg, int8_t eg, uint8_t phase) {
  int32_t m = mg;
  int32_t e = eg;
  return (m * (int32_t)phase + e * (int32_t)(24u - phase)) / 24;
}

static int32_t test_piece_pst(unsigned piece, unsigned sq, uint8_t phase) {
  int plane = test_plane(piece);
  unsigned idx;
  if (plane < 0 || (piece & 7u) == 0) {
    return 0;
  }
  idx = test_pesto_index(sq, piece);
  return test_interp(mcumax_pesto_mg_planes[plane][idx],
                     mcumax_pesto_eg_planes[plane][idx], phase);
}

/* Side-to-move total at a caller-supplied phase. Does not retune phase. */
static int32_t test_full_pst(const uint8_t *board, uint8_t side, uint8_t phase) {
  int32_t white = 0;
  int32_t black = 0;
  unsigned sq;

  for (sq = 0; sq < 0x80u; sq++) {
    unsigned piece;
    int32_t v;
    if (sq & 0x88u) {
      continue;
    }
    piece = board[sq];
    if ((piece & 7u) == 0) {
      continue;
    }
    v = test_piece_pst(piece, sq, phase);
    if (piece & RAW_BLACK) {
      black += v;
    } else {
      white += v;
    }
  }
  if (side == RAW_WHITE) {
    return white - black;
  }
  return black - white;
}

static uint8_t engine_promo(chess_piece_type promotion) {
  switch (promotion) {
  case CHESS_KNIGHT:
    return 3;
  case CHESS_BISHOP:
    return 5;
  case CHESS_ROOK:
    return 6;
  case CHESS_QUEEN:
    return 7;
  default:
    return 0;
  }
}

static void snapshot_board(uint8_t *dst) {
  unsigned sq;
  for (sq = 0; sq < 0x80u; sq++) {
    dst[sq] = mcumax_get_board_byte((mcumax_square)sq);
  }
}

static void check_move_identity(const char *fen, const chess_position *pos,
                                chess_move mv) {
  uint8_t before[0x80];
  uint8_t after[0x80];
  uint8_t side;
  uint8_t phase;
  uint8_t from;
  uint8_t to;
  uint8_t promo = 0;
  int32_t delta = 0;
  int32_t full_before;
  int32_t full_after;
  int color;
  chess_piece mover;
  int castle;
  int ep;
  int capture;
  int promo_move;
  unsigned tf;
  unsigned ff;

  mcumax_set_fen_position(fen);
  side = mcumax_get_current_side();
  phase = mcumax_eval_pesto_phase();
  snapshot_board(before);
  full_before = test_full_pst(before, side, phase);

  from = (uint8_t)mcu_from_core(mv.from);
  to = (uint8_t)mcu_from_core(mv.to);
  mover = pos->board[mv.from];
  ff = mv.from % 8u;
  tf = mv.to % 8u;
  castle = mover.type == CHESS_KING && (ff > tf ? ff - tf : tf - ff) == 2u;
  ep = mover.type == CHESS_PAWN && pos->ep_square == mv.to &&
       pos->board[mv.to].type == CHESS_EMPTY;
  capture = ep || pos->board[mv.to].type != CHESS_EMPTY;
  promo_move = mv.promotion != CHESS_EMPTY;
  color = (side == RAW_WHITE) ? 0 : 1;

  if (promo_move) {
    static const chess_piece_type k_promos[] = {CHESS_QUEEN, CHESS_KNIGHT,
                                                 CHESS_BISHOP, CHESS_ROOK};
    size_t i;
    for (i = 0; i < 4; i++) {
      promo = engine_promo(k_promos[i]);
      if (!mcumax_eval_probe_pst_delta(from, to, promo, &delta, after)) {
        printf("FAIL probe promo fen %s %u->%u piece %u\n", fen, mv.from, mv.to,
               promo);
        failures++;
        return;
      }
      full_after = test_full_pst(after, side, phase);
      if (delta != full_after - full_before) {
        printf("FAIL promo identity fen %s %u->%u piece %u delta %d full %d\n",
               fen, mv.from, mv.to, promo, (int)delta,
               (int)(full_after - full_before));
        failures++;
        return;
      }
      kind_hits[color][KIND_PROMO]++;
      if (k_promos[i] != CHESS_QUEEN) {
        kind_hits[color][KIND_UNDER]++;
      }
    }
    if (capture) {
      kind_hits[color][KIND_CAPTURE]++;
    }
    return;
  }

  if (!mcumax_eval_probe_pst_delta(from, to, 0, &delta, after)) {
    printf("FAIL probe fen %s %u->%u\n", fen, mv.from, mv.to);
    failures++;
    return;
  }
  full_after = test_full_pst(after, side, phase);
  if (delta != full_after - full_before) {
    printf("FAIL identity fen %s %u->%u delta %d full %d phase %u\n", fen,
           mv.from, mv.to, (int)delta, (int)(full_after - full_before), phase);
    failures++;
    return;
  }

  if (castle) {
    kind_hits[color][KIND_CASTLE]++;
  } else if (ep) {
    kind_hits[color][KIND_EP]++;
  } else if (capture) {
    kind_hits[color][KIND_CAPTURE]++;
  } else {
    kind_hits[color][KIND_QUIET]++;
  }
}

static void check_position_moves(const char *fen) {
  chess_position pos;
  chess_move moves[CHESS_MAX_MOVES];
  size_t count = 0;
  size_t i;
  char exported[CHESS_FEN_MAX];

  CHECK(chess_position_from_fen(&pos, fen) == CHESS_OK);
  CHECK(chess_position_to_fen(&pos, exported, sizeof exported) == CHESS_OK);
  CHECK(chess_generate_legal(&pos, moves, CHESS_MAX_MOVES, &count) == CHESS_OK);
  for (i = 0; i < count; i++) {
    check_move_identity(exported, &pos, moves[i]);
  }
}

static uint32_t rng_state = 0xC0FFEEu;

static uint32_t rnd(void) {
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state;
}

static void walk_random(const char *fen, int plies) {
  chess_position pos;
  int ply;

  CHECK(chess_position_from_fen(&pos, fen) == CHESS_OK);
  for (ply = 0; ply < plies; ply++) {
    chess_move moves[CHESS_MAX_MOVES];
    chess_undo undo;
    size_t count = 0;
    char exported[CHESS_FEN_MAX];
    size_t i;

    if (chess_generate_legal(&pos, moves, CHESS_MAX_MOVES, &count) != CHESS_OK ||
        count == 0) {
      break;
    }
    CHECK(chess_position_to_fen(&pos, exported, sizeof exported) == CHESS_OK);
    for (i = 0; i < count; i++) {
      check_move_identity(exported, &pos, moves[i]);
    }
    CHECK(chess_make(&pos, moves[rnd() % count], &undo) == CHESS_OK);
  }
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

static void test_orientation(void) {
  /* Phase 0 is pure endgame. e7 is PeSTO a8=0 index 12; e2 is index 52. */
  const uint8_t white_pawn = (uint8_t)(RAW_WHITE | 1u);
  const uint8_t black_pawn = (uint8_t)(RAW_BLACK | 2u);
  int32_t white_e7 = mcumax_eval_pst_piece(white_pawn, 0x14, 0);
  int32_t white_e2 = mcumax_eval_pst_piece(white_pawn, 0x64, 0);
  int32_t black_e2 = mcumax_eval_pst_piece(black_pawn, 0x64, 0);
  int32_t black_e7 = mcumax_eval_pst_piece(black_pawn, 0x14, 0);

  CHECK(white_e7 > white_e2);
  CHECK(white_e7 == mcumax_pesto_eg_pawn[12]);
  CHECK(white_e2 == mcumax_pesto_eg_pawn[52]);
  CHECK(black_e2 > black_e7);
  CHECK(black_e2 == white_e7);
  CHECK(black_e7 == white_e2);
}

static void test_start_symmetric(void) {
  mcumax_set_fen_position(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  CHECK(mcumax_eval_pst_score() == 0);
  CHECK(mcumax_eval_pesto_phase() == 24);
}

static void test_pst_identities(void) {
  static const char *fens[] = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
      "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3",
      "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2",
      "rnbqkbnr/pppp1ppp/8/8/4pP2/8/PPPPP1PP/RNBQKBNR b KQkq f3 0 3",
      "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
      "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1",
      "4k3/P7/8/8/8/8/1p6/4K3 w - - 0 1",
      "4k3/P7/8/8/8/8/1p6/4K3 b - - 0 1",
      "4k3/8/8/3q4/8/2N5/8/4K3 w - - 0 1",
      "4k3/8/8/3Q4/8/2n5/8/4K3 b - - 0 1",
      "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 6 5",
      "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
  };
  size_t i;
  int color;
  int kind;

  for (i = 0; i < sizeof(fens) / sizeof(fens[0]); i++) {
    check_position_moves(fens[i]);
  }
  walk_random("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 12);
  walk_random("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
              8);

  for (color = 0; color < 2; color++) {
    for (kind = 0; kind < KIND_COUNT; kind++) {
      if (kind_hits[color][kind] == 0) {
        printf("FAIL missing kind %d for side %d\n", kind, color);
        failures++;
      }
    }
  }
}

int main(void) {
  test_orientation();
  test_start_symmetric();
  test_pst_identities();
  test_opening_without_book();
  if (failures != 0) {
    printf("FAIL: %d test_pst check(s) failed\n", failures);
    return 1;
  }
  printf("PASS: all test_pst checks passed\n");
  return 0;
}
