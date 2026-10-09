/* Portable search job: deadline/cancel callback, Easy sampling with a
 * replayable seed, first-legal fallback, generation-guarded apply. The
 * engine itself stays synchronous; cancellation lands through its
 * periodic callback via mcumax_stop_search. */
#include "mcu-max.h"

#include "chess_ai.h"
#include "chess_core.h"
#include "opening_book.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#endif

#define CHESS_AI_TT_MIN_FREE_HEAP (32u * 1024u)

static bool s_engine_init_done;
static chess_position s_last_pos;
static bool s_have_last_pos;
static mcumax_move s_move_hist[256];
static size_t s_move_hist_len;

static void core_move_to_mcumax(const chess_move *m, mcumax_move *out) {
    unsigned file = (unsigned)(m->from % 8u);
    unsigned rank = (unsigned)(m->from / 8u);
    unsigned tfile = (unsigned)(m->to % 8u);
    unsigned trank = (unsigned)(m->to / 8u);
    out->from = (uint8_t)(((7u - rank) << 4) | file);
    out->to = (uint8_t)(((7u - trank) << 4) | tfile);
}

static bool position_reachable_by_one_move(const chess_position *from,
                                          const chess_position *to,
                                          chess_move *out) {
    chess_move moves[CHESS_MAX_MOVES];
    size_t count = 0;
    size_t i;

    if (from == NULL || to == NULL || out == NULL) {
        return false;
    }
    if (chess_generate_legal(from, moves, CHESS_MAX_MOVES, &count) != CHESS_OK) {
        return false;
    }
    for (i = 0; i < count; i++) {
        chess_position probe = *from;
        chess_undo undo;
        if (chess_make(&probe, moves[i], &undo) != CHESS_OK) {
            continue;
        }
        if (memcmp(&probe.board, &to->board, sizeof(probe.board)) == 0 &&
            probe.side_to_move == to->side_to_move &&
            probe.castling == to->castling && probe.ep_square == to->ep_square) {
            *out = moves[i];
            return true;
        }
    }
    return false;
}

static void ensure_engine_init(void) {
    if (!s_engine_init_done) {
        chess_ai_engine_init();
    }
}

void chess_ai_engine_init(void) {
    if (s_engine_init_done) {
        return;
    }
    s_engine_init_done = true;
#if !defined(MCUMAX_HASH_BITS) || MCUMAX_HASH_BITS > 0
    size_t need = mcumax_hash_table_bytes();
    if (need == 0) {
        return;
    }
#ifdef ESP_PLATFORM
    size_t free_before =
        (size_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (free_before < need) {
        ESP_LOGW("chess_ai", "TT skip: need %u bytes, free %u",
                 (unsigned)need, (unsigned)free_before);
        return;
    }
#endif
    if (!mcumax_hash_alloc()) {
#ifdef ESP_PLATFORM
        ESP_LOGW("chess_ai", "TT alloc failed (%u bytes)", (unsigned)need);
#else
        (void)need;
#endif
        return;
    }
#ifdef ESP_PLATFORM
    {
        size_t free_after =
            (size_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (free_after < CHESS_AI_TT_MIN_FREE_HEAP) {
            ESP_LOGW("chess_ai",
                     "TT disabled: %u bytes free after %u-byte table (min %u)",
                     (unsigned)free_after, (unsigned)need,
                     (unsigned)CHESS_AI_TT_MIN_FREE_HEAP);
            mcumax_hash_shutdown();
        }
    }
#endif
#endif
}

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
        *deadline_ms = 5000u;
        *node_max = 20000000u;
        *depth_max = 12u;
        break;
    case CHESS_AI_NORMAL:
    default:
        *deadline_ms = 1500u;
        *node_max = 5000000u;
        *depth_max = 8u;
        break;
    }
}

int chess_ai_suggest(const chess_position *pos, uint32_t node_max,
                     unsigned depth_max, chess_move *out) {
    ensure_engine_init();
    char fen[CHESS_FEN_MAX];
    mcumax_move reply;
    chess_move m;
    chess_position probe;
    chess_undo undo;
    unsigned last;

    if (pos == NULL || out == NULL) {
        return -1;
    }
    if (chess_opening_book_probe(pos, 0u, true, out)) {
        s_last_pos = *pos;
        s_have_last_pos = true;
        return 0;
    }
    if (chess_position_to_fen(pos, fen, sizeof(fen)) != CHESS_OK) {
        return -1;
    }
    chess_move delta;
    if (s_have_last_pos &&
        position_reachable_by_one_move(&s_last_pos, pos, &delta)) {
        mcumax_move mm;
        core_move_to_mcumax(&delta, &mm);
        if (!mcumax_play_move(mm)) {
            s_move_hist_len = 0;
#if MCUMAX_HASH_BITS > 0
            mcumax_hash_set_replay_hint(NULL, 0);
#endif
            mcumax_set_fen_position(fen);
        } else if (s_move_hist_len < sizeof(s_move_hist) / sizeof(s_move_hist[0])) {
            s_move_hist[s_move_hist_len++] = mm;
        }
    } else {
        s_move_hist_len = 0;
#if MCUMAX_HASH_BITS > 0
        mcumax_hash_set_replay_hint(NULL, 0);
#endif
        mcumax_set_fen_position(fen);
    }
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
    s_last_pos = *pos;
    s_have_last_pos = true;
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
    ensure_engine_init();
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
    s_callback_ctx.job = NULL;

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
    if (chess_opening_book_probe(&req->position, req->book_seed,
                                 req->book_enabled, &mapped)) {
        job->best = mapped;
        job->has_best = true;
        job->outcome = CHESS_AI_OK;
        s_callback_ctx.job = NULL;
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
    mcumax_set_fen_position(fen);
    mcumax_set_callback(engine_callback, &s_callback_ctx);
    reply = mcumax_search_best_move(req->node_max, req->depth_max);
    mcumax_set_callback(NULL, NULL);
    s_callback_ctx.job = NULL;

    if (job->cancel) {
        job->has_fallback = false;
        job->outcome = CHESS_AI_CANCELLED;
        s_callback_ctx.job = NULL;
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
            s_callback_ctx.job = NULL;
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
        s_callback_ctx.job = NULL;
        return;
    }
    /* A deadline with a validated completed-iteration result is normal.
     * Only a deadline before any usable result requires fallback. */
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
