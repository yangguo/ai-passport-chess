/* Task 9/10 RED-then-GREEN: worker job with fake clock/cancel.
 * Deterministic: no threads, no wall time. Engine calls are real
 * (bounded); time only moves when the test advances it. */
#include <stdio.h>
#include <string.h>

#include "chess_ai.h"
#include "chess_core.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      failures++;                                                            \
    }                                                                        \
  } while (0)

#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

typedef struct fake_clock {
    uint64_t now;
    uint64_t step_per_callback;
    unsigned yields;
} fake_clock;

static uint64_t fake_now(void *ctx) {
    fake_clock *c = (fake_clock *)ctx;
    c->now += c->step_per_callback;
    return c->now;
}

static void fake_yield(void *ctx) {
    ((fake_clock *)ctx)->yields++;
}

/* Yield hook that cancels its job after a fixed number of yields:
 * deterministic mid-search cancellation on one thread. */
typedef struct canceller {
    unsigned calls;
    unsigned fire_at;
    chess_ai_job *job;
} canceller;

static void cancel_yield(void *ctx) {
    canceller *c = (canceller *)ctx;
    if (++c->calls >= c->fire_at) {
        c->job->cancel = true;
    }
}

static void make_request(chess_ai_request *req, const char *fen,
                         chess_ai_level level, fake_clock *clock) {
    memset(req, 0, sizeof(*req));
    CHECK(chess_position_from_fen(&req->position, fen) == CHESS_OK);
    req->generation = 7;
    req->level = level;
    chess_ai_level_budgets(level, &req->deadline_ms, &req->node_max,
                           &req->depth_max);
    req->easy_seed = 1234;
    (void)clock;
}

static void test_budgets_sane(void) {
    uint32_t ms;
    uint32_t nodes;
    unsigned depth;
    chess_ai_level_budgets(CHESS_AI_EASY, &ms, &nodes, &depth);
    CHECK(ms == 100u);
    chess_ai_level_budgets(CHESS_AI_NORMAL, &ms, &nodes, &depth);
    CHECK(ms == 1500u);
    chess_ai_level_budgets(CHESS_AI_HARD, &ms, &nodes, &depth);
    CHECK(ms == 5000u);
    chess_ai_level_budgets(CHESS_AI_NORMAL, NULL, NULL, NULL);
}

static void test_ok_search(void) {
    chess_ai_request req;
    chess_ai_job job;
    fake_clock clock;
    memset(&clock, 0, sizeof(clock));
    memset(&job, 0, sizeof(job));
    make_request(&req, START_FEN, CHESS_AI_NORMAL, &clock);
    /* Time flows (yield path exercised) but the deadline is out of
     * reach, so the search still completes normally. */
    req.deadline_ms = 4000000000u;
    clock.step_per_callback = 1;
    job.clock = fake_now;
    job.clock_ctx = &clock;
    job.yield = fake_yield;
    job.yield_ctx = &clock;
    chess_ai_run_job(&job, &req);
    CHECK(job.outcome == CHESS_AI_OK);
    CHECK(clock.yields > 0);
    CHECK(job.has_best);
    CHECK(job.result_generation == 7);
    CHECK(job.has_fallback);
    /* Best must be core-legal. */
    {
        chess_move legal[CHESS_MAX_MOVES];
        size_t count = 0;
        size_t i;
        bool found = false;
        CHECK(chess_generate_legal(&req.position, legal, CHESS_MAX_MOVES,
                                   &count) == CHESS_OK);
        for (i = 0; i < count; i++) {
            if (legal[i].from == job.best.from &&
                legal[i].to == job.best.to &&
                legal[i].promotion == job.best.promotion) {
                found = true;
            }
        }
        CHECK(found);
    }
}

static void test_timeout_keeps_fallback(void) {
    chess_ai_request req;
    chess_ai_job job;
    fake_clock clock;
    memset(&clock, 0, sizeof(clock));
    memset(&job, 0, sizeof(job));
    /* Clock already past any relative deadline: the started_at tick
     * alone exceeds it, so TIMEOUT holds however often the engine
     * calls back (possibly never). */
    make_request(&req, START_FEN, CHESS_AI_NORMAL, &clock);
    clock.step_per_callback = 1000;
    job.clock = fake_now;
    job.clock_ctx = &clock;
    chess_ai_run_job(&job, &req);
    CHECK(job.outcome == CHESS_AI_TIMEOUT);
    CHECK(!job.has_best);
    CHECK(job.has_fallback);
}

/* A normal deadline is the end of iterative deepening, not an engine
 * failure. Keep the completed tactical result instead of legal[0]. */
static void test_deadline_keeps_searched_capture(void) {
    chess_ai_request req;
    chess_ai_job job;
    fake_clock clock;
    chess_position probe;
    chess_undo undo;
    memset(&clock, 0, sizeof(clock));
    memset(&job, 0, sizeof(job));
    make_request(&req, "6k1/8/8/8/4q3/8/8/4R1K1 w - - 0 1",
                 CHESS_AI_NORMAL, &clock);
    req.deadline_ms = 5000;
    req.node_max = 100000000;
    req.depth_max = 30;
    clock.step_per_callback = 1;
    job.clock = fake_now;
    job.clock_ctx = &clock;
    chess_ai_run_job(&job, &req);
    CHECK(clock.now >= job.deadline_at_ms);
    CHECK(job.outcome == CHESS_AI_OK);
    CHECK(job.has_best);
    if (job.has_best) {
        CHECK(job.best.from == 4 && job.best.to == 28); /* Rxe4 */
        probe = req.position;
        CHECK(chess_make(&probe, job.best, &undo) == CHESS_OK);
    }
    /* A new search stopped immediately must not reuse this checkpoint. */
    memset(&job, 0, sizeof(job));
    memset(&clock, 0, sizeof(clock));
    clock.step_per_callback = 1;
    req.deadline_ms = 0;
    job.clock = fake_now;
    job.clock_ctx = &clock;
    chess_ai_run_job(&job, &req);
    CHECK(job.outcome == CHESS_AI_TIMEOUT);
    CHECK(!job.has_best && job.has_fallback);
}

static void test_cancel_before_search(void) {
    chess_ai_request req;
    chess_ai_job job;
    memset(&job, 0, sizeof(job));
    make_request(&req, START_FEN, CHESS_AI_NORMAL, NULL);
    job.cancel = true;
    chess_ai_run_job(&job, &req);
    CHECK(job.outcome == CHESS_AI_CANCELLED);
    CHECK(!job.has_best);
    CHECK(!job.has_fallback); /* cancelled: never sneak a move */
}

static void test_cancel_mid_search(void) {
    chess_ai_request req;
    chess_ai_job job;
    fake_clock clock;
    canceller stopper;
    memset(&clock, 0, sizeof(clock));
    memset(&job, 0, sizeof(job));
    memset(&stopper, 0, sizeof(stopper));
    make_request(&req, START_FEN, CHESS_AI_HARD, &clock);
    /* Time flows but the deadline is unreachable: only the cancel
     * injected from the yield hook may stop this search. */
    req.deadline_ms = 4000000000u;
    clock.step_per_callback = 1;
    stopper.fire_at = 3;
    stopper.job = &job;
    job.clock = fake_now;
    job.clock_ctx = &clock;
    job.yield = cancel_yield;
    job.yield_ctx = &stopper;
    chess_ai_run_job(&job, &req);
    CHECK(stopper.calls >= 3);
    CHECK(job.outcome == CHESS_AI_CANCELLED);
    CHECK(!job.has_best);
}

static void test_no_move_terminal(void) {
    chess_ai_request req;
    chess_ai_job job;
    memset(&job, 0, sizeof(job));
    make_request(&req, "k7/8/1Q6/8/8/8/8/7K b - - 0 1", CHESS_AI_NORMAL,
                 NULL);
    chess_ai_run_job(&job, &req);
    CHECK(job.outcome == CHESS_AI_NO_MOVE);
    CHECK(!job.has_best && !job.has_fallback);
}

static void test_easy_replayable(void) {
    chess_ai_request req;
    chess_ai_job first;
    chess_ai_job second;
    fake_clock clock;
    int non_best = 0;
    int trial;
    memset(&clock, 0, sizeof(clock));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    make_request(&req, START_FEN, CHESS_AI_EASY, &clock);
    first.clock = fake_now;
    first.clock_ctx = &clock;
    second.clock = fake_now;
    second.clock_ctx = &clock;
    chess_ai_run_job(&first, &req);
    CHECK(first.outcome == CHESS_AI_OK);
    chess_ai_run_job(&second, &req);
    CHECK(second.best.from == first.best.from &&
          second.best.to == first.best.to &&
          second.best.promotion == first.best.promotion);
    /* Across seeds the sampler must sometimes leave best. */
    for (trial = 0; trial < 100; trial++) {
        chess_ai_job probe;
        memset(&probe, 0, sizeof(probe));
        req.easy_seed = (uint32_t)(trial + 1);
        probe.clock = fake_now;
        probe.clock_ctx = &clock;
        chess_ai_run_job(&probe, &req);
        if (probe.has_best &&
            (probe.best.from != first.best.from ||
             probe.best.to != first.best.to)) {
            non_best++;
        }
    }
    CHECK(non_best > 0);
}

static void test_avoid_mate_in_one(void) {
    /* After 1.f3 e5 White must not hang g4 (Qh4#) nor allow any
     * immediate mate: every Black reply must leave White alive. */
    chess_ai_request req;
    chess_ai_job job;
    fake_clock clock;
    chess_position after;
    chess_undo undo;
    chess_move replies[CHESS_MAX_MOVES];
    size_t count = 0;
    size_t i;
    memset(&clock, 0, sizeof(clock));
    memset(&job, 0, sizeof(job));
    make_request(&req,
                 "rnbqkbnr/pppp1ppp/8/4p3/8/5P2/PPPPP2P/RNBQKBNR w KQkq - 0 2",
                 CHESS_AI_NORMAL, &clock);
    job.clock = fake_now;
    job.clock_ctx = &clock;
    chess_ai_run_job(&job, &req);
    CHECK(job.outcome == CHESS_AI_OK);
    CHECK(!(job.best.from == 14 && job.best.to == 30)); /* not g2g4 */
    after = req.position;
    CHECK(chess_make(&after, job.best, &undo) == CHESS_OK);
    CHECK(chess_generate_legal(&after, replies, CHESS_MAX_MOVES, &count) ==
          CHESS_OK);
    for (i = 0; i < count; i++) {
        chess_position probe = after;
        chess_undo u2;
        /* Mate for Black = White mated: no replies and in check. */
        {
            chess_move wq[CHESS_MAX_MOVES];
            size_t wn = 0;
            bool wcheck;
            CHECK(chess_make(&probe, replies[i], &u2) == CHESS_OK);
            CHECK(chess_generate_legal(&probe, wq, CHESS_MAX_MOVES, &wn) ==
                  CHESS_OK);
            wcheck = chess_is_attacked(&probe, probe.white_king, CHESS_BLACK);
            CHECK(!(wn == 0 && wcheck));
        }
    }
}

static void test_apply_checked(void) {
    chess_game game;
    chess_move m = {12, 28, CHESS_EMPTY};
    CHECK(chess_game_init_fen(&game, START_FEN) == CHESS_OK);
    CHECK(chess_ai_apply_checked(&game, m, 7, 7) == CHESS_OK);
    CHECK(chess_ai_apply_checked(&game, m, 6, 7) == CHESS_ERR_STALE);
    {
        chess_move bad = {12, 36, CHESS_EMPTY};
        CHECK(chess_ai_apply_checked(&game, bad, 7, 7) ==
              CHESS_ERR_ILLEGAL_MOVE);
    }
    CHECK(chess_ai_apply_checked(NULL, m, 7, 7) == CHESS_ERR_NULL);
    /* Terminal games reject even fresh generations. */
    CHECK(chess_game_init_fen(&game, "k7/8/1Q6/8/8/8/8/7K b - - 0 1") ==
          CHESS_OK);
    CHECK(chess_ai_apply_checked(&game, m, 7, 7) == CHESS_ERR_GAME_OVER);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--deadline-only") == 0) {
        test_deadline_keeps_searched_capture();
        return failures != 0;
    }
    test_budgets_sane();
    test_ok_search();
    test_timeout_keeps_fallback();
    test_deadline_keeps_searched_capture();
    test_cancel_before_search();
    test_cancel_mid_search();
    test_no_move_terminal();
    test_easy_replayable();
    test_avoid_mate_in_one();
    test_apply_checked();

    if (failures == 0) {
        printf("PASS: all test_ai_worker checks passed\n");
        return 0;
    }
    printf("FAILURES: %d\n", failures);
    return 1;
}
