/* Task 5 RED: game outcomes, repetition, draw claims.
 * Expected to FAIL (link): chess_game_* does not exist yet. */
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

/* Minimal UCI reader for driving games: "e2e4", "e7e8q". */
static int parse_test_uci(const char *s, chess_move *m) {
  chess_piece_type promo = CHESS_EMPTY;
  size_t len = strlen(s);
  if ((len != 4 && len != 5) || s[0] < 'a' || s[0] > 'h' || s[2] < 'a' ||
      s[2] > 'h' || s[1] < '1' || s[1] > '8' || s[3] < '1' || s[3] > '8') {
    return -1;
  }
  if (len == 5) {
    switch (s[4]) {
    case 'q':
      promo = CHESS_QUEEN;
      break;
    case 'r':
      promo = CHESS_ROOK;
      break;
    case 'b':
      promo = CHESS_BISHOP;
      break;
    case 'n':
      promo = CHESS_KNIGHT;
      break;
    default:
      return -1;
    }
  }
  m->from = (uint8_t)((s[1] - '1') * 8 + (s[0] - 'a'));
  m->to = (uint8_t)((s[3] - '1') * 8 + (s[2] - 'a'));
  m->promotion = promo;
  return 0;
}

static void play(chess_game *game, const char *uci) {
  chess_move m;
  CHECK(parse_test_uci(uci, &m) == 0);
  CHECK(chess_game_apply(game, m) == CHESS_OK);
}

static void test_fools_mate(void) {
  chess_game game;
  chess_position frozen;
  CHECK(chess_game_init_fen(
            &game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  play(&game, "f2f3");
  CHECK(chess_game_status(&game) == CHESS_STATUS_ONGOING);
  play(&game, "e7e5");
  play(&game, "g2g4");
  play(&game, "d8h4");
  CHECK(chess_game_status(&game) == CHESS_STATUS_CHECKMATE_BLACK_WINS);
  /* Terminal: further applies are rejected without touching the game. */
  frozen = game.position;
  {
    chess_move m;
    CHECK(parse_test_uci("g1f3", &m) == 0);
    CHECK(chess_game_apply(&game, m) == CHESS_ERR_GAME_OVER);
  }
  CHECK(memcmp(&game.position, &frozen, sizeof(frozen)) == 0);
}

static void test_stalemate(void) {
  chess_game game;
  CHECK(chess_game_init_fen(&game, "k7/8/1Q6/8/8/8/8/7K b - - 0 1") ==
        CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_STALEMATE);
  {
    chess_move m;
    CHECK(parse_test_uci("a8a7", &m) == 0);
    CHECK(chess_game_apply(&game, m) == CHESS_ERR_GAME_OVER);
  }
}

static void test_mate_beats_seventy_five(void) {
  /* Mated on the 150th halfmove: checkmate outranks the 75-move rule. */
  chess_game game;
  CHECK(chess_game_init_fen(
            &game,
            "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 150 3") ==
        CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_CHECKMATE_BLACK_WINS);
}

static void test_dead_positions(void) {
  chess_game game;
  /* Bare kings. */
  CHECK(chess_game_init_fen(&game, "4k3/8/8/8/8/8/8/4K3 w - - 0 1") ==
        CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_DRAW_DEAD);
  /* K+N vs K. */
  CHECK(chess_game_init_fen(&game, "4k3/8/8/8/8/5N2/8/4K3 w - - 0 1") ==
        CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_DRAW_DEAD);
  /* K+B vs K. */
  CHECK(chess_game_init_fen(&game, "4k3/8/8/8/8/8/8/2B1K3 w - - 0 1") ==
        CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_DRAW_DEAD);
  /* Opposite-color bishops can still mate: not dead (e6 odd, c1 even). */
  CHECK(chess_game_init_fen(&game, "k7/8/4b3/8/8/8/8/2B4K w - - 0 1") ==
        CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_ONGOING);
  /* Two knights can still mate: not dead. */
  CHECK(chess_game_init_fen(&game, "4k3/8/8/8/8/5N2/5N2/4K3 w - - 0 1") ==
        CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_ONGOING);
}

/* Knight dance to controlled repetition counts. */
static void dance_to(chess_game *game, int halfmoves) {
  static const char *kCycle[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
  int i;
  for (i = 0; i < halfmoves; i++) {
    play(game, kCycle[i % 4]);
  }
}

static void test_threefold_claim(void) {
  chess_game game;
  CHECK(chess_game_init_fen(
            &game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  dance_to(&game, 4); /* startpos seen twice */
  CHECK(chess_game_status(&game) == CHESS_STATUS_ONGOING);
  CHECK(chess_game_claim_draw(&game, NULL) == CHESS_ERR_NO_CLAIM);
  CHECK(chess_game_status(&game) == CHESS_STATUS_ONGOING);
  dance_to(&game, 4); /* startpos seen three times */
  CHECK(chess_game_claim_draw(&game, NULL) == CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_DRAW_CLAIMED_THREEFOLD);
  {
    chess_move m;
    CHECK(parse_test_uci("e2e4", &m) == 0);
    CHECK(chess_game_apply(&game, m) == CHESS_ERR_GAME_OVER);
  }
}

static void test_intended_move_claim(void) {
  chess_game game;
  chess_move good;
  chess_move bad;
  CHECK(chess_game_init_fen(
            &game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  dance_to(&game, 7); /* one halfmove short of the third startpos */
  CHECK(parse_test_uci("f6g8", &good) == 0); /* completes it */
  CHECK(parse_test_uci("f6e4", &bad) == 0); /* reaches fresh ground */
  CHECK(chess_game_claim_draw(&game, &bad) == CHESS_ERR_NO_CLAIM);
  CHECK(chess_game_status(&game) == CHESS_STATUS_ONGOING);
  {
    chess_move illegal = {0, 0, CHESS_EMPTY};
    CHECK(chess_game_claim_draw(&game, &illegal) == CHESS_ERR_ILLEGAL_MOVE);
  }
  CHECK(chess_game_claim_draw(&game, &good) == CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_DRAW_CLAIMED_THREEFOLD);
}

static void test_fivefold_automatic(void) {
  chess_game game;
  CHECK(chess_game_init_fen(
            &game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  dance_to(&game, 7);
  CHECK(chess_game_status(&game) == CHESS_STATUS_ONGOING);
  play(&game, "f6g8"); /* third startpos: still only claimable */
  CHECK(chess_game_status(&game) == CHESS_STATUS_ONGOING);
  dance_to(&game, 8); /* two more cycles: fifth startpos, automatic */
  CHECK(chess_game_status(&game) == CHESS_STATUS_DRAW_FIVEFOLD);
}

static void test_fifty_claim_and_seventy_five_auto(void) {
  chess_game game;
  /* 99 -> quiet move -> 100: claimable, not automatic. */
  CHECK(chess_game_init_fen(
            &game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 99 1") ==
        CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_ONGOING);
  play(&game, "b1c3");
  CHECK(game.position.halfmove_clock == 100);
  CHECK(chess_game_status(&game) == CHESS_STATUS_ONGOING);
  CHECK(chess_game_claim_draw(&game, NULL) == CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_DRAW_CLAIMED_FIFTY);
  /* Imported at 150 with no mate: automatic immediately. */
  CHECK(chess_game_init_fen(
            &game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 150 1") ==
        CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_DRAW_SEVENTY_FIVE);
  /* 149 -> quiet move -> 150: automatic, then frozen. */
  CHECK(chess_game_init_fen(
            &game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 149 1") ==
        CHESS_OK);
  play(&game, "b1c3");
  CHECK(chess_game_status(&game) == CHESS_STATUS_DRAW_SEVENTY_FIVE);
  {
    chess_move m;
    CHECK(parse_test_uci("g8f6", &m) == 0);
    CHECK(chess_game_apply(&game, m) == CHESS_ERR_GAME_OVER);
  }
}

static void test_ep_key_normalization(void) {
  chess_game game;
  uint8_t with_ep[34];
  uint8_t without_ep[34];
  uint8_t effective[34];
  chess_position cleared;
  CHECK(chess_game_init_fen(
            &game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  play(&game, "e2e4");
  chess_position_key(&game.position, with_ep);
  cleared = game.position;
  cleared.ep_square = CHESS_NO_SQUARE;
  chess_position_key(&cleared, without_ep);
  /* No black pawn can take e3: the ep square must not split the key. */
  CHECK(memcmp(with_ep, without_ep, sizeof(with_ep)) == 0);
  /* With a live capture available the file is part of the key. */
  CHECK(chess_game_init_fen(&game, "6k1/8/8/3pP3/8/8/8/4K3 w - d6 0 1") ==
        CHESS_OK);
  chess_position_key(&game.position, effective);
  cleared = game.position;
  cleared.ep_square = CHESS_NO_SQUARE;
  chess_position_key(&cleared, without_ep);
  CHECK(memcmp(effective, without_ep, sizeof(effective)) != 0);
}

static void test_irreversible_resets_window(void) {
  chess_game game;
  CHECK(chess_game_init_fen(
            &game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  CHECK(game.history_len == 1);
  play(&game, "g1f3");
  CHECK(game.history_len == 2);
  play(&game, "e7e5"); /* pawn move: boundary, window restarts */
  CHECK(game.history_len == 1);
  play(&game, "f3e5"); /* capture: boundary again */
  CHECK(game.history_len == 1);
  CHECK(game.position.halfmove_clock == 0);
}

static void test_agree_and_resign(void) {
  chess_game game;
  chess_move m;
  CHECK(chess_game_init_fen(
            &game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  CHECK(chess_game_agree_draw(&game) == CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_DRAW_AGREED);
  CHECK(parse_test_uci("e2e4", &m) == 0);
  CHECK(chess_game_apply(&game, m) == CHESS_ERR_GAME_OVER);

  CHECK(chess_game_init_fen(
            &game, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  CHECK(chess_game_resign(&game, CHESS_WHITE) == CHESS_OK);
  CHECK(chess_game_status(&game) == CHESS_STATUS_RESIGN_BLACK_WINS);
}

static void test_init_errors(void) {
  chess_game game;
  chess_game poison;
  chess_position pos;
  memset(&pos, 0, sizeof(pos));
  memset(&poison, 0xAA, sizeof(poison));
  game = poison;
  CHECK(chess_game_init(NULL, &pos) == CHESS_ERR_NULL);
  CHECK(chess_game_init_fen(NULL, "x") == CHESS_ERR_NULL);
  CHECK(chess_game_init_fen(&game, "not a fen") == CHESS_ERR_BAD_FEN);
  CHECK(memcmp(&game, &poison, sizeof(game)) == 0);
  CHECK(chess_game_apply(NULL, (chess_move){0, 0, CHESS_EMPTY}) ==
        CHESS_ERR_NULL);
  CHECK(chess_game_status(NULL) == CHESS_STATUS_INVALID);
  CHECK(chess_game_claim_draw(NULL, NULL) == CHESS_ERR_NULL);
}

static uint32_t lcg_next(uint32_t *state) {
  *state = *state * 1664525u + 1013904223u;
  return *state >> 8;
}

/* Seeded random games: history stays bounded, terminal games freeze,
 * every apply is either accepted or cleanly rejected. */
static void test_random_games_stay_bounded(void) {
  static const char *kStarts[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
  };
  uint32_t rng = 0x5EEDu;
  size_t g;

  for (g = 0; g < sizeof(kStarts) / sizeof(kStarts[0]); g++) {
    unsigned game;
    for (game = 0; game < 10; game++) {
      chess_game state;
      unsigned ply;
      CHECK(chess_game_init_fen(&state, kStarts[g]) == CHESS_OK);
      for (ply = 0; ply < 200; ply++) {
        chess_move moves[CHESS_MAX_MOVES];
        size_t count = 0;
        chess_move pick;
        chess_status before = chess_game_status(&state);
        CHECK(chess_generate_legal(&state.position, moves, CHESS_MAX_MOVES,
                                   &count) == CHESS_OK);
        if (before != CHESS_STATUS_ONGOING) {
          chess_position frozen = state.position;
          pick = moves[0];
          if (count == 0) {
            pick.from = 0;
            pick.to = 1;
            pick.promotion = CHESS_EMPTY;
          }
          CHECK(chess_game_apply(&state, pick) == CHESS_ERR_GAME_OVER);
          CHECK(memcmp(&state.position, &frozen, sizeof(frozen)) == 0);
          break;
        }
        if (count == 0) {
          break;
        }
        CHECK(state.history_len >= 1 &&
              state.history_len <= CHESS_HISTORY_MAX);
        pick = moves[lcg_next(&rng) % count];
        CHECK(chess_game_apply(&state, pick) == CHESS_OK);
        CHECK(state.history_len >= 1 &&
              state.history_len <= CHESS_HISTORY_MAX);
      }
    }
  }
}

int main(void) {
  test_fools_mate();
  test_stalemate();
  test_mate_beats_seventy_five();
  test_dead_positions();
  test_threefold_claim();
  test_intended_move_claim();
  test_fivefold_automatic();
  test_fifty_claim_and_seventy_five_auto();
  test_ep_key_normalization();
  test_irreversible_resets_window();
  test_agree_and_resign();
  test_init_errors();
  test_random_games_stay_bounded();

  if (failures == 0) {
    printf("PASS: all test_game checks passed\n");
    return 0;
  }
  printf("FAILURES: %d\n", failures);
  return 1;
}
