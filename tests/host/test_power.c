#include <assert.h>
#include <stdio.h>

#include "chess_power.h"

static void test_idle_deadlines_and_activity_restore(void) {
  chess_power_state state;
  chess_power_init(&state, 1000);
  assert(chess_power_poll(&state, 30999, false) == CHESS_POWER_NONE);
  assert(chess_power_poll(&state, 31000, false) == CHESS_POWER_DIM);
  assert(chess_power_poll(&state, 31001, false) == CHESS_POWER_NONE);
  chess_power_activity(&state, 32000);
  assert(chess_power_poll(&state, 61999, false) == CHESS_POWER_RESTORE);
  assert(chess_power_poll(&state, 62000, false) == CHESS_POWER_DIM);
  assert(chess_power_poll(&state, 332000, false) == CHESS_POWER_SLEEP);
}

static void test_busy_work_defers_sleep_without_resetting_deadline(void) {
  chess_power_state state;
  chess_power_init(&state, 0);
  assert(chess_power_poll(&state, 30000, true) == CHESS_POWER_DIM);
  assert(chess_power_poll(&state, 300000, true) == CHESS_POWER_NONE);
  assert(chess_power_poll(&state, 300001, false) == CHESS_POWER_SLEEP);
}

static void test_clock_rollback_resets_idle_baseline(void) {
  chess_power_state state;
  chess_power_init(&state, 90000);
  assert(chess_power_poll(&state, 100, false) == CHESS_POWER_NONE);
  assert(chess_power_poll(&state, 30100, false) == CHESS_POWER_DIM);
}

int main(void) {
  test_idle_deadlines_and_activity_restore();
  test_busy_work_defers_sleep_without_resetting_deadline();
  test_clock_rollback_resets_idle_baseline();
  puts("power policy tests passed");
  return 0;
}
