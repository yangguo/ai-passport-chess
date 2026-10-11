/* Firmware AI worker protocol.
 *
 * chess_app runs at tskIDLE_PRIORITY+1. The AI worker is priority 3, so
 * xQueueSend switches to it before chess_ai_task_request returns. A book
 * reply never blocks, so the worker used to finish and clear s_busy, and
 * request() then set s_busy again. Every later request was refused: the
 * computer moved once and never moved again.
 *
 * The queue stub models that preemption: send runs the worker until it
 * blocks on the next receive.
 */
#define _POSIX_C_SOURCE 200809L

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "chess_ai.h"
#include "chess_ai_task.h"
#include "chess_core.h"
#include "mcu-max.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      failures++;                                                            \
    }                                                                        \
  } while (0)

#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

static TaskFunction_t s_worker_fn;
static jmp_buf s_blocked;
static int s_running;
static chess_ai_request s_queued;
static int s_has_item;
static uint64_t s_timer_us;

int64_t esp_timer_get_time(void) {
  /* One millisecond per read so a firmware deadline stops a real search
   * after budget callbacks instead of a multi-second host run. Book
   * replies never read the clock. */
  s_timer_us += 1000;
  return (int64_t)s_timer_us;
}

QueueHandle_t xQueueCreate(unsigned n, size_t size) {
  (void)n;
  (void)size;
  return (QueueHandle_t)1;
}

void vQueueDelete(QueueHandle_t q) { (void)q; }

int xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                void *arg, unsigned prio, TaskHandle_t *out) {
  (void)name;
  (void)stack;
  (void)arg;
  (void)prio;
  s_worker_fn = fn;
  if (out != NULL) {
    *out = (TaskHandle_t)1;
  }
  return pdPASS;
}

void vTaskDelay(unsigned ticks) { (void)ticks; }

unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task) {
  (void)task;
  return 1000;
}

int xQueueSend(QueueHandle_t q, const void *item, unsigned ticks) {
  (void)q;
  (void)ticks;
  s_queued = *(const chess_ai_request *)item;
  s_has_item = 1;
  if (s_worker_fn != NULL && !s_running) {
    s_running = 1;
    if (setjmp(s_blocked) == 0) {
      s_worker_fn(NULL);
    }
    s_running = 0;
  }
  return pdTRUE;
}

int xQueueReceive(QueueHandle_t q, void *item, unsigned ticks) {
  (void)q;
  (void)ticks;
  if (s_has_item) {
    *(chess_ai_request *)item = s_queued;
    s_has_item = 0;
    return pdTRUE;
  }
  longjmp(s_blocked, 1);
}

static void fill_request(chess_ai_request *req, const chess_position *pos,
                         chess_ai_level level) {
  memset(req, 0, sizeof(*req));
  req->position = *pos;
  req->generation = 3;
  req->level = level;
  chess_ai_level_budgets(level, &req->deadline_ms, &req->node_max,
                         &req->depth_max);
  req->book_enabled = true;
  req->book_seed = 0;
  req->easy_seed = 1;
}

static int move_ok(const chess_position *pos, chess_move m) {
  chess_position probe = *pos;
  chess_undo undo;
  return chess_make(&probe, m, &undo) == CHESS_OK;
}

/* Two opening replies at every difficulty. The second request is the
 * one the device dropped after the book move. */
static void test_second_request_after_book(void) {
  chess_ai_level levels[3] = {CHESS_AI_EASY, CHESS_AI_NORMAL, CHESS_AI_HARD};
  int i;
  CHECK(chess_ai_task_start());
  for (i = 0; i < 3; i++) {
    chess_game game;
    chess_ai_request req;
    chess_ai_result_event ev;
    chess_position after;
    CHECK(chess_game_init_fen(&game, START_FEN) == CHESS_OK);
    fill_request(&req, &game.position, levels[i]);
    CHECK(chess_ai_task_request(&req));
    CHECK(!chess_ai_task_busy());
    CHECK(chess_ai_task_take_result(&ev));
    CHECK(ev.outcome == CHESS_AI_OK && ev.has_move);
    CHECK(ev.move.from == 12 && ev.move.to == 28); /* e2e4 */
    CHECK(chess_game_apply(&game, ev.move) == CHESS_OK);
    /* Human answer, then the computer must accept another search. */
    {
      chess_move human = {52, 36, CHESS_EMPTY}; /* e7e5 */
      CHECK(chess_game_apply(&game, human) == CHESS_OK);
    }
    after = game.position;
    fill_request(&req, &after, levels[i]);
    CHECK(chess_ai_task_request(&req));
    CHECK(!chess_ai_task_busy());
    CHECK(chess_ai_task_take_result(&ev));
    CHECK(ev.outcome == CHESS_AI_OK && ev.has_move);
    CHECK(move_ok(&after, ev.move));
    printf("task level %d second move %c%c%c%c\n", (int)levels[i],
           'a' + ev.move.from % 8, '1' + ev.move.from / 8,
           'a' + ev.move.to % 8, '1' + ev.move.to / 8);
  }
}

static uint64_t wall_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static uint64_t job_clock(void *ctx) {
  (void)ctx;
  return wall_ms();
}

static volatile sig_atomic_t s_timed_out;

static void on_alarm(int sig) {
  (void)sig;
  s_timed_out = 1;
  _exit(86);
}

static void run_firmware_search(const char *fen, chess_ai_level level,
                                int book) {
  chess_ai_request req;
  chess_ai_job job;
  uint64_t t0;
  uint64_t dt;
  uint32_t nodes = 0;
  uint32_t idepth = 0;
  memset(&req, 0, sizeof(req));
  memset(&job, 0, sizeof(job));
  CHECK(chess_position_from_fen(&req.position, fen) == CHESS_OK);
  req.level = level;
  chess_ai_level_budgets(level, &req.deadline_ms, &req.node_max,
                         &req.depth_max);
  req.book_enabled = book;
  req.book_seed = 0;
  req.easy_seed = 1;
  req.generation = 9;
  job.clock = job_clock;
  t0 = wall_ms();
  s_timed_out = 0;
  alarm((unsigned)(req.deadline_ms / 1000u + 8u));
  chess_ai_run_job(&job, &req);
  alarm(0);
  dt = wall_ms() - t0;
  mcumax_get_last_search_stats(&nodes, &idepth);
  CHECK(job.outcome == CHESS_AI_OK);
  CHECK(job.has_best);
  CHECK(move_ok(&req.position, job.best));
  if (!book) {
    /* Wall clock is the primary cap. A finished depth may return early;
     * a search that ignores the deadline runs until the alarm. */
    CHECK(job.callback_count > 0);
    CHECK(dt < (uint64_t)req.deadline_ms + 2000u);
  }
  printf("search level %d book %d ms %llu budget %u nodes %u depth %u "
         "move %c%c%c%c\n",
         (int)level, book, (unsigned long long)dt, req.deadline_ms, nodes,
         idepth, 'a' + job.best.from % 8, '1' + job.best.from / 8,
         'a' + job.best.to % 8, '1' + job.best.to / 8);
}

static void test_consecutive_firmware_budgets(void) {
  chess_ai_level levels[3] = {CHESS_AI_EASY, CHESS_AI_NORMAL, CHESS_AI_HARD};
  const char *positions[] = {
      START_FEN,
      "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
      "6k1/8/8/8/4q3/8/8/4R1K1 w - - 0 1",
  };
  int li;
  int pi;
  signal(SIGALRM, on_alarm);
  for (li = 0; li < 3; li++) {
    /* Two searches back to back: the device hung on the second one. */
    run_firmware_search(START_FEN, levels[li], 0);
    run_firmware_search(START_FEN, levels[li], 0);
  }
  for (pi = 0; pi < 3; pi++) {
    run_firmware_search(positions[pi], CHESS_AI_NORMAL, pi == 0);
  }
  (void)s_timed_out;
}

/* Fixed node/depth budget, deadline out of reach. Must match main:
 * the scheduling fix does not change search results. */
static void test_fixed_budget_matches_main(void) {
  chess_ai_request req;
  chess_ai_job job;
  uint32_t nodes = 0;
  uint32_t idepth = 0;
  memset(&req, 0, sizeof(req));
  memset(&job, 0, sizeof(job));
  CHECK(chess_position_from_fen(&req.position, START_FEN) == CHESS_OK);
  req.level = CHESS_AI_NORMAL;
  req.deadline_ms = 600000;
  req.node_max = 100000;
  req.depth_max = 4;
  req.book_enabled = false;
  job.clock = job_clock;
  alarm(10);
  chess_ai_run_job(&job, &req);
  alarm(0);
  mcumax_get_last_search_stats(&nodes, &idepth);
  CHECK(job.outcome == CHESS_AI_OK && job.has_best);
  /* main 52dd384, MCUMAX_HASH_BITS=12, book off, 100000 nodes / depth 4.
   * Depth completes first (29458 nodes, root iter 7 = depth 4 + 3). */
  CHECK(nodes == 29458u);
  CHECK(idepth == 7u);
  CHECK(job.best.from == 11 && job.best.to == 27); /* d2d4 */
}

/* ESP skips the table when the heap gate fails. Search must still
 * return a legal move with the table shut down. */
static void test_search_after_tt_shutdown(void) {
  chess_ai_request req;
  chess_ai_job job;
  CHECK(mcumax_hash_is_active());
  mcumax_hash_shutdown();
  CHECK(!mcumax_hash_is_active());
  memset(&req, 0, sizeof(req));
  memset(&job, 0, sizeof(job));
  CHECK(chess_position_from_fen(&req.position, START_FEN) == CHESS_OK);
  req.level = CHESS_AI_EASY;
  req.deadline_ms = 80;
  req.node_max = 200000;
  req.depth_max = 4;
  req.book_enabled = false;
  job.clock = job_clock;
  alarm(5);
  chess_ai_run_job(&job, &req);
  alarm(0);
  CHECK(job.outcome == CHESS_AI_OK);
  CHECK(job.has_best);
  CHECK(move_ok(&req.position, job.best));
  CHECK(!mcumax_hash_is_active());
  printf("search after TT shutdown move %c%c%c%c\n",
         'a' + job.best.from % 8, '1' + job.best.from / 8,
         'a' + job.best.to % 8, '1' + job.best.to / 8);
}

int main(void) {
  test_second_request_after_book();
  test_fixed_budget_matches_main();
  test_consecutive_firmware_budgets();
  test_search_after_tt_shutdown();
  if (failures == 0) {
    printf("PASS: ai task second move and firmware deadlines\n");
    return 0;
  }
  printf("FAILURES: %d\n", failures);
  return 1;
}
