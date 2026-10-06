/* Worker plumbing around the portable chess_ai_run_job. Clock is the
 * monotonic esp_timer; the yield hook gives the search a 1-tick pause
 * when due so input stays responsive. Stack high-water is logged once
 * per search for the performance gate (units verified on device). */
#include "chess_ai_task.h"

#include <string.h>

#include "chess_ai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* 8 KiB — see docs/performance.md; verify task stack units + high
 * water on device before treating this as final. */
#define AI_TASK_STACK 8192
#define AI_TASK_PRIORITY 3

static const char *TAG = "chess-ai";

static QueueHandle_t s_requests;
static TaskHandle_t s_worker;
static chess_ai_job s_job;
static chess_ai_result_event s_result;
static bool s_result_pending;
static bool s_busy;

static uint64_t clock_ms(void *ctx) {
    (void)ctx;
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void yield_tick(void *ctx) {
    (void)ctx;
    vTaskDelay(1);
}

static void worker_main(void *arg) {
    (void)arg;
    for (;;) {
        chess_ai_request req;
        if (xQueueReceive(s_requests, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        memset(&s_job, 0, sizeof(s_job));
        s_job.clock = clock_ms;
        s_job.yield = yield_tick;
        chess_ai_run_job(&s_job, &req);
        s_result.outcome = s_job.outcome;
        s_result.move = s_job.best;
        s_result.has_move = s_job.has_best;
        s_result.fallback = s_job.fallback;
        s_result.has_fallback = s_job.has_fallback;
        s_result.generation = s_job.result_generation;
        s_result.level = req.level;
        s_result_pending = true;
        s_busy = false;
        ESP_LOGI(TAG, "search done outcome=%d callbacks=%lu stack_free=%u",
                 (int)s_job.outcome, (unsigned long)s_job.callback_count,
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

bool chess_ai_task_start(void) {
    if (s_worker != NULL) {
        return true;
    }
    s_requests = xQueueCreate(1, sizeof(chess_ai_request));
    if (s_requests == NULL) {
        return false;
    }
    s_busy = false;
    s_result_pending = false;
    if (xTaskCreate(worker_main, "chess_ai", AI_TASK_STACK, NULL,
                    AI_TASK_PRIORITY, &s_worker) != pdPASS) {
        vQueueDelete(s_requests);
        s_requests = NULL;
        return false;
    }
    return true;
}

bool chess_ai_task_request(const chess_ai_request *req) {
    if (s_worker == NULL || req == NULL || s_busy) {
        return false;
    }
    if (xQueueSend(s_requests, req, 0) != pdTRUE) {
        return false;
    }
    s_busy = true;
    return true;
}

void chess_ai_task_cancel(void) {
    s_job.cancel = true;
}

bool chess_ai_task_busy(void) {
    return s_busy;
}

bool chess_ai_task_take_result(chess_ai_result_event *out) {
    if (out == NULL || !s_result_pending) {
        return false;
    }
    *out = s_result;
    s_result_pending = false;
    return true;
}
