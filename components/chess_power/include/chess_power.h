#ifndef CHESS_POWER_H
#define CHESS_POWER_H

#include <stdbool.h>
#include <stdint.h>

#define CHESS_POWER_DIM_AFTER_MS 30000u
#define CHESS_POWER_SLEEP_AFTER_MS 300000u

typedef enum chess_power_action {
  CHESS_POWER_NONE = 0,
  CHESS_POWER_DIM,
  CHESS_POWER_RESTORE,
  CHESS_POWER_SLEEP
} chess_power_action;

typedef struct chess_power_state {
  uint64_t last_activity_ms;
  bool dimmed;
  bool restore_pending;
} chess_power_state;

void chess_power_init(chess_power_state *state, uint64_t now_ms);
void chess_power_activity(chess_power_state *state, uint64_t now_ms);
chess_power_action chess_power_poll(chess_power_state *state, uint64_t now_ms,
                                   bool work_busy);

#endif
