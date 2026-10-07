/* AI worker task (firmware): one search at a time, generation-tagged
 * results polled via take_result. The owner cancels through the job
 * flag; the engine callback stops the search, and completion (even
 * cancelled) always produces exactly one pending result. The app loop
 * polls take_result on its queue timeout; no task notifications cross
 * the boundary.
 */
#ifndef CHESS_AI_TASK_H
#define CHESS_AI_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "chess_ai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Posted to the app input queue; app matches generation, side and
 * terminal state before applying (see chess_ai_apply_checked). */
typedef struct chess_ai_result_event {
    chess_ai_outcome outcome;
    chess_move move;     /* valid when has_move */
    bool has_move;
    chess_move fallback; /* valid when has_fallback */
    bool has_fallback;
    uint64_t generation;
    chess_ai_level level;
} chess_ai_result_event;

/* Start the worker (8 KiB stack, low priority). app_task receives
 * nothing directly; results arrive via chess_ai_take_result. Unit
 * check docs/performance.md before changing the stack size. */
bool chess_ai_task_start(void);

/* Queue one search; false when a search is already running. */
bool chess_ai_task_request(const chess_ai_request *req);

/* Ask the running search to stop; its result still arrives exactly
 * once (CANCELLED unless it already finished). */
void chess_ai_task_cancel(void);

/* True while a search is in flight. */
bool chess_ai_task_busy(void);

/* Fetch the latest result; false when none is pending. */
bool chess_ai_task_take_result(chess_ai_result_event *out);

#endif /* CHESS_AI_TASK_H */
