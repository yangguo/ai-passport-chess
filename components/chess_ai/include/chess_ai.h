/* In-process mcu-max adapter + portable search job (host + firmware).
 * FEN in, bounded engine search, UCI out, core-validated. No callbacks
 * installed by suggest(); run_job installs a deadline/cancel callback
 * and an optional yield hook. Promotion replies are always queens —
 * the engine's documented limit; the core still accepts all four.
 *
 * Difficulty budgets (docs/ai-integration.md, device-calibrated later):
 * time is the primary limit, nodes/depth are second guardrails.
 */
#ifndef CHESS_AI_H
#define CHESS_AI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chess_core.h"

typedef enum chess_ai_level {
    CHESS_AI_EASY = 0,
    CHESS_AI_NORMAL = 1,
    CHESS_AI_HARD = 2
} chess_ai_level;

typedef enum chess_ai_outcome {
    CHESS_AI_OK = 0,
    CHESS_AI_TIMEOUT,
    CHESS_AI_CANCELLED,
    CHESS_AI_NO_MOVE,
    CHESS_AI_ENGINE_ERROR
} chess_ai_outcome;

/* Monotonic millisecond clock (firmware: esp_timer; host tests: fake). */
typedef uint64_t (*chess_ai_clock_ms)(void *ctx);
/* Cooperative yield inside the search callback (firmware: 1-tick delay
 * when due; host: counting stub or NULL). */
typedef void (*chess_ai_yield)(void *ctx);

typedef struct chess_ai_request {
    chess_position position;
    uint64_t generation;
    chess_ai_level level;
    uint32_t deadline_ms; /* relative budget */
    uint32_t node_max;
    unsigned depth_max;
    uint32_t easy_seed; /* replayable Easy sampling */
    uint32_t book_seed; /* 0 = main weighted line when book_enabled */
    bool book_enabled; /* false after save restore; CLI uses internal default */
} chess_ai_request;

typedef struct chess_ai_job {
    /* In: set by owner before run_job. */
    volatile bool cancel;
    chess_ai_clock_ms clock;
    void *clock_ctx;
    chess_ai_yield yield;
    void *yield_ctx;
    /* Out: valid after run_job returns. */
    chess_move best;
    bool has_best;
    chess_move fallback; /* first legal move, when one exists */
    bool has_fallback;
    chess_ai_outcome outcome;
    uint64_t result_generation;
    /* Internal callback bookkeeping (do not touch). */
    uint32_t started_at_ms;
    uint32_t deadline_at_ms;
    uint32_t callback_count;
    uint32_t last_yield_ms;
} chess_ai_job;

#define CHESS_AI_EASY_RANDOM_PCT 15u

/* One-shot suggest for the side to move (used by the host CLI).
 * Returns 0 with *out set, nonzero on any failure. */
int chess_ai_suggest(const chess_position *pos, uint32_t node_max,
                     unsigned depth_max, chess_move *out);

/* Fill level budgets (deadline/nodes/depth) into an owner-made request. */
void chess_ai_level_budgets(chess_ai_level level, uint32_t *deadline_ms,
                            uint32_t *node_max, unsigned *depth_max);

/* Run a bounded, cancellable search synchronously (caller = worker
 * task). Installs the engine callback, maps and core-validates the
 * reply. A deadline returns OK when a completed, legal search result
 * exists. Timeout without such a result, cancel, or invalid reply has no
 * best move; a fallback is still provided unless cancelled. */
void chess_ai_run_job(chess_ai_job *job, const chess_ai_request *req);

/* Guarded apply for worker results: terminal games and generation
 * drift are rejected before legality is even consulted. Returns
 * CHESS_OK, CHESS_ERR_GAME_OVER, CHESS_ERR_STALE or
 * CHESS_ERR_ILLEGAL_MOVE; the game is untouched on failure. */
chess_error chess_ai_apply_checked(chess_game *game, chess_move m,
                                   uint64_t req_generation,
                                   uint64_t cur_generation);

#endif /* CHESS_AI_H */
