#include "chess_power.h"

void chess_power_init(chess_power_state *state, uint64_t now_ms) {
  if (!state) return;
  state->last_activity_ms = now_ms;
  state->dimmed = false;
  state->restore_pending = false;
}

void chess_power_activity(chess_power_state *state, uint64_t now_ms) {
  if (!state) return;
  state->last_activity_ms = now_ms;
  if (state->dimmed) {
    state->dimmed = false;
    state->restore_pending = true;
  }
}

chess_power_action chess_power_poll(chess_power_state *state, uint64_t now_ms,
                                   bool work_busy) {
  uint64_t idle_ms;
  if (!state) return CHESS_POWER_NONE;
  if (state->restore_pending) {
    state->restore_pending = false;
    return CHESS_POWER_RESTORE;
  }
  if (now_ms < state->last_activity_ms) {
    state->last_activity_ms = now_ms;
    return CHESS_POWER_NONE;
  }
  idle_ms = now_ms - state->last_activity_ms;
  if (idle_ms >= CHESS_POWER_SLEEP_AFTER_MS) {
    return work_busy ? CHESS_POWER_NONE : CHESS_POWER_SLEEP;
  }
  if (!state->dimmed && idle_ms >= CHESS_POWER_DIM_AFTER_MS) {
    state->dimmed = true;
    return CHESS_POWER_DIM;
  }
  return CHESS_POWER_NONE;
}
