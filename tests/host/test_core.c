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

int main(void) {
  test_startpos_fields();
  test_bad_fen_leaves_output_untouched();
  test_fen_round_trip();
  test_rejects_bad_fens();
  test_null_and_small_buffer();

  if (failures == 0) {
    printf("PASS: all test_core checks passed\n");
    return 0;
  }
  printf("FAILURES: %d\n", failures);
  return 1;
}
