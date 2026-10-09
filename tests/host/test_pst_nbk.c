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

int main(void) {
  test_mirror_symmetry();
  test_delta_cap();
  test_opening_without_book();
  if (failures != 0) {
    printf("FAIL: %d test_pst_nbk check(s) failed\n", failures);
    return 1;
  }
  printf("PASS: all test_pst_nbk checks passed\n");
  return 0;
}
