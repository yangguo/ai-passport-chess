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
      /* White pawn 0x09 | moved 0x20 = 0x29; + (647-1) wraps to 0xAF.
         Low 3 bits then become N 0xAB, B 0xAD, R 0xAE, Q 0xAF.
         Black pawn 0x12 | moved = 0x32; + (647-2) wraps to 0xB7.
         N 0xB3, B 0xB5, R 0xB6, Q 0xB7. */
      {
        uint8_t placed = after[to];
        uint8_t want = (uint8_t)((side == RAW_WHITE) ? 0xA8u : 0xB0u);
        want = (uint8_t)(want | promo);
        if (placed != want) {
          printf("FAIL promo byte fen %s %u->%u got 0x%02x want 0x%02x\n", fen,
                 mv.from, mv.to, placed, want);
          failures++;
          return;
        }
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

/* Upstream PeSTO centipawns (tools/pesto_source.py, a8=0), scaled by the
 * committed rule round(cp * 52 / 100). The integers below are that
 * arithmetic written out; this function does not read mcumax_pesto_tables.h
 * and does not recompute the blend. */
static void expect_pst(const char *label, uint8_t piece, mcumax_square sq,
                       uint8_t phase, int32_t want) {
  int32_t got = mcumax_eval_pst_piece(piece, sq, phase);
  if (got != want) {
    printf("FAIL golden %s piece 0x%02x sq 0x%02x phase %u got %d want %d\n",
           label, piece, sq, phase, (int)got, (int)want);
    failures++;
  }
}

static void test_golden_upstream(void) {
  const uint8_t wp = (uint8_t)(RAW_WHITE | 1u);
  const uint8_t bp = (uint8_t)(RAW_BLACK | 2u);
  const uint8_t wn = (uint8_t)(RAW_WHITE | 3u);
  const uint8_t bn = (uint8_t)(RAW_BLACK | 3u);
  const uint8_t wk = (uint8_t)(RAW_WHITE | 4u);
  const uint8_t bk = (uint8_t)(RAW_BLACK | 4u);
  const uint8_t wb = (uint8_t)(RAW_WHITE | 5u);
  const uint8_t bb = (uint8_t)(RAW_BLACK | 5u);
  const uint8_t wr = (uint8_t)(RAW_WHITE | 6u);
  const uint8_t br = (uint8_t)(RAW_BLACK | 6u);
  const uint8_t wq = (uint8_t)(RAW_WHITE | 7u);
  const uint8_t bq = (uint8_t)(RAW_BLACK | 7u);

  /* Pawn e7 index 12: MG 68 cp * 52 = 3536 → 35; EG 147*52 = 7644 → 76.
     Pawn e2 index 52: MG -15*52 = -780 → -8; EG 13*52 = 676 → 7. */
  expect_pst("P e7 MG", wp, 0x14, 24, 35);
  expect_pst("P e7 EG", wp, 0x14, 0, 76);
  expect_pst("P e2 MG", wp, 0x64, 24, -8);
  expect_pst("P e2 EG", wp, 0x64, 0, 7);
  expect_pst("p e2 MG mirror", bp, 0x64, 24, 35);
  expect_pst("p e7 EG mirror", bp, 0x14, 0, 7);

  /* Knight g1 index 62: MG -19*52 = -988 → -10; EG -50*52 = -2600 → -26.
     Knight f3 index 45: MG 17*52 = 884 → 9; EG -3*52 = -156 → -2. */
  expect_pst("N g1 MG", wn, 0x76, 24, -10);
  expect_pst("N g1 EG", wn, 0x76, 0, -26);
  expect_pst("N f3 MG", wn, 0x55, 24, 9);
  expect_pst("N f3 EG", wn, 0x55, 0, -2);
  expect_pst("n g8 MG mirror", bn, 0x06, 24, -10);
  expect_pst("n f6 EG mirror", bn, 0x25, 0, -2);
  /* Phase 16 blend of the scaled f3 knight: (9*16 + -2*8) / 24 = 5. */
  expect_pst("N f3 phase 16", wn, 0x55, 16, 5);

  /* Bishop c1 index 58: MG -14*52 = -728 → -7; EG -23*52 = -1196 → -12.
     Bishop c4 index 34: MG 13*52 = 676 → 7; EG 13*52 = 676 → 7. */
  expect_pst("B c1 MG", wb, 0x72, 24, -7);
  expect_pst("B c1 EG", wb, 0x72, 0, -12);
  expect_pst("B c4 MG", wb, 0x42, 24, 7);
  expect_pst("B c4 EG", wb, 0x42, 0, 7);
  expect_pst("b c8 MG mirror", bb, 0x02, 24, -7);

  /* Rook on the 7th, a7 index 8: MG 27*52 = 1404 → 14; EG 11*52 = 572 → 6. */
  expect_pst("R a7 MG", wr, 0x10, 24, 14);
  expect_pst("R a7 EG", wr, 0x10, 0, 6);
  expect_pst("r a2 EG mirror", br, 0x60, 0, 6);

  /* Queen d1 index 59: MG 10*52 = 520 → 5; EG -43*52 = -2236 → -22.
     Queen d4 index 35: MG -10*52 = -520 → -5; EG 47*52 = 2444 → 24. */
  expect_pst("Q d1 MG", wq, 0x73, 24, 5);
  expect_pst("Q d1 EG", wq, 0x73, 0, -22);
  expect_pst("Q d4 MG", wq, 0x43, 24, -5);
  expect_pst("Q d4 EG", wq, 0x43, 0, 24);
  expect_pst("q d8 MG mirror", bq, 0x03, 24, 5);

  /* King g1 index 62: MG 24*52 = 1248 → 12; EG -24*52 = -1248 → -12.
     King e4 index 36: MG -46*52 = -2392 → -24; EG 27*52 = 1404 → 14. */
  expect_pst("K g1 MG", wk, 0x76, 24, 12);
  expect_pst("K g1 EG", wk, 0x76, 0, -12);
  expect_pst("K e4 MG", wk, 0x44, 24, -24);
  expect_pst("K e4 EG", wk, 0x44, 0, 14);
  expect_pst("k g8 MG mirror", bk, 0x06, 24, 12);
  expect_pst("k e5 EG mirror", bk, 0x34, 0, 14);
  CHECK(mcumax_eval_pst_piece(wk, 0x76, 24) !=
        mcumax_eval_pst_piece(wk, 0x44, 0));
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
  CHECK(white_e7 == 76);
  CHECK(white_e2 == 7);
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
  test_golden_upstream();
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
