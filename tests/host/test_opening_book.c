/* Opening book: seed 0 main line, transpositions, Easy in-book, restore path. */
#include <stdio.h>
#include <string.h>

#include "chess_ai.h"
#include "chess_core.h"
#include "opening_book.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      failures++;                                                            \
    }                                                                        \
  } while (0)

#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

static uint8_t sq(char file, unsigned rank) {
  return (uint8_t)((rank - 1u) * 8u + (unsigned)(file - 'a'));
}

static bool uci_to_core_move(const chess_position *pos, const char *uci,
                             chess_move *out) {
  unsigned ff;
  unsigned fr;
  unsigned tf;
  unsigned tr;
  chess_move legal[CHESS_MAX_MOVES];
  size_t count = 0;
  size_t i;
  if (uci == NULL || strlen(uci) < 4u) {
    return false;
  }
  ff = (unsigned)(uci[0] - 'a');
  fr = (unsigned)(uci[1] - '1');
  tf = (unsigned)(uci[2] - 'a');
  tr = (unsigned)(uci[3] - '1');
  if (ff > 7u || fr > 7u || tf > 7u || tr > 7u) {
    return false;
  }
  out->from = (uint8_t)(fr * 8u + ff);
  out->to = (uint8_t)(tr * 8u + tf);
  out->promotion = CHESS_EMPTY;
  if (chess_generate_legal(pos, legal, CHESS_MAX_MOVES, &count) != CHESS_OK) {
    return false;
  }
  for (i = 0; i < count; i++) {
    if (legal[i].from == out->from && legal[i].to == out->to &&
        legal[i].promotion == out->promotion) {
      return true;
    }
  }
  return false;
}

static bool play_uci(chess_position *pos, const char *uci) {
  chess_move m;
  chess_undo undo;
  if (!uci_to_core_move(pos, uci, &m)) {
    return false;
  }
  return chess_make(pos, m, &undo) == CHESS_OK;
}

static void test_start_seed0_e4(void) {
  chess_position pos;
  chess_move m;
  CHECK(chess_position_from_fen(&pos, START_FEN) == CHESS_OK);
  CHECK(chess_opening_book_probe(&pos, 0u, true, &m));
  CHECK(m.from == 12 && m.to == 28); /* e2e4 */
  CHECK(chess_ai_suggest(&pos, 200000u, 4u, &m) == 0);
  CHECK(m.from == 12 && m.to == 28);
}

static void test_black_vs_e4_not_c6(void) {
  chess_position pos;
  chess_move m;
  const uint8_t c7 = sq('c', 7u);
  const uint8_t c6 = sq('c', 6u);
  uint32_t seed;
  CHECK(chess_position_from_fen(&pos,
                                "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR "
                                "b KQkq - 0 1") == CHESS_OK);
  for (seed = 0u; seed < 512u; seed++) {
    CHECK(chess_opening_book_probe(&pos, seed, true, &m));
    CHECK(!(m.from == c7 && m.to == c6)); /* ...c6 excluded (4-move cap) */
  }
}

static void test_black_vs_e4_e5(void) {
  chess_position pos;
  chess_move m;
  CHECK(chess_position_from_fen(&pos,
                                "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR "
                                "b KQkq - 0 1") == CHESS_OK);
  CHECK(chess_opening_book_probe(&pos, 0u, true, &m));
  CHECK(m.from == 52 && m.to == 36); /* e7e5 */
}

static void test_seed0_two_ply_line(void) {
  chess_position pos;
  chess_undo undo;
  chess_move m;
  CHECK(chess_position_from_fen(&pos, START_FEN) == CHESS_OK);
  CHECK(chess_opening_book_probe(&pos, 0u, true, &m));
  CHECK(chess_make(&pos, m, &undo) == CHESS_OK);
  CHECK(chess_opening_book_probe(&pos, 0u, true, &m));
  CHECK(m.from == 52 && m.to == 36); /* e7e5 */
}

static void test_seed_variety(void) {
  chess_position pos;
  chess_move a;
  chess_move b;
  CHECK(chess_position_from_fen(&pos, START_FEN) == CHESS_OK);
  CHECK(chess_opening_book_probe(&pos, 0u, true, &a));
  CHECK(chess_opening_book_probe(&pos, 3u, true, &b));
  CHECK(a.from == 12 && a.to == 28);
  CHECK(b.from == 11 && b.to == 27); /* seed 3: d2d4 (Lichess explorer weights) */
}

static void test_transposition(void) {
  /* 1.e4 c5 2.Nf3 d6 and 1.Nf3 d6 2.e4 c5 → same key; seed 0 plays d2d4. */
  chess_position a;
  chess_position b;
  chess_move ma;
  chess_move mb;
  uint8_t ka[34];
  uint8_t kb[34];
  CHECK(chess_position_from_fen(&a, START_FEN) == CHESS_OK);
  CHECK(chess_position_from_fen(&b, START_FEN) == CHESS_OK);
  CHECK(play_uci(&a, "e2e4"));
  CHECK(play_uci(&a, "c7c5"));
  CHECK(play_uci(&a, "g1f3"));
  CHECK(play_uci(&a, "d7d6"));
  CHECK(play_uci(&b, "g1f3"));
  CHECK(play_uci(&b, "d7d6"));
  CHECK(play_uci(&b, "e2e4"));
  CHECK(play_uci(&b, "c7c5"));
  chess_position_key(&a, ka);
  chess_position_key(&b, kb);
  CHECK(memcmp(ka, kb, 34) == 0);
  CHECK(chess_opening_book_probe(&a, 0u, true, &ma));
  CHECK(chess_opening_book_probe(&b, 0u, true, &mb));
  CHECK(ma.from == 11 && ma.to == 27);
  CHECK(mb.from == ma.from && mb.to == ma.to);
}

static void test_book_miss_uses_search(void) {
  chess_ai_request req;
  chess_ai_job job;
  chess_position pos;
  CHECK(chess_position_from_fen(
            &pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") ==
        CHESS_OK);
  memset(&req, 0, sizeof(req));
  req.position = pos;
  req.level = CHESS_AI_NORMAL;
  req.book_enabled = false;
  chess_ai_level_budgets(CHESS_AI_NORMAL, &req.deadline_ms, &req.node_max,
                         &req.depth_max);
  memset(&job, 0, sizeof(job));
  chess_ai_run_job(&job, &req);
  CHECK(job.outcome == CHESS_AI_OK);
  CHECK(job.callback_count > 0);
}

typedef struct fake_clock {
  uint64_t now;
} fake_clock;

static uint64_t fake_now(void *ctx) {
  return ((fake_clock *)ctx)->now;
}

static void test_book_hit_skips_search(void) {
  chess_ai_request req;
  chess_ai_job job;
  fake_clock clock;
  memset(&clock, 0, sizeof(clock));
  memset(&req, 0, sizeof(req));
  CHECK(chess_position_from_fen(&req.position, START_FEN) == CHESS_OK);
  req.level = CHESS_AI_NORMAL;
  req.book_enabled = true;
  req.book_seed = 0;
  chess_ai_level_budgets(CHESS_AI_NORMAL, &req.deadline_ms, &req.node_max,
                         &req.depth_max);
  memset(&job, 0, sizeof(job));
  job.clock = fake_now;
  job.clock_ctx = &clock;
  chess_ai_run_job(&job, &req);
  CHECK(job.outcome == CHESS_AI_OK);
  CHECK(job.callback_count == 0);
  CHECK(job.best.from == 12 && job.best.to == 28);
}

static void test_easy_no_random_in_book(void) {
  chess_ai_request req;
  chess_ai_job job;
  fake_clock clock;
  int trial;
  memset(&clock, 0, sizeof(clock));
  memset(&req, 0, sizeof(req));
  CHECK(chess_position_from_fen(&req.position, START_FEN) == CHESS_OK);
  req.level = CHESS_AI_EASY;
  req.book_enabled = true;
  req.book_seed = 0;
  chess_ai_level_budgets(CHESS_AI_EASY, &req.deadline_ms, &req.node_max,
                         &req.depth_max);
  for (trial = 0; trial < 50; trial++) {
    memset(&job, 0, sizeof(job));
    req.easy_seed = (uint32_t)(trial + 1);
    job.clock = fake_now;
    job.clock_ctx = &clock;
    chess_ai_run_job(&job, &req);
    CHECK(job.outcome == CHESS_AI_OK);
    CHECK(job.best.from == 12 && job.best.to == 28);
  }
}

static void test_easy_random_out_of_book(void) {
  chess_ai_request req;
  chess_ai_job first;
  fake_clock clock;
  int non_best = 0;
  int trial;
  memset(&clock, 0, sizeof(clock));
  memset(&req, 0, sizeof(req));
  CHECK(chess_position_from_fen(&req.position, START_FEN) == CHESS_OK);
  req.level = CHESS_AI_EASY;
  req.book_enabled = false;
  req.easy_seed = 99;
  chess_ai_level_budgets(CHESS_AI_EASY, &req.deadline_ms, &req.node_max,
                         &req.depth_max);
  memset(&first, 0, sizeof(first));
  first.clock = fake_now;
  first.clock_ctx = &clock;
  chess_ai_run_job(&first, &req);
  CHECK(first.outcome == CHESS_AI_OK);
  for (trial = 0; trial < 100; trial++) {
    chess_ai_job probe;
    memset(&probe, 0, sizeof(probe));
    req.easy_seed = (uint32_t)(trial + 1);
    probe.clock = fake_now;
    probe.clock_ctx = &clock;
    chess_ai_run_job(&probe, &req);
    if (probe.has_best &&
        (probe.best.from != first.best.from || probe.best.to != first.best.to)) {
      non_best++;
    }
  }
  CHECK(non_best > 0);
}

static void test_resp_after_e3(void) {
  chess_position pos;
  chess_move m;
  CHECK(chess_position_from_fen(&pos, START_FEN) == CHESS_OK);
  CHECK(play_uci(&pos, "e2e3"));
  CHECK(chess_opening_book_probe(&pos, 0u, true, &m));
  CHECK(m.from == 51 && m.to == 35); /* d7d5 */
}

static void test_resp_after_c3(void) {
  chess_position pos;
  chess_move m;
  CHECK(chess_position_from_fen(&pos, START_FEN) == CHESS_OK);
  CHECK(play_uci(&pos, "c2c3"));
  CHECK(chess_opening_book_probe(&pos, 0u, true, &m));
  CHECK(m.from == 51 && m.to == 35); /* d7d5 */
}

static void test_search_then_book_probe(void) {
  /* Regression: many engine searches must not break later book probes
   * (device path: search a move, later transposition back into book). */
  chess_ai_request req;
  chess_ai_job job;
  fake_clock clock;
  chess_position pos;
  chess_move m;
  int i;
  memset(&clock, 0, sizeof(clock));
  memset(&req, 0, sizeof(req));
  CHECK(chess_position_from_fen(&req.position, START_FEN) == CHESS_OK);
  req.level = CHESS_AI_EASY;
  req.book_enabled = false;
  req.easy_seed = 99;
  chess_ai_level_budgets(CHESS_AI_EASY, &req.deadline_ms, &req.node_max,
                         &req.depth_max);
  for (i = 0; i < 120; i++) {
    memset(&job, 0, sizeof(job));
    req.easy_seed = (uint32_t)(i + 1);
    job.clock = fake_now;
    job.clock_ctx = &clock;
    chess_ai_run_job(&job, &req);
    CHECK(job.outcome == CHESS_AI_OK);
    CHECK(job.callback_count > 0);
  }
  CHECK(chess_position_from_fen(&pos, START_FEN) == CHESS_OK);
  CHECK(chess_opening_book_probe(&pos, 0u, true, &m));
  CHECK(m.from == 12 && m.to == 28);
  CHECK(chess_opening_book_probe(&pos, 3u, true, &m));
  CHECK(m.from == 11 && m.to == 27);
  CHECK(chess_opening_book_probe(&pos, 8u, true, &m));
  CHECK(m.from == 6 && m.to == 21);
  CHECK(chess_opening_book_probe(&pos, 39u, true, &m));
  CHECK(m.from == 10 && m.to == 26);
}

static void test_root_first_move_weights(void) {
  chess_position pos;
  chess_move m;
  CHECK(chess_position_from_fen(&pos, START_FEN) == CHESS_OK);
  CHECK(chess_opening_book_probe(&pos, 0u, true, &m));
  CHECK(m.from == 12 && m.to == 28); /* e4 wins at 100 */
  CHECK(chess_opening_book_probe(&pos, 3u, true, &m));
  CHECK(m.from == 11 && m.to == 27); /* d4 */
  CHECK(chess_opening_book_probe(&pos, 8u, true, &m));
  CHECK(m.from == 6 && m.to == 21); /* Nf3 */
  CHECK(chess_opening_book_probe(&pos, 39u, true, &m));
  CHECK(m.from == 10 && m.to == 26); /* c4 */
}

static int run_suite(int engine_search_first) {
  int before = failures;
  if (engine_search_first) {
    test_book_hit_skips_search();
    test_book_miss_uses_search();
    test_easy_no_random_in_book();
    test_easy_random_out_of_book();
    test_search_then_book_probe();
  }
  test_start_seed0_e4();
  test_black_vs_e4_not_c6();
  test_black_vs_e4_e5();
  test_seed0_two_ply_line();
  test_seed_variety();
  test_transposition();
  test_resp_after_e3();
  test_resp_after_c3();
  test_root_first_move_weights();
  if (!engine_search_first) {
    test_book_hit_skips_search();
    test_book_miss_uses_search();
    test_easy_no_random_in_book();
    test_easy_random_out_of_book();
    test_search_then_book_probe();
  }
  return failures != before;
}

int main(void) {
  if (run_suite(0) != 0 || run_suite(1) != 0) {
    printf("FAILURES: %d\n", failures);
    return 1;
  }
  printf("PASS: all test_opening_book checks passed (%zu book positions, "
         "both orderings)\n",
         chess_opening_book_position_count());
  return 0;
}
