/* Task 2 host tests: FEN parse/export, validation, bounds.
 * TDD: startpos + bad-FEN-no-clobber were written first and watched fail
 * (missing chess_core.h); edge cases below lock the rest of the contract. */
#include <stdio.h>
#include <string.h>

#include "chess_core.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      failures++;                                                            \
    }                                                                        \
  } while (0)

static void test_startpos_fields(void) {
  chess_position pos;
  /* Poison the struct so uninitialized fields are caught. */
  memset(&pos, 0xA5, sizeof(pos));

  chess_error err = chess_position_from_fen(
      &pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  CHECK(err == CHESS_OK);
  if (err != CHESS_OK) {
    return;
  }

  CHECK(pos.side_to_move == CHESS_WHITE);
  /* a1=0: white rook on a1, white king on e1(4), empty d4(27). */
  CHECK(pos.board[0].type == CHESS_ROOK && pos.board[0].color == CHESS_WHITE);
  CHECK(pos.board[4].type == CHESS_KING && pos.board[4].color == CHESS_WHITE);
  CHECK(pos.board[27].type == CHESS_EMPTY);
  /* White pawn a2(8); black pawn a7(48), rook a8(56), king e8(60). */
  CHECK(pos.board[8].type == CHESS_PAWN && pos.board[8].color == CHESS_WHITE);
  CHECK(pos.board[48].type == CHESS_PAWN && pos.board[48].color == CHESS_BLACK);
  CHECK(pos.board[56].type == CHESS_ROOK && pos.board[56].color == CHESS_BLACK);
  CHECK(pos.board[60].type == CHESS_KING && pos.board[60].color == CHESS_BLACK);
  CHECK(pos.castling ==
        (CHESS_CASTLE_WK | CHESS_CASTLE_WQ | CHESS_CASTLE_BK | CHESS_CASTLE_BQ));
  CHECK(pos.ep_square == CHESS_NO_SQUARE);
  CHECK(pos.halfmove_clock == 0);
  CHECK(pos.fullmove_number == 1);
  CHECK(pos.white_king == 4);
  CHECK(pos.black_king == 60);
}

static void test_bad_fen_leaves_output_untouched(void) {
  chess_position pos;
  memset(&pos, 0, sizeof(pos));

  chess_error err = chess_position_from_fen(
      &pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  CHECK(err == CHESS_OK);
  if (err != CHESS_OK) {
    return;
  }
  chess_position before = pos;

  err = chess_position_from_fen(&pos, "not a fen!!!");
  CHECK(err != CHESS_OK);
  CHECK(memcmp(&pos, &before, sizeof(pos)) == 0);
}

static void test_fen_round_trip(void) {
  static const char *kFens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    /* After 1.e4: black to move, ep square e3(20), fullmove still 1. */
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
    /* No castling, nonzero counters. */
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 99 150",
    "4k3/8/8/8/8/8/8/4K2R w K - 0 1",
    "6k1/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
    "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1",
  };
  size_t i;

  for (i = 0; i < sizeof(kFens) / sizeof(kFens[0]); i++) {
    chess_position pos;
    char buf[CHESS_FEN_MAX];
    chess_error err = chess_position_from_fen(&pos, kFens[i]);
    CHECK(err == CHESS_OK);
    if (err != CHESS_OK) {
      continue;
    }
    memset(buf, 0, sizeof(buf));
    err = chess_position_to_fen(&pos, buf, sizeof(buf));
    CHECK(err == CHESS_OK);
    CHECK(strcmp(buf, kFens[i]) == 0);
  }

  /* Spot-check the ep square index: e3 = file 4, rank index 2 -> 20. */
  {
    chess_position pos;
    chess_error err = chess_position_from_fen(
        &pos,
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    CHECK(err == CHESS_OK);
    if (err == CHESS_OK) {
      CHECK(pos.ep_square == 20);
      CHECK(pos.side_to_move == CHESS_BLACK);
    }
  }
}

static void test_rejects_bad_fens(void) {
  static const char *kBad[] = {
    "", /* empty */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR", /* 1 field */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 x", /* 7 fields */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR  w KQkq - 0 1", /* dbl space */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0", /* 5 fields */
    "Xnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", /* bad piece */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 ", /* trailing */
    "rnbqkbnr/ppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", /* short rank */
    "rnbqkbnr/ppppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", /* long rank */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1", /* 7 ranks */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/8 w KQkq - 0 1", /* 9 ranks */
    "rnbqkbnr/pppppppp/8/8/3K4/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", /* 2x W king */
    "rnbqkbnr/pppppppp/8/8/3k4/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", /* 2x B king */
    "8/8/8/8/8/8/8/8 w - - 0 1", /* no kings */
    "rnbq1bnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", /* no B king */
    "8/8/8/8/8/5k2/5K2/8 w - - 0 1", /* adjacent kings */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNP w KQkq - 0 1", /* pawn r1 */
    "RNBRKBNP/PPPPPPPP/8/8/8/8/pppppppp/rnbrkbnq w KQkq - 0 1", /* pawns r8 */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1", /* bad side */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KK - 0 1", /* dup castle */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkqX - 0 1", /* bad castle */
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e4 0 1", /* ep rank */
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e3 0 1", /* ep side */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 999999 1", /* hclk ovf */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - -1 1", /* neg hclk */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0", /* fmove 0 */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 70000", /* fmove ovf */
  };
  size_t i;

  for (i = 0; i < sizeof(kBad) / sizeof(kBad[0]); i++) {
    chess_position pos;
    chess_position before;
    chess_error err;
    memset(&before, 0x3C, sizeof(before));
    pos = before;
    err = chess_position_from_fen(&pos, kBad[i]);
    if (err == CHESS_OK) {
      printf("FAIL %s:%d: accepted bad FEN: %s\n", __FILE__, __LINE__, kBad[i]);
      failures++;
    }
    if (memcmp(&pos, &before, sizeof(pos)) != 0) {
      printf("FAIL %s:%d: clobbered output for: %s\n", __FILE__, __LINE__,
             kBad[i]);
      failures++;
    }
  }
}

static void test_null_and_small_buffer(void) {
  chess_position pos;
  char buf[CHESS_FEN_MAX];
  const char *start =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  CHECK(chess_position_from_fen(NULL, start) == CHESS_ERR_NULL);
  CHECK(chess_position_from_fen(&pos, NULL) == CHESS_ERR_NULL);
  CHECK(chess_position_to_fen(NULL, buf, sizeof(buf)) == CHESS_ERR_NULL);
  CHECK(chess_position_to_fen(&pos, NULL, sizeof(buf)) == CHESS_ERR_NULL);
  CHECK(chess_position_validate(NULL) == CHESS_ERR_NULL);

  CHECK(chess_position_from_fen(&pos, start) == CHESS_OK);

  /* Exact fit (strlen + NUL) succeeds; one byte short fails untouched. */
  {
    size_t need = strlen(start) + 1;
    memset(buf, 0x5A, sizeof(buf));
    CHECK(chess_position_to_fen(&pos, buf, need) == CHESS_OK);
    CHECK(strcmp(buf, start) == 0);

    memset(buf, 0x5A, sizeof(buf));
    CHECK(chess_position_to_fen(&pos, buf, need - 1) ==
          CHESS_ERR_BUFFER_TOO_SMALL);
    {
      size_t k;
      for (k = 0; k < sizeof(buf); k++) {
        if (buf[k] != 0x5A) {
          printf("FAIL %s:%d: buffer touched on short write\n", __FILE__,
                 __LINE__);
          failures++;
          break;
        }
      }
    }
  }
}

/* ---- Task 3 RED: move generation (UCI sets), attacks, make/unmake ----
 * Expected to FAIL to compile: chess_move/chess_generate_legal/etc.
 * do not exist yet. */

static void move_to_uci(chess_move m, char out[6]) {
  static const char promos[] = "xpnbrqk";
  out[0] = (char)('a' + m.from % 8u);
  out[1] = (char)('1' + m.from / 8u);
  out[2] = (char)('a' + m.to % 8u);
  out[3] = (char)('1' + m.to / 8u);
  if (m.promotion == CHESS_EMPTY) {
    out[4] = '\0';
  } else {
    out[4] = promos[m.promotion];
    out[5] = '\0';
  }
}

/* Exact set equality: counts match and every expected UCI is present
 * (the generator must never emit duplicates). */
static void check_move_set(const char *fen, const char *const *expected,
                           size_t nexp) {
  chess_position pos;
  chess_move moves[CHESS_MAX_MOVES];
  size_t count = 0;
  size_t i;
  char uci[6];

  CHECK(chess_position_from_fen(&pos, fen) == CHESS_OK);
  CHECK(chess_generate_legal(&pos, moves, CHESS_MAX_MOVES, &count) ==
        CHESS_OK);
  if (count != nexp) {
    printf("FAIL %s:%d: %s: got %zu moves, want %zu\n", __FILE__, __LINE__,
           fen, count, nexp);
    failures++;
    return;
  }
  for (i = 0; i < nexp; i++) {
    size_t j;
    int found = 0;
    for (j = 0; j < count; j++) {
      move_to_uci(moves[j], uci);
      if (strcmp(uci, expected[i]) == 0) {
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("FAIL %s:%d: %s: missing %s\n", __FILE__, __LINE__, fen,
             expected[i]);
      failures++;
    }
  }
}

static void test_startpos_move_set(void) {
  static const char *kWant[] = {
    "a2a3", "a2a4", "b2b3", "b2b4", "c2c3", "c2c4", "d2d3", "d2d4",
    "e2e3", "e2e4", "f2f3", "f2f4", "g2g3", "g2g4", "h2h3", "h2h4",
    "b1a3", "b1c3", "g1f3", "g1h3",
  };
  check_move_set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                 kWant, sizeof(kWant) / sizeof(kWant[0]));
}

/* White Re2 pinned against Ke1 by black Re8: rook must stay on the
 * e-file, king keeps its four open squares. */
static void test_pin_move_set(void) {
  static const char *kWant[] = {
    "e1d1", "e1f1", "e1d2", "e1f2",
    "e2e3", "e2e4", "e2e5", "e2e6", "e2e7", "e2e8",
  };
  check_move_set("4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1", kWant,
                 sizeof(kWant) / sizeof(kWant[0]));
}

static void test_bishop_move_set(void) {
  static const char *kWant[] = {
    "c1b2", "c1a3", "c1d2", "c1e3", "c1f4", "c1g5", "c1h6",
    "e1d1", "e1d2", "e1e2", "e1f1", "e1f2",
  };
  check_move_set("4k3/8/8/8/8/8/8/2B1K3 w - - 0 1", kWant,
                 sizeof(kWant) / sizeof(kWant[0]));
}

static void test_knight_move_set(void) {
  static const char *kWant[] = {
    "f3d4", "f3e5", "f3g5", "f3h4", "f3h2", "f3g1", "f3d2",
    "e1d1", "e1d2", "e1e2", "e1f1", "e1f2",
  };
  check_move_set("4k3/8/8/8/8/5N2/8/4K3 w - - 0 1", kWant,
                 sizeof(kWant) / sizeof(kWant[0]));
}

static void test_is_attacked(void) {
  chess_position pos;

  /* Black pawn on d5 hits c4(26)/e4(28), not d4(27). */
  CHECK(chess_position_from_fen(&pos, "4k3/8/8/3p4/8/8/8/4K3 w - - 0 1") ==
        CHESS_OK);
  CHECK(chess_is_attacked(&pos, 26, CHESS_BLACK));
  CHECK(chess_is_attacked(&pos, 28, CHESS_BLACK));
  CHECK(!chess_is_attacked(&pos, 27, CHESS_BLACK));
  CHECK(!chess_is_attacked(&pos, 26, CHESS_WHITE));

  /* Knight attacks regardless of occupancy: Nf3 hits own Ke1. */
  CHECK(chess_position_from_fen(&pos, "4k3/8/8/8/8/5N2/8/4K3 w - - 0 1") ==
        CHESS_OK);
  CHECK(chess_is_attacked(&pos, 27, CHESS_WHITE)); /* d4 */
  CHECK(chess_is_attacked(&pos, 4, CHESS_WHITE)); /* own king e1 */
  CHECK(!chess_is_attacked(&pos, 4, CHESS_BLACK));

  /* King adjacency: black Kf3 hits e2(12), not e1. */
  CHECK(chess_position_from_fen(&pos, "8/8/8/8/8/5k2/8/4K3 w - - 0 1") ==
        CHESS_OK);
  CHECK(chess_is_attacked(&pos, 12, CHESS_BLACK));
  CHECK(!chess_is_attacked(&pos, 4, CHESS_BLACK));

  /* Sliders through the pin: Re8 hits e7/e2, not d2; Re2 hits e8. */
  CHECK(chess_position_from_fen(&pos, "4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1") ==
        CHESS_OK);
  CHECK(chess_is_attacked(&pos, 52, CHESS_BLACK)); /* e7 */
  CHECK(chess_is_attacked(&pos, 12, CHESS_BLACK)); /* e2 */
  CHECK(!chess_is_attacked(&pos, 11, CHESS_BLACK)); /* d2 */
  CHECK(chess_is_attacked(&pos, 60, CHESS_WHITE)); /* e8 */
  CHECK(chess_is_attacked(&pos, 3, CHESS_WHITE)); /* d1, by Ke1 */

  /* Out-of-range square is never attacked. */
  CHECK(!chess_is_attacked(&pos, 64, CHESS_WHITE));
}

static void test_make_unmake_round_trip(void) {
  static const char *kFens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1",
    "4k3/8/8/8/8/5N2/8/4K3 w - - 0 1",
    "4k3/8/8/8/8/8/8/R3K3 w Q - 0 1",
    "6k1/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
    "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1",
    "4k3/8/8/8/8/8/1p6/R3K3 b - - 0 1",
  };
  size_t f;

  for (f = 0; f < sizeof(kFens) / sizeof(kFens[0]); f++) {
    chess_position pos;
    chess_move moves[CHESS_MAX_MOVES];
    size_t count = 0;
    size_t i;
    CHECK(chess_position_from_fen(&pos, kFens[f]) == CHESS_OK);
    CHECK(chess_generate_legal(&pos, moves, CHESS_MAX_MOVES, &count) ==
          CHESS_OK);
    for (i = 0; i < count; i++) {
      chess_position before = pos;
      chess_undo undo;
      CHECK(chess_make(&pos, moves[i], &undo) == CHESS_OK);
      chess_unmake(&pos, &undo);
      if (memcmp(&pos, &before, sizeof(pos)) != 0) {
        char uci[6];
        move_to_uci(moves[i], uci);
        printf("FAIL %s:%d: %s: %s not reversible\n", __FILE__, __LINE__,
               kFens[f], uci);
        failures++;
      }
    }
  }
}

static void test_double_push_state(void) {
  chess_position pos;
  chess_move e4 = {12, 28, CHESS_EMPTY}; /* e2(12) -> e4(28) */
  chess_undo undo;
  chess_move replies[CHESS_MAX_MOVES];
  size_t count = 0;

  CHECK(chess_position_from_fen(
            &pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  CHECK(chess_make(&pos, e4, &undo) == CHESS_OK);
  CHECK(pos.side_to_move == CHESS_BLACK);
  CHECK(pos.ep_square == 20); /* e3 */
  CHECK(pos.halfmove_clock == 0);
  CHECK(pos.fullmove_number == 1);
  CHECK(pos.board[12].type == CHESS_EMPTY);
  CHECK(pos.board[28].type == CHESS_PAWN &&
        pos.board[28].color == CHESS_WHITE);
  /* Black still has all 20 replies. */
  CHECK(chess_generate_legal(&pos, replies, CHESS_MAX_MOVES, &count) ==
        CHESS_OK);
  CHECK(count == 20);
  chess_unmake(&pos, &undo);
  CHECK(pos.side_to_move == CHESS_WHITE);
  CHECK(pos.ep_square == CHESS_NO_SQUARE);
}

static void test_illegal_make_rejected(void) {
  chess_position pos;
  chess_position before;
  chess_move bad = {12, 36, CHESS_EMPTY}; /* e2e5: pawn cannot jump */
  chess_undo undo;

  CHECK(chess_position_from_fen(
            &pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  before = pos;
  CHECK(chess_make(&pos, bad, &undo) != CHESS_OK);
  CHECK(memcmp(&pos, &before, sizeof(pos)) == 0);

  /* Same-square non-move and missing undo are also rejected. */
  bad.from = 12;
  bad.to = 12;
  CHECK(chess_make(&pos, bad, &undo) != CHESS_OK);
  CHECK(memcmp(&pos, &before, sizeof(pos)) == 0);

  {
    chess_move e4 = {12, 28, CHESS_EMPTY};
    CHECK(chess_make(&pos, e4, NULL) == CHESS_ERR_NULL);
    CHECK(chess_make(NULL, e4, &undo) == CHESS_ERR_NULL);
    CHECK(memcmp(&pos, &before, sizeof(pos)) == 0);
  }
  /* NULL undo on unmake is a documented no-op, never a crash. */
  chess_unmake(&pos, NULL);
  CHECK(memcmp(&pos, &before, sizeof(pos)) == 0);
}

/* Check-based FEN rejection (needs is_attacked, new in Task 3). */
static void test_check_position_fens(void) {
  chess_position pos;
  char buf[CHESS_FEN_MAX];

  /* Both kings in check: impossible. */
  CHECK(chess_position_from_fen(&pos, "4k3/8/8/1B6/8/8/4r3/4K3 w - - 0 1") !=
        CHESS_OK);
  /* Non-mover in check: black checked while white moves. */
  CHECK(chess_position_from_fen(&pos, "4k3/8/8/1B6/8/8/8/R3K3 w - - 0 1") !=
        CHESS_OK);
  /* Mover in check is a legal game position. */
  CHECK(chess_position_from_fen(&pos, "4k3/8/8/1B6/8/8/8/R3K3 b - - 0 1") ==
        CHESS_OK);
  CHECK(chess_position_to_fen(&pos, buf, sizeof(buf)) == CHESS_OK);
  CHECK(strcmp(buf, "4k3/8/8/1B6/8/8/8/R3K3 b - - 0 1") == 0);
}

/* ---- Task 4 RED: castling, en-passant capture, promotions ----
 * Positive sets must FAIL until specials are generated; the
 * through-check negative set already holds and stays green. */

static void test_castle_through_check_blocked(void) {
  /* Black Bc4 hits f1: short castle unavailable, everything else stays. */
  static const char *kWant[] = {
    "e1d1", "e1d2", "e1f2",
    "h1g1", "h1f1", "h1h2", "h1h3", "h1h4", "h1h5", "h1h6", "h1h7", "h1h8",
  };
  check_move_set("4k3/8/8/8/2b5/8/8/4K2R w K - 0 1", kWant,
                 sizeof(kWant) / sizeof(kWant[0]));
}

static void test_castle_kingside_allowed(void) {
  static const char *kWant[] = {
    "e1d1", "e1d2", "e1e2", "e1f2", "e1f1", "e1g1",
    "h1g1", "h1f1", "h1h2", "h1h3", "h1h4", "h1h5", "h1h6", "h1h7", "h1h8",
  };
  check_move_set("4k3/8/8/8/8/8/8/4K2R w K - 0 1", kWant,
                 sizeof(kWant) / sizeof(kWant[0]));
}

static void test_castle_queenside_allowed(void) {
  static const char *kWant[] = {
    "e1d1", "e1d2", "e1e2", "e1f2", "e1f1", "e1c1",
    "a1b1", "a1c1", "a1d1", "a1a2", "a1a3", "a1a4", "a1a5", "a1a6", "a1a7",
    "a1a8",
  };
  check_move_set("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1", kWant,
                 sizeof(kWant) / sizeof(kWant[0]));
}

static void test_castle_state(void) {
  chess_position pos;
  chess_position before;
  chess_move oo = {4, 6, CHESS_EMPTY}; /* e1 -> g1 */
  chess_undo undo;

  CHECK(chess_position_from_fen(&pos, "4k3/8/8/8/8/8/8/4K2R w K - 0 1") ==
        CHESS_OK);
  before = pos;
  CHECK(chess_make(&pos, oo, &undo) == CHESS_OK);
  CHECK(pos.board[6].type == CHESS_KING); /* king on g1 */
  CHECK(pos.board[5].type == CHESS_ROOK && /* rook on f1 */
        pos.board[5].color == CHESS_WHITE);
  CHECK(pos.board[4].type == CHESS_EMPTY && pos.board[7].type == CHESS_EMPTY);
  CHECK(pos.castling == 0);
  CHECK(pos.side_to_move == CHESS_BLACK);
  CHECK(pos.white_king == 6);
  chess_unmake(&pos, &undo);
  CHECK(memcmp(&pos, &before, sizeof(pos)) == 0);
}

static void test_rights_lost(void) {
  chess_position pos;
  chess_position before;
  chess_move take = {9, 0, CHESS_EMPTY}; /* b2 -> a1 takes the corner rook */
  chess_undo undo;

  /* Corner capture clears the right immediately. */
  CHECK(chess_position_from_fen(&pos, "4k3/8/8/8/8/8/1b6/R3K3 b Q - 0 1") ==
        CHESS_OK);
  before = pos;
  CHECK(chess_make(&pos, take, &undo) == CHESS_OK);
  CHECK(pos.castling == 0);
  chess_unmake(&pos, &undo);
  CHECK(memcmp(&pos, &before, sizeof(pos)) == 0);
  CHECK(pos.castling == CHESS_CASTLE_WQ);

  /* King move clears both rights of its color. */
  CHECK(chess_position_from_fen(&pos, "4k3/8/8/8/8/8/8/4K2R w K - 0 1") ==
        CHESS_OK);
  {
    chess_move kf = {4, 5, CHESS_EMPTY}; /* e1 -> f1 */
    CHECK(chess_make(&pos, kf, &undo) == CHESS_OK);
    CHECK(pos.castling == 0);
  }

  /* Rook leaving its corner clears its own right. */
  CHECK(chess_position_from_fen(&pos, "4k3/8/8/8/8/8/8/4K2R w K - 0 1") ==
        CHESS_OK);
  {
    chess_move rh = {7, 15, CHESS_EMPTY}; /* h1 -> h2 */
    CHECK(chess_make(&pos, rh, &undo) == CHESS_OK);
    CHECK((pos.castling & CHESS_CASTLE_WK) == 0);
  }
}

/* Capturing en passant onto d6 would uncover the e-file to Re8. */
static void test_ep_pin(void) {
  static const char *kWant[] = {
    "e1d1", "e1d2", "e1e2", "e1f1", "e1f2", "e5e6",
  };
  check_move_set("4r1k1/8/8/3pP3/8/8/8/4K3 w - d6 0 1", kWant,
                 sizeof(kWant) / sizeof(kWant[0]));
}

static void test_ep_capture_allowed(void) {
  static const char *kWant[] = {
    "e1d1", "e1d2", "e1e2", "e1f1", "e1f2", "e5e6", "e5d6",
  };
  check_move_set("6k1/8/8/3pP3/8/8/8/4K3 w - d6 0 1", kWant,
                 sizeof(kWant) / sizeof(kWant[0]));
}

static void test_ep_state(void) {
  chess_position pos;
  chess_position before;
  chess_move ep = {36, 43, CHESS_EMPTY}; /* e5(36) takes d6(43) e.p. */
  chess_undo undo;

  CHECK(chess_position_from_fen(&pos, "6k1/8/8/3pP3/8/8/8/4K3 w - d6 0 1") ==
        CHESS_OK);
  before = pos;
  CHECK(chess_make(&pos, ep, &undo) == CHESS_OK);
  CHECK(pos.board[43].type == CHESS_PAWN &&
        pos.board[43].color == CHESS_WHITE);
  CHECK(pos.board[36].type == CHESS_EMPTY);
  CHECK(pos.board[35].type == CHESS_EMPTY); /* victim d5 gone */
  CHECK(pos.ep_square == CHESS_NO_SQUARE);
  CHECK(pos.halfmove_clock == 0);
  chess_unmake(&pos, &undo);
  CHECK(memcmp(&pos, &before, sizeof(pos)) == 0);
}

static void test_promotion_push(void) {
  static const char *kWant[] = {
    "b7b8q", "b7b8r", "b7b8b", "b7b8n",
    "e1d1", "e1d2", "e1e2", "e1f1", "e1f2",
  };
  check_move_set("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1", kWant,
                 sizeof(kWant) / sizeof(kWant[0]));
}

static void test_promotion_capture(void) {
  /* b8 empty: quiet push and diagonal capture promote together. */
  static const char *kWant[] = {
    "b7b8q", "b7b8r", "b7b8b", "b7b8n",
    "b7a8q", "b7a8r", "b7a8b", "b7a8n",
    "e1d1", "e1d2", "e1e2", "e1f1", "e1f2",
  };
  check_move_set("r3k3/1P6/8/8/8/8/8/4K3 w - - 0 1", kWant,
                 sizeof(kWant) / sizeof(kWant[0]));
}

static void test_black_promotions(void) {
  /* Quiet push only: b1 empty. */
  static const char *kPush[] = {
    "b2b1q", "b2b1r", "b2b1b", "b2b1n",
    "e8d8", "e8d7", "e8e7", "e8f7", "e8f8",
  };
  /* Push plus capture of Ra1. */
  static const char *kTake[] = {
    "b2b1q", "b2b1r", "b2b1b", "b2b1n",
    "b2a1q", "b2a1r", "b2a1b", "b2a1n",
    "e8d8", "e8d7", "e8e7", "e8f7", "e8f8",
  };
  check_move_set("4k3/8/8/8/8/8/1p6/4K3 b - - 0 1", kPush,
                 sizeof(kPush) / sizeof(kPush[0]));
  check_move_set("4k3/8/8/8/8/8/1p6/R3K3 b - - 0 1", kTake,
                 sizeof(kTake) / sizeof(kTake[0]));
}

static void test_promotion_state(void) {
  chess_position pos;
  chess_position before;
  chess_move promo = {49, 57, CHESS_QUEEN}; /* b7 -> b8=Q */
  chess_undo undo;

  CHECK(chess_position_from_fen(&pos, "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1") ==
        CHESS_OK);
  before = pos;
  CHECK(chess_make(&pos, promo, &undo) == CHESS_OK);
  CHECK(pos.board[57].type == CHESS_QUEEN &&
        pos.board[57].color == CHESS_WHITE);
  CHECK(pos.board[49].type == CHESS_EMPTY);
  CHECK(pos.halfmove_clock == 0);
  CHECK(pos.side_to_move == CHESS_BLACK);
  chess_unmake(&pos, &undo);
  CHECK(memcmp(&pos, &before, sizeof(pos)) == 0);
}

/* ---- Task 4, step 4: seeded random-play properties ----
 * Fixed seed, no I/O: every step checks make/unmake reversibility,
 * structural validity, mover-king safety and FEN round-trip fidelity. */
static uint32_t lcg_next(uint32_t *state) {
  *state = *state * 1664525u + 1013904223u;
  return *state >> 8;
}

static void test_seeded_random_properties(void) {
  static const char *kStarts[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
  };
  uint32_t rng = 0xC0FFEEu;
  size_t g;

  for (g = 0; g < sizeof(kStarts) / sizeof(kStarts[0]); g++) {
    unsigned game;
    for (game = 0; game < 20; game++) {
      chess_position pos;
      unsigned ply;
      CHECK(chess_position_from_fen(&pos, kStarts[g]) == CHESS_OK);
      for (ply = 0; ply < 100; ply++) {
        chess_move moves[CHESS_MAX_MOVES];
        size_t count = 0;
        chess_move pick;
        chess_position before;
        chess_undo undo;
        char fen[CHESS_FEN_MAX];
        chess_position refen;
        uint8_t ksq;
        chess_color mover = pos.side_to_move;

        CHECK(chess_generate_legal(&pos, moves, CHESS_MAX_MOVES, &count) ==
              CHESS_OK);
        if (count == 0) {
          break; /* checkmate or stalemate: game over */
        }
        pick = moves[lcg_next(&rng) % count];
        before = pos;
        CHECK(chess_make(&pos, pick, &undo) == CHESS_OK);
        /* Mover must not have left its own king in check. */
        ksq = (mover == CHESS_WHITE) ? pos.white_king : pos.black_king;
        CHECK(!chess_is_attacked(&pos, ksq, pos.side_to_move));
        CHECK(chess_position_validate(&pos) == CHESS_OK);
        /* FEN must survive a full export/import cycle byte-exact. */
        CHECK(chess_position_to_fen(&pos, fen, sizeof(fen)) == CHESS_OK);
        CHECK(chess_position_from_fen(&refen, fen) == CHESS_OK);
        CHECK(memcmp(&pos, &refen, sizeof(pos)) == 0);
        chess_unmake(&pos, &undo);
        CHECK(memcmp(&pos, &before, sizeof(pos)) == 0);
        CHECK(chess_make(&pos, pick, &undo) == CHESS_OK);
      }
    }
  }
}

int main(void) {
  test_startpos_fields();
  test_bad_fen_leaves_output_untouched();
  test_fen_round_trip();
  test_rejects_bad_fens();
  test_null_and_small_buffer();
  test_startpos_move_set();
  test_pin_move_set();
  test_bishop_move_set();
  test_knight_move_set();
  test_is_attacked();
  test_make_unmake_round_trip();
  test_double_push_state();
  test_illegal_make_rejected();
  test_check_position_fens();
  test_castle_through_check_blocked();
  test_castle_kingside_allowed();
  test_castle_queenside_allowed();
  test_castle_state();
  test_rights_lost();
  test_ep_pin();
  test_ep_capture_allowed();
  test_ep_state();
  test_promotion_push();
  test_promotion_capture();
  test_black_promotions();
  test_promotion_state();
  test_seeded_random_properties();

  if (failures == 0) {
    printf("PASS: all test_core checks passed\n");
    return 0;
  }
  printf("FAILURES: %d\n", failures);
  return 1;
}
