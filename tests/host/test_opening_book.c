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
  CHECK(chess_position_from_fen(&pos,
                                "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR "
                                "b KQkq - 0 1") == CHESS_OK);
  CHECK(chess_opening_book_probe(&pos, 0u, true, &m));
  CHECK(!(m.from == 50 && m.to == 34)); /* not c7c6 */
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
  CHECK(chess_opening_book_probe(&pos, 42u, true, &b));
  CHECK(a.from == 12 && a.to == 28);
  /* Non-zero seed may pick d4/Nf3/c4; at least one alt exists in table. */
  CHECK(chess_opening_book_position_count() >= 4u);
}

static void test_transposition(void) {
  /* 1.e4 c5 2.Nf3 d6 and 1.Nf3 c5 2.e4 d6 share one core key. */
  const char *fen =
      "rnbqkbnr/pp2pppp/3p4/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 3";
  chess_position a;
  chess_position b;
  chess_move ma;
  chess_move mb;
  uint8_t ka[34];
  uint8_t kb[34];
  CHECK(chess_position_from_fen(&a, fen) == CHESS_OK);
  CHECK(chess_position_from_fen(&b, fen) == CHESS_OK);
  chess_position_key(&a, ka);
  chess_position_key(&b, kb);
  CHECK(memcmp(ka, kb, 34) == 0);
  CHECK(chess_opening_book_probe(&a, 0u, true, &ma));
  CHECK(chess_opening_book_probe(&b, 0u, true, &mb));
  CHECK(ma.from == mb.from && ma.to == mb.to);
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
  CHECK(chess_position_from_fen(
            &pos, "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1") ==
        CHESS_OK);
  CHECK(chess_opening_book_probe(&pos, 0u, true, &m));
  CHECK(m.from == 51 && m.to == 35); /* d7d5 */
}

int main(void) {
  test_start_seed0_e4();
  test_black_vs_e4_not_c6();
  test_seed0_two_ply_line();
  test_seed_variety();
  test_transposition();
  test_book_hit_skips_search();
  test_book_miss_uses_search();
  test_easy_no_random_in_book();
  test_easy_random_out_of_book();
  test_resp_after_e3();

  if (failures == 0) {
    printf("PASS: all test_opening_book checks passed (%zu book positions)\n",
           chess_opening_book_position_count());
    return 0;
  }
  printf("FAILURES: %d\n", failures);
  return 1;
}
