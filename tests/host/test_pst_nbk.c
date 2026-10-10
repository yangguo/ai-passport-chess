/* Capped knight/bishop/king PST: symmetry, delta cap, opening sanity. */
#include <stdio.h>
#include <stdlib.h>

#include "chess_core.h"
#include "mcu-max.h"
#include "mcumax_nbk_tables.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      failures++;                                                            \
    }                                                                        \
  } while (0)

static const int k_knight_steps[] = {14, 18, 31, 33};
static const int k_king_steps[] = {1, 16, 15, 17, -1, -16, -15, -17};
static const int k_bishop_steps[] = {1, 16, 15, 17, -1, -16, -15, -17};

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
      {12, 28}, {11, 27}, {6, 21}, {1, 18}, {10, 26},
  };
  size_t i;
  for (i = 0; i < sizeof(k_allowed) / sizeof(k_allowed[0]); i++) {
    if (m->from == k_allowed[i].from && m->to == k_allowed[i].to) {
      return true;
    }
  }
  return false;
}

static int abs32(int32_t v) { return v < 0 ? -v : v; }

static int8_t plane_at(const int8_t *plane, unsigned sq) {
  unsigned idx = ((sq >> 4) << 3) | (sq & 7u);
  return plane[idx];
}

static void check_plane_cap(const int8_t *plane, const int *steps, size_t nsteps,
                            int slide) {
  for (unsigned rank = 0; rank < 8; rank++) {
    for (unsigned file = 0; file < 8; file++) {
      unsigned from = (rank << 4) | file;
      int8_t v0 = plane_at(plane, from);
      for (size_t i = 0; i < nsteps; i++) {
        if (slide) {
          int to = (int)from + steps[i];
          while (to >= 0 && (to & 0x88) == 0) {
            int delta = abs32((int32_t)plane_at(plane, (unsigned)to) - (int32_t)v0);
            CHECK(delta <= MCUMAX_NBK_PST_DELTA_CAP);
            to += steps[i];
          }
        } else {
          int to = (int)from + steps[i];
          if (to < 0 || (to & 0x88) != 0) {
            continue;
          }
          int delta = abs32((int32_t)plane_at(plane, (unsigned)to) - (int32_t)v0);
          CHECK(delta <= MCUMAX_NBK_PST_DELTA_CAP);
        }
      }
    }
  }
}

static void test_delta_cap(void) {
  check_plane_cap(mcumax_nbk_knight, k_knight_steps,
                  sizeof(k_knight_steps) / sizeof(k_knight_steps[0]), 0);
  check_plane_cap(mcumax_nbk_bishop, k_bishop_steps,
                  sizeof(k_bishop_steps) / sizeof(k_bishop_steps[0]), 1);
  check_plane_cap(mcumax_nbk_king_mg, k_king_steps,
                  sizeof(k_king_steps) / sizeof(k_king_steps[0]), 0);
  check_plane_cap(mcumax_nbk_king_eg, k_king_steps,
                  sizeof(k_king_steps) / sizeof(k_king_steps[0]), 0);
}

static void test_mirror_symmetry(void) {
  int32_t w;
  int32_t b;

  mcumax_set_fen_position("4k3/8/8/4N3/4n3/8/8/4K3 w - - 0 1");
  w = mcumax_eval_nbk_pst_score();
  mcumax_set_fen_position("4k3/8/8/4N3/4n3/8/8/4K3 b - - 0 1");
  b = mcumax_eval_nbk_pst_score();
  CHECK(w == -b);
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
  CHECK(m.from != 8 || m.to != 24);  /* not a2a3 */
  CHECK(m.from != 15 || m.to != 23); /* not h2h3 */
  CHECK(first_move_allowed(&m));
}

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

static unsigned mcu_from_core(unsigned sq) {
  unsigned rank = sq / 8u;
  unsigned file = sq % 8u;
  return ((7u - rank) << 4) | file;
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

/* Independent of mcumax_nbk_lookup. */
static int32_t test_nbk_at(unsigned piece, unsigned sq, int eg_king) {
  unsigned type = piece & 7u;
  unsigned idx_sq = sq & 0x77u;
  unsigned idx;
  const int8_t *plane = NULL;

  if (piece & RAW_BLACK) {
    idx_sq ^= 0x70u;
  }
  idx = ((idx_sq >> 4) << 3) | (idx_sq & 7u);
  if (type == 3u) {
    plane = mcumax_nbk_knight;
  } else if (type == 5u) {
    plane = mcumax_nbk_bishop;
  } else if (type == 4u) {
    plane = eg_king ? mcumax_nbk_king_eg : mcumax_nbk_king_mg;
  }
  if (plane == NULL) {
    return 0;
  }
  return plane[idx];
}

static int32_t test_full_nbk(const uint8_t *board, uint8_t side, int eg_king) {
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
    if ((piece & 7u) == 0u) {
      continue;
    }
    v = test_nbk_at(piece, sq, eg_king);
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
  uint8_t from;
  uint8_t to;
  int eg_king;
  int32_t delta = 0;
  int32_t full_before;
  int32_t full_after;
  int color;
  chess_piece mover;
  int castle;
  int ep;
  int capture;
  unsigned tf;
  unsigned ff;

  mcumax_set_fen_position(fen);
  side = mcumax_get_current_side();
  eg_king = mcumax_eval_non_pawn_material() > 30;
  snapshot_board(before);
  full_before = test_full_nbk(before, side, eg_king);
  from = (uint8_t)mcu_from_core(mv.from);
  to = (uint8_t)mcu_from_core(mv.to);
  mover = pos->board[mv.from];
  ff = mv.from % 8u;
  tf = mv.to % 8u;
  castle = mover.type == CHESS_KING && (ff > tf ? ff - tf : tf - ff) == 2u;
  ep = mover.type == CHESS_PAWN && pos->ep_square == mv.to &&
       pos->board[mv.to].type == CHESS_EMPTY;
  capture = ep || pos->board[mv.to].type != CHESS_EMPTY;
  color = (side == RAW_WHITE) ? 0 : 1;

  if (mv.promotion != CHESS_EMPTY) {
    static const chess_piece_type k_promos[] = {CHESS_QUEEN, CHESS_KNIGHT,
                                                 CHESS_BISHOP, CHESS_ROOK};
    size_t i;
    for (i = 0; i < 4; i++) {
      if (!mcumax_eval_probe_nbk_delta(from, to, engine_promo(k_promos[i]),
                                       &delta, after)) {
        printf("FAIL probe promo fen %s %u->%u\n", fen, mv.from, mv.to);
        failures++;
        return;
      }
      full_after = test_full_nbk(after, side, eg_king);
      if (delta != full_after - full_before) {
        printf("FAIL promo identity fen %s %u->%u delta %d full %d\n", fen,
               mv.from, mv.to, (int)delta, (int)(full_after - full_before));
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

  if (!mcumax_eval_probe_nbk_delta(from, to, 0, &delta, after)) {
    printf("FAIL probe fen %s %u->%u\n", fen, mv.from, mv.to);
    failures++;
    return;
  }
  full_after = test_full_nbk(after, side, eg_king);
  if (delta != full_after - full_before) {
    printf("FAIL identity fen %s %u->%u delta %d full %d\n", fen, mv.from, mv.to,
           (int)delta, (int)(full_after - full_before));
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

static uint32_t rng_state = 0xA11CEu;

static uint32_t rnd(void) {
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state;
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

static void test_orientation(void) {
  const uint8_t white_king = (uint8_t)(RAW_WHITE | 4u);
  const uint8_t black_king = (uint8_t)(RAW_BLACK | 4u);
  const uint8_t white_knight = (uint8_t)(RAW_WHITE | 3u);
  const uint8_t black_knight = (uint8_t)(RAW_BLACK | 3u);
  int32_t g1;
  int32_t g8;

  mcumax_set_fen_position(
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  g1 = mcumax_eval_nbk_piece(white_king, 0x76);
  g8 = mcumax_eval_nbk_piece(white_king, 0x06);
  CHECK(g1 > g8);
  CHECK(g1 == mcumax_nbk_king_mg[62]);
  CHECK(g8 == mcumax_nbk_king_mg[6]);
  CHECK(mcumax_eval_nbk_piece(black_king, 0x06) == g1);
  CHECK(mcumax_eval_nbk_piece(black_king, 0x76) == g8);
  CHECK(mcumax_nbk_knight[0] != mcumax_nbk_knight[56]);
  CHECK(mcumax_eval_nbk_piece(white_knight, 0x00) == mcumax_nbk_knight[0]);
  CHECK(mcumax_eval_nbk_piece(white_knight, 0x70) == mcumax_nbk_knight[56]);
  CHECK(mcumax_eval_nbk_piece(black_knight, 0x00) == mcumax_nbk_knight[56]);
}

static void test_nbk_identities(void) {
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
  };
  size_t i;
  int color;
  int kind;

  for (i = 0; i < sizeof(fens) / sizeof(fens[0]); i++) {
    check_position_moves(fens[i]);
  }
  walk_random("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 8);
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
  test_mirror_symmetry();
  test_delta_cap();
  test_nbk_identities();
  test_opening_without_book();
  if (failures != 0) {
    printf("FAIL: %d test_pst_nbk check(s) failed\n", failures);
    return 1;
  }
  printf("PASS: all test_pst_nbk checks passed\n");
  return 0;
}
