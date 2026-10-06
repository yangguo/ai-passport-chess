/* Portable search job: deadline/cancel callback, Easy sampling with a
 * replayable seed, first-legal fallback, generation-guarded apply. The
 * engine itself stays synchronous; cancellation lands through its
 * periodic callback via mcumax_stop_search. */
#include "mcu-max.h"

#include "chess_ai.h"
#include "chess_core.h"

void chess_ai_level_budgets(chess_ai_level level, uint32_t *deadline_ms,
                            uint32_t *node_max, unsigned *depth_max) {
    if (deadline_ms == NULL || node_max == NULL || depth_max == NULL) {
        return;
    }
    switch (level) {
    case CHESS_AI_EASY:
        *deadline_ms = 100u;
        *node_max = 500000u;
        *depth_max = 6u;
        break;
    case CHESS_AI_HARD:
        *deadline_ms = 1000u;
        *node_max = 4000000u;
        *depth_max = 12u;
        break;
    case CHESS_AI_NORMAL:
    default:
        *deadline_ms = 300u;
        *node_max = 1000000u;
        *depth_max = 8u;
        break;
    }
}

int chess_ai_suggest(const chess_position *pos, uint32_t node_max,
                     unsigned depth_max, chess_move *out) {
    char fen[CHESS_FEN_MAX];
    mcumax_move reply;
    chess_move m;
    chess_position probe;
    chess_undo undo;
    unsigned last;

    if (pos == NULL || out == NULL) {
        return -1;
    }
    if (chess_position_to_fen(pos, fen, sizeof(fen)) != CHESS_OK) {
        return -1;
    }
    /* mcumax_set_fen_position resets the global engine state first,
     * so consecutive calls never leak history. No callback installed:
     * the search always ends on the node/depth budget. */
    mcumax_set_fen_position(fen);
    reply = mcumax_search_best_move(node_max, depth_max);
    if (reply.from == MCUMAX_SQUARE_INVALID ||
        reply.to == MCUMAX_SQUARE_INVALID) {
        return -1;
    }
    {
        unsigned file = (unsigned)(reply.from & 15u);
        unsigned rank = (unsigned)((reply.from >> 4) & 15u);
        unsigned tfile = (unsigned)(reply.to & 15u);
        unsigned trank = (unsigned)((reply.to >> 4) & 15u);
        if (file > 7u || rank > 7u || tfile > 7u || trank > 7u) {
            return -1;
        }
        m.from = (uint8_t)((7u - rank) * 8u + file);
        m.to = (uint8_t)((7u - trank) * 8u + tfile);
    }
    /* The engine only ever queens; anything else promoting is mapped
     * the same way and then validated like every other reply. */
    m.promotion = CHESS_EMPTY;
    last = (pos->side_to_move == CHESS_WHITE) ? 7u : 0u;
    if (pos->board[m.from].type == CHESS_PAWN && m.to / 8u == last) {
        m.promotion = CHESS_QUEEN;
    }
    probe = *pos;
    if (chess_make(&probe, m, &undo) != CHESS_OK) {
        return -1;
    }
    *out = m;
    return 0;
}

/* Deterministic 32-bit mixer for replayable Easy sampling. */
static uint32_t easy_rng_next(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

typedef struct job_callback_ctx {
    chess_ai_job *job;
} job_callback_ctx;

static job_callback_ctx s_callback_ctx;

static void engine_callback(void *userdata) {
    job_callback_ctx *ctx = (job_callback_ctx *)userdata;
    chess_ai_job *job = ctx->job;
    uint64_t now;
    job->callback_count++;
    if (job->cancel) {
        mcumax_stop_search();
        return;
    }
    if (job->clock == NULL) {
        return;
    }
    now = job->clock(job->clock_ctx);
    if (now >= job->deadline_at_ms) {
        mcumax_stop_search();
        return;
    }
    /* Yield at most every few ms of search time, never per node. */
    if (job->yield != NULL && now - job->last_yield_ms >= 3u) {
        job->last_yield_ms = (uint32_t)now;
        job->yield(job->yield_ctx);
    }
}

static bool collect_legal(const chess_position *pos, chess_move *out,
                          size_t cap, size_t *count) {
    return chess_generate_legal(pos, out, cap, count) == CHESS_OK;
}

void chess_ai_run_job(chess_ai_job *job, const chess_ai_request *req) {
    chess_move legal[CHESS_MAX_MOVES];
    size_t count = 0;
    char fen[CHESS_FEN_MAX];
    mcumax_move reply;
    chess_move mapped;
    chess_position probe;
    chess_undo undo;
    unsigned last;
    bool timed_out = false;

    if (job == NULL || req == NULL) {
        return;
    }
    job->has_best = false;
    job->has_fallback = false;
    job->outcome = CHESS_AI_ENGINE_ERROR;
    job->result_generation = req->generation;
    job->callback_count = 0;

    if (!collect_legal(&req->position, legal, CHESS_MAX_MOVES, &count) ||
        count == 0) {
        job->outcome = CHESS_AI_NO_MOVE;
        return;
    }
    /* Fallback first: valid whenever the position is, even if the
     * search later fails — except on user cancel. */
    job->fallback = legal[0];
    job->has_fallback = true;

    if (job->cancel) {
        job->has_fallback = false;
        job->outcome = CHESS_AI_CANCELLED;
        return;
    }
    if (chess_position_to_fen(&req->position, fen, sizeof(fen)) != CHESS_OK) {
        job->outcome = CHESS_AI_ENGINE_ERROR;
        return;
    }
    job->started_at_ms =
        job->clock != NULL ? (uint32_t)job->clock(job->clock_ctx) : 0u;
    job->deadline_at_ms = job->started_at_ms + req->deadline_ms;
    job->last_yield_ms = job->started_at_ms;
    s_callback_ctx.job = job;
    mcumax_set_callback(engine_callback, &s_callback_ctx);
    mcumax_set_fen_position(fen);
    reply = mcumax_search_best_move(req->node_max, req->depth_max);
    mcumax_set_callback(NULL, NULL);

    if (job->cancel) {
        job->has_fallback = false;
        job->outcome = CHESS_AI_CANCELLED;
        return;
    }
    if (job->clock != NULL && job->clock(job->clock_ctx) >= job->deadline_at_ms) {
        timed_out = true;
    }
    {
        unsigned file = (unsigned)(reply.from & 15u);
        unsigned rank = (unsigned)((reply.from >> 4) & 15u);
        unsigned tfile = (unsigned)(reply.to & 15u);
        unsigned trank = (unsigned)((reply.to >> 4) & 15u);
        bool squares_ok = file <= 7u && rank <= 7u && tfile <= 7u &&
                          trank <= 7u &&
                          reply.from != MCUMAX_SQUARE_INVALID &&
                          reply.to != MCUMAX_SQUARE_INVALID;
        if (!squares_ok) {
            job->outcome =
                timed_out ? CHESS_AI_TIMEOUT : CHESS_AI_ENGINE_ERROR;
            return;
        }
        mapped.from = (uint8_t)((7u - rank) * 8u + file);
        mapped.to = (uint8_t)((7u - trank) * 8u + tfile);
    }
    mapped.promotion = CHESS_EMPTY;
    last = (req->position.side_to_move == CHESS_WHITE) ? 7u : 0u;
    if (req->position.board[mapped.from].type == CHESS_PAWN &&
        mapped.to / 8u == last) {
        mapped.promotion = CHESS_QUEEN;
    }
    probe = req->position;
    if (chess_make(&probe, mapped, &undo) != CHESS_OK) {
        job->outcome = timed_out ? CHESS_AI_TIMEOUT : CHESS_AI_ENGINE_ERROR;
        return;
    }
    if (timed_out) {
        job->outcome = CHESS_AI_TIMEOUT;
        return;
    }
    if (req->level == CHESS_AI_EASY && count > 1) {
        uint32_t rng = req->easy_seed;
        uint32_t roll = easy_rng_next(&rng) % 100u;
        if (roll < CHESS_AI_EASY_RANDOM_PCT) {
            size_t pick = 1u + easy_rng_next(&rng) % (count - 1u);
            size_t i;
            /* Find a non-best legal move deterministically. */
            for (i = 1; i < count; i++) {
                size_t at = (pick + i) % count;
                if (legal[at].from != mapped.from ||
                    legal[at].to != mapped.to ||
                    legal[at].promotion != mapped.promotion) {
                    mapped = legal[at];
                    break;
                }
            }
        }
    }
    job->best = mapped;
    job->has_best = true;
    job->outcome = CHESS_AI_OK;
}

chess_error chess_ai_apply_checked(chess_game *game, chess_move m,
                                   uint64_t req_generation,
                                   uint64_t cur_generation) {
    if (game == NULL) {
        return CHESS_ERR_NULL;
    }
    if (chess_game_status(game) != CHESS_STATUS_ONGOING) {
        return CHESS_ERR_GAME_OVER;
    }
    if (req_generation != cur_generation) {
        return CHESS_ERR_STALE;
    }
    return chess_game_apply(game, m);
}
