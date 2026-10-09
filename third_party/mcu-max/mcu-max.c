/*
 * mcu-max
 * Chess game engine for low-resource MCUs
 *
 * (C) 2022-2024 Gissio
 *
 * License: MIT
 *
 * Based on micro-Max 4.8 by H.G. Muller.
 * Compliant with FIDE laws (except for underpromotion).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mcu-max.h"

/* Transposition table size: 2^MCUMAX_HASH_BITS entries (0 = disabled). */
#ifndef MCUMAX_HASH_BITS
#define MCUMAX_HASH_BITS 0
#endif

#if MCUMAX_HASH_BITS > 0
#define MCUMAX_HASHING_ENABLED 1
#define MCUMAX_HASH_TABLE_SIZE (1u << (MCUMAX_HASH_BITS))
#else
#define MCUMAX_HASH_TABLE_SIZE 1u
#endif

// Constants
#define MCUMAX_BOARD_MASK 0x88
#define MCUMAX_BOARD_WHITE 0x8
#define MCUMAX_BOARD_BLACK 0x10
#define MCUMAX_PIECE_MOVED 0x20
#define MCUMAX_SCORE_MAX 8000
#define MCUMAX_DEPTH_MAX 99

enum mcumax_mode
{
    MCUMAX_INTERNAL_NODE,
    MCUMAX_SEARCH_VALID_MOVES,
    MCUMAX_SEARCH_BEST_MOVE,
    MCUMAX_PLAY_MOVE,
};

struct
{
    // Board: first half of 16x8 + dummy
    uint8_t board[0x80 + 1];
    uint8_t current_side;

    // Engine
    int32_t score;
    uint8_t en_passant_square;
    int32_t non_pawn_material;

#ifdef MCUMAX_HASHING_ENABLED
    uint32_t hash_key;
    uint32_t hash_key2;
#endif

    // Interface
    uint8_t square_from; // Selected move
    uint8_t square_to;

    uint32_t node_count;
    uint32_t node_max;
    uint32_t depth_max;

    bool stop_search;

    // Local patch: only a fully completed root iteration may survive stop.
    mcumax_move completed_move;

    // Extra
    mcumax_callback user_callback;
    void *user_data;

    mcumax_move *valid_moves_buffer;
    uint32_t valid_moves_buffer_size;
    uint32_t valid_moves_num;
} mcumax;

static const int8_t mcumax_capture_values[] = {
    0, 2, 2, 7, -1, 8, 12, 23};

static const int8_t mcumax_step_vectors_indices[] = {
    0, 7, -1, 11, 6, 8, 3, 6};

static const int8_t mcumax_step_vectors[] = {
    // Upstream pawn
    -16, -15, -17, 0,
    // Rook
    1, 16, 0,
    // King, queen
    1, 16, 15, 17, 0,
    // Knight
    14, 18, 31, 33, 0};

static const int8_t mcumax_board_setup[] = {
    MCUMAX_ROOK,
    MCUMAX_KNIGHT,
    MCUMAX_BISHOP,
    MCUMAX_QUEEN,
    MCUMAX_KING,
    MCUMAX_BISHOP,
    MCUMAX_KNIGHT,
    MCUMAX_ROOK,
};

static uint8_t mcumax_start_board[sizeof(mcumax.board)];
static bool mcumax_start_board_ready;

#define MCUMAX_CASTLE_WK 0x01u
#define MCUMAX_CASTLE_WQ 0x02u
#define MCUMAX_CASTLE_BK 0x04u
#define MCUMAX_CASTLE_BQ 0x08u

static void mcumax_apply_moved_flags_from_layout(void)
{
    for (uint8_t square = 0; square < 0x80; square++)
    {
        if (square & MCUMAX_BOARD_MASK)
            continue;

        uint8_t piece = mcumax.board[square];
        uint8_t start = mcumax_start_board[square];

        if (!piece)
            continue;

        if (piece != start)
            mcumax.board[square] = piece | MCUMAX_PIECE_MOVED;
        else
            mcumax.board[square] = piece & (uint8_t)~MCUMAX_PIECE_MOVED;
    }
}

static void mcumax_mark_square_moved(uint8_t square)
{
    if (mcumax.board[square])
        mcumax.board[square] |= MCUMAX_PIECE_MOVED;
}

static void mcumax_clear_square_moved(uint8_t square)
{
    if (mcumax.board[square])
        mcumax.board[square] &= (uint8_t)~MCUMAX_PIECE_MOVED;
}

static bool mcumax_square_is_king(uint8_t square, uint8_t color)
{
    uint8_t piece = mcumax.board[square] & (uint8_t)~MCUMAX_PIECE_MOVED;

    return piece == (MCUMAX_KING | color);
}

static bool mcumax_square_is_rook(uint8_t square, uint8_t color)
{
    uint8_t piece = mcumax.board[square] & (uint8_t)~MCUMAX_PIECE_MOVED;

    return piece == (MCUMAX_ROOK | color);
}

/* FEN castling field: home K/R are virgin only when that side's right is set. */
static void mcumax_apply_castling_rights(uint8_t rights)
{
    if (mcumax_square_is_king(0x74, MCUMAX_BOARD_WHITE))
    {
        if ((rights & (MCUMAX_CASTLE_WK | MCUMAX_CASTLE_WQ)) != 0)
            mcumax_clear_square_moved(0x74);
        else
            mcumax_mark_square_moved(0x74);
    }

    if (mcumax_square_is_rook(0x77, MCUMAX_BOARD_WHITE))
    {
        if (rights & MCUMAX_CASTLE_WK)
            mcumax_clear_square_moved(0x77);
        else
            mcumax_mark_square_moved(0x77);
    }

    if (mcumax_square_is_rook(0x70, MCUMAX_BOARD_WHITE))
    {
        if (rights & MCUMAX_CASTLE_WQ)
            mcumax_clear_square_moved(0x70);
        else
            mcumax_mark_square_moved(0x70);
    }

    if (mcumax_square_is_king(0x04, MCUMAX_BOARD_BLACK))
    {
        if ((rights & (MCUMAX_CASTLE_BK | MCUMAX_CASTLE_BQ)) != 0)
            mcumax_clear_square_moved(0x04);
        else
            mcumax_mark_square_moved(0x04);
    }

    if (mcumax_square_is_rook(0x07, MCUMAX_BOARD_BLACK))
    {
        if (rights & MCUMAX_CASTLE_BK)
            mcumax_clear_square_moved(0x07);
        else
            mcumax_mark_square_moved(0x07);
    }

    if (mcumax_square_is_rook(0x00, MCUMAX_BOARD_BLACK))
    {
        if (rights & MCUMAX_CASTLE_BQ)
            mcumax_clear_square_moved(0x00);
        else
            mcumax_mark_square_moved(0x00);
    }
}

#if MCUMAX_HASH_BITS > 0

#define MCUMAX_HASH_SCRAMBLE_TABLE_SIZE 1035

extern const uint8_t mcumax_hash_scramble_table[MCUMAX_HASH_SCRAMBLE_TABLE_SIZE];

#define HashScramble(A, B)                      \
    *(uint32_t *)(mcumax_hash_scramble_table + \
                  A + (B & 8) + MCUMAX_SQUARE_INVALID * (B & 0b111))
#define Hash(A)                                                  \
    HashScramble(square_to + A, mcumax.board[square_to]) -       \
        HashScramble(square_from + A, scan_piece) -             \
        HashScramble(capture_square + A, capture_piece)

struct HashEntry
{
    uint32_t key2;
    int32_t score;
    uint8_t square_from;
    uint8_t square_to;
    uint8_t depth;
};

_Static_assert(sizeof(struct HashEntry) == 12, "HashEntry layout must match micro-Max");

static struct HashEntry *mcumax_hash_table;
static bool mcumax_hash_table_owned;

static struct mcumax_hash_stats mcumax_hash_stats;

static void mcumax_hash_sync_after_fen(void)
{
    bool at_start = true;

    for (uint8_t square = 0; square < 0x80; square++)
    {
        if (square & MCUMAX_BOARD_MASK)
            continue;
        if (mcumax.board[square] != mcumax_start_board[square])
        {
            at_start = false;
            break;
        }
    }

    if (at_start && mcumax.current_side == MCUMAX_BOARD_WHITE &&
        mcumax.en_passant_square == MCUMAX_SQUARE_INVALID)
    {
        mcumax.hash_key = 0;
        mcumax.hash_key2 = 0;
        return;
    }

    /* FEN reload clears the table in mcumax_init(); keys restart at zero. */
    mcumax.hash_key = 0;
    mcumax.hash_key2 = 0;
}

#endif

typedef bool (*mcumax_move_callback)(mcumax_move move);

static int32_t mcumax_search(int32_t alpha,
                             int32_t beta,
                             int32_t score,
                             uint8_t en_passant_square,
                             uint8_t depth,
                             enum mcumax_mode mode);

// Recursive minimax search
// (alpha,beta)=window, score=current evaluation score, en_passant_square=e.p. sqr.
// depth=depth, in_root=in_root; returns score
static int32_t mcumax_search(int32_t alpha,
                             int32_t beta,
                             int32_t score,
                             uint8_t en_passant_square,
                             uint8_t depth,
                             enum mcumax_mode mode)
{
    if (mcumax.user_callback)
        mcumax.user_callback(mcumax.user_data);

    uint8_t iter_depth;
    int32_t iter_score;
    uint8_t iter_square_from;
    uint8_t iter_square_to;

#ifdef MCUMAX_HASHING_ENABLED
    int32_t hash_key;
    int32_t hash_key2;
#endif

    uint8_t square_start;

    uint8_t square_from;
    uint8_t square_to;

    uint8_t replay_move;
    int32_t null_move_score;

    uint8_t scan_piece;
    uint8_t scan_piece_type;

    int8_t step_vector;
    int8_t step_vector_index;

    uint8_t castling_skip_square;
    uint8_t castling_rook_square;

    uint8_t capture_square;
    uint8_t capture_piece;
    int32_t capture_piece_value;

    uint8_t step_depth;
    int32_t step_alpha;
    int32_t step_score;
    int32_t step_score_new;

    // Adj. window: delay bonus
    alpha -= alpha < score;
    beta -= beta <= score;

#ifdef MCUMAX_HASHING_ENABLED
    struct HashEntry *hash_entry = NULL;

    if (mcumax_hash_table)
    {
        bool tt_cutoff = false;

        // Lookup pos. in hash table
        hash_entry = mcumax_hash_table +
                     ((mcumax.hash_key +
                       mcumax.current_side * en_passant_square) &
                      (MCUMAX_HASH_TABLE_SIZE - 1));

        mcumax_hash_stats.probes++;

        iter_depth = hash_entry->depth;
        iter_score = hash_entry->score;
        iter_square_from = hash_entry->square_from;
        iter_square_to = hash_entry->square_to;

        if (hash_entry->key2 == mcumax.hash_key2)
            mcumax_hash_stats.key_hits++;

        // Resume at stored depth
        if ((hash_entry->key2 != mcumax.hash_key2) ||
            (mode != MCUMAX_INTERNAL_NODE) || // Miss: other pos. or empty
            !(((iter_score <= alpha) ||
               (iter_square_from & 0x8)) &&
              ((iter_score >= beta) ||
               (iter_square_from & MCUMAX_SQUARE_INVALID)))) // Or window incompatible
        {
            // Start iteration from scratch
            iter_depth =
                iter_square_to = 0;
        }
        else
        {
            tt_cutoff = true;
            mcumax_hash_stats.cutoffs++;
        }

        // Start at best-move hint
        iter_square_from &= ~MCUMAX_BOARD_MASK;

        (void)tt_cutoff;
    }
    else
    {
        iter_depth =
            iter_score =
                iter_square_from =
                    iter_square_to = 0;
    }

    hash_key = mcumax.hash_key;
    hash_key2 = mcumax.hash_key2;
#else
    iter_depth =
        iter_score =
            iter_square_from =
                iter_square_to = 0;
#endif

    // Min depth = 2 iterative deepening loop
    // root: deepen upto time
    // time's up: go do best
    while ((iter_depth++ < depth) ||
           (iter_depth < 3) ||
           ((mode != MCUMAX_INTERNAL_NODE) &&
            (mcumax.square_from == MCUMAX_SQUARE_INVALID) &&
            (((mcumax.node_count < mcumax.node_max) &&
              (iter_depth <= mcumax.depth_max)) ||
             (mcumax.square_from = iter_square_from,
              mcumax.square_to = iter_square_to & ~MCUMAX_BOARD_MASK,
              iter_depth = 3))))
    {
        if (mcumax.stop_search)
            break;

        // Start scan at previous best
        square_from =
            square_start = (mode != MCUMAX_SEARCH_VALID_MOVES)
                               ? iter_square_from
                               : 0;

        // Request try noncastling first
        replay_move = iter_square_to & MCUMAX_SQUARE_INVALID;

        // Change side
        mcumax.current_side ^= 0x18;

        // Search null move
        null_move_score = (iter_depth > 2) && (beta != -MCUMAX_SCORE_MAX)
                              ? mcumax_search(-beta,
                                              1 - beta,
                                              -score,
                                              MCUMAX_SQUARE_INVALID,
                                              iter_depth - 3,
                                              MCUMAX_INTERNAL_NODE)
                              : MCUMAX_SCORE_MAX;

        // Change side
        mcumax.current_side ^= 0x18;

        // Prune if > beta unconsidered:static eval
        iter_score = (-null_move_score < beta) ||
                             (mcumax.non_pawn_material > 35)
                         ? (iter_depth - 2)
                               ? -MCUMAX_SCORE_MAX
                               : score
                         : -null_move_score;

        // Node count (for timing)
        mcumax.node_count++;

        do
        {
            // Scan board looking for
            scan_piece = mcumax.board[square_from];

            // Own piece (inefficient!)
            if (scan_piece & mcumax.current_side)
            {
                // p = piece type (set r>0)
                step_vector = scan_piece_type = (scan_piece & 0b111);

                // First step vector for piece
                step_vector_index = mcumax_step_vectors_indices[scan_piece_type];

                // Loop over directions o[]
                while ((step_vector = ((scan_piece_type > 2) &&
                                       (step_vector < 0))
                                          ? -step_vector
                                          : -mcumax_step_vectors[++step_vector_index]))
                {
                replay:
                    // Resume normal after best
                    square_to = square_from;

                    castling_skip_square =
                        castling_rook_square = MCUMAX_SQUARE_INVALID;

                    // y traverses ray, or:
                    do
                    {
                        // Sneak in previous best move
                        capture_square =
                            square_to =
                                replay_move
                                    ? (iter_square_to ^ replay_move)
                                    : (square_to + step_vector);

                        // Board edge hit
                        if (square_to & MCUMAX_BOARD_MASK)
                            break;

                        // Bad castling
                        if ((en_passant_square != MCUMAX_SQUARE_INVALID) &&
                            mcumax.board[en_passant_square] &&
                            ((square_to - en_passant_square) < 2) &&
                            ((en_passant_square - square_to) < 2))
                            iter_score = MCUMAX_SCORE_MAX;

                        // Shift capture square if en-passant
                        if ((scan_piece_type < 3) &&
                            (square_to == en_passant_square))
                            capture_square ^= 16;

                        capture_piece = mcumax.board[capture_square];

                        // Capture own, bad pawn mode
                        if ((capture_piece & mcumax.current_side) ||
                            ((scan_piece_type < 3) &&
                             !((square_to - square_from) & 0b111) - !capture_piece))
                            break;

                        // Value of captured piece
                        capture_piece_value = 37 * mcumax_capture_values[capture_piece & 0b111] +
                                              (capture_piece & 0xc0);

                        // King capture
                        if (capture_piece_value < 0)
                        {
                            iter_score = MCUMAX_SCORE_MAX;
                            iter_depth = MCUMAX_DEPTH_MAX - 1;
                        }

                        // Abort on fail high
                        if ((iter_score >= beta) &&
                            (iter_depth > 1))
                            goto cutoff;

                        // MVV/LVA scoring if depth == 1
                        step_score = (iter_depth != 1)
                                         ? score
                                         : capture_piece_value - scan_piece_type;

                        // All captures if depth == 2
                        if ((iter_depth - !capture_piece) > 1)
                        {
                            // Center positional score
                            step_score = (scan_piece_type < 6)
                                             ? mcumax.board[square_from + 0x8] -
                                                   mcumax.board[square_to + 0x8]
                                             : 0;

                            mcumax.board[castling_rook_square] =
                                mcumax.board[capture_square] =
                                    mcumax.board[square_from] = 0;

                            // Do move, set non-virgin
                            mcumax.board[square_to] = scan_piece | MCUMAX_PIECE_MOVED;

                            // Castling: put rook & score
                            if (!(castling_rook_square & MCUMAX_BOARD_MASK))
                            {
                                mcumax.board[castling_skip_square] = mcumax.current_side + 6;
                                step_score += 50;
                            }

                            // Freeze king in mid-game
                            step_score -= ((scan_piece_type != 4) ||
                                           (mcumax.non_pawn_material > 30))
                                              ? 0
                                              : 20;

                            // Pawns
                            if (scan_piece_type < 3)
                            {
                                step_score -=
                                    9 * ((((square_from - 2) & MCUMAX_BOARD_MASK) ||
                                          mcumax.board[square_from - 2] - scan_piece) +
                                         // Structure, undefended
                                         (((square_from + 2) & MCUMAX_BOARD_MASK) ||
                                          mcumax.board[square_from + 2] - scan_piece) -
                                         1 +
                                         // Squares plus bias
                                         (mcumax.board[square_from ^ 0x10] ==
                                          (mcumax.current_side + 36))) // Cling to magnetic king
                                    - (mcumax.non_pawn_material >> 2); // End-game Pawn-push bonus

                                // Promotion / passer bonus
                                capture_piece_value +=
                                    step_alpha =
                                        (square_to + step_vector + 1) & MCUMAX_SQUARE_INVALID
                                            ? (647 - scan_piece_type)
                                            : 2 * (scan_piece & (square_to + 0x10) & 0x20);

                                // Upgrade pawn or convert to queen
                                mcumax.board[square_to] += step_alpha;
                            }

#ifdef MCUMAX_HASHING_ENABLED
                            mcumax.hash_key += Hash(0);
                            mcumax.hash_key2 += Hash(8) + castling_rook_square - MCUMAX_SQUARE_INVALID;
#endif

                            // New score & alpha
                            step_score += score + capture_piece_value;
                            step_alpha = iter_score > alpha
                                             ? iter_score
                                             : alpha;

                            // New depth, reduce non-capture
                            step_depth = iter_depth - 1 -
                                         ((iter_depth > 5) &&
                                          (scan_piece_type > 2) &&
                                          !capture_piece &&
                                          !replay_move);

                            // Extend 1 ply if in check
                            if (!((mcumax.non_pawn_material > 30) ||
                                  (null_move_score - MCUMAX_SCORE_MAX) ||
                                  (iter_depth < 3) ||
                                  (capture_piece &&
                                   (scan_piece_type != 4))))
                                step_depth = iter_depth;

                            // Futility, recursive evaluation of reply
                            do
                            {
                                // Change side
                                mcumax.current_side ^= 0x18;

                                step_score_new = ((mode == MCUMAX_SEARCH_VALID_MOVES) ||
                                                  (step_depth > 2) ||
                                                  (step_score > step_alpha))
                                                     ? -mcumax_search(-beta,
                                                                      -step_alpha,
                                                                      -step_score,
                                                                      castling_skip_square,
                                                                      step_depth,
                                                                      MCUMAX_INTERNAL_NODE)
                                                     : step_score;

                                // Change side
                                mcumax.current_side ^= 0x18;
                            } while ((step_score_new > alpha) &&
                                     (++step_depth < iter_depth));

                            // No fail: re-search unreduced
                            step_score = step_score_new;

                            if ((mode == MCUMAX_PLAY_MOVE) &&
                                (step_score != -MCUMAX_SCORE_MAX) &&
                                (square_from == mcumax.square_from) &&
                                (square_to == mcumax.square_to))
                            {
                                // Playing move
                                mcumax.score = -score - capture_piece_value;
                                mcumax.en_passant_square = castling_skip_square;

#ifdef MCUMAX_HASHING_ENABLED
                                // Lock game in hash as draw
                                if (hash_entry)
                                {
                                    hash_entry->depth = MCUMAX_DEPTH_MAX;
                                    hash_entry->score = 0;
                                }
#endif

                                // Total captured material
                                mcumax.non_pawn_material += capture_piece_value >> 7;

                                // Change side
                                mcumax.current_side ^= 0x18;

                                // Captured non-pawn material
                                return beta;
                            }

#ifdef MCUMAX_HASHING_ENABLED
                            mcumax.hash_key = hash_key;
                            mcumax.hash_key2 = hash_key2;
#endif

                            // Undo move
                            mcumax.board[castling_rook_square] = mcumax.current_side + 6;
                            mcumax.board[castling_skip_square] = mcumax.board[square_to] = 0;
                            mcumax.board[square_from] = scan_piece;
                            mcumax.board[capture_square] = capture_piece;

                            // Unwind only after board/side/material have been restored.
                            if (mcumax.stop_search)
                                goto cutoff;

                            if ((mode == MCUMAX_SEARCH_BEST_MOVE) &&
                                (step_score != -MCUMAX_SCORE_MAX) &&
                                (square_from == mcumax.square_from) &&
                                (square_to == mcumax.square_to))
                                // Searching best move
                                return beta;

                            if ((mode == MCUMAX_SEARCH_VALID_MOVES) &&
                                (step_score != -MCUMAX_SCORE_MAX) &&
                                (mcumax.square_from == MCUMAX_SQUARE_INVALID) &&
                                (iter_depth == 3) &&
                                !replay_move)
                            {
                                // Searching valid moves
                                mcumax_move move = {square_from, square_to};

                                if (mcumax.valid_moves_num < mcumax.valid_moves_buffer_size)
                                    mcumax.valid_moves_buffer[mcumax.valid_moves_num] = move;

                                mcumax.valid_moves_num++;
                            }
                        }

                        // New best, update max,best
                        if (step_score > iter_score)
                        {
                            // Mark non-double
                            iter_score = step_score;
                            iter_square_from = square_from;
                            iter_square_to = square_to |
                                             (castling_skip_square & MCUMAX_SQUARE_INVALID);
                        }

                        if (replay_move)
                        {
                            // Redo after doing old best
                            replay_move = 0;

                            goto replay;
                        }

                        // Not first step, moved before
                        if ((square_from + step_vector - square_to) ||
                            (scan_piece & MCUMAX_PIECE_MOVED) ||
                            // No pawn and no lateral king move
                            ((scan_piece_type > 2) &&
                             (((scan_piece_type != 4) ||
                               (step_vector_index != 7) ||
                               // No virgin rook in corner
                               (mcumax.board[castling_rook_square =
                                                 ((square_from + 3) ^
                                                  ((step_vector >> 1) & 0b111))] -
                                mcumax.current_side - 6) ||
                               // No two empty squares next to rook
                               mcumax.board[castling_rook_square ^ 1] ||
                               mcumax.board[castling_rook_square ^ 2]))))
                            // Fake capture for nonsliding
                            capture_piece += (scan_piece_type < 5);
                        else
                            // Enable en-passant
                            castling_skip_square = square_to;

                        // If no capture, continue ray
                    } while (!capture_piece);
                }
            }

            // Next square of board, wrap
        } while ((square_from = ((square_from + 9) &
                                 ~MCUMAX_BOARD_MASK)) != square_start);

    cutoff:
        if ((mode == MCUMAX_SEARCH_BEST_MOVE) &&
            !mcumax.stop_search && iter_depth >= 3 &&
            iter_score > -MCUMAX_SCORE_MAX)
        {
            mcumax.completed_move = (mcumax_move){
                iter_square_from, iter_square_to & ~MCUMAX_BOARD_MASK};
        }

        // Check test thru NM best loses king: (stale)mate
        if ((iter_score == -MCUMAX_SCORE_MAX) &&
            (null_move_score != MCUMAX_SCORE_MAX))
            iter_score = 0;

#ifdef MCUMAX_HASHING_ENABLED
        // Protect game history
        if (hash_entry && hash_entry->depth < MCUMAX_DEPTH_MAX)
        {
            if (hash_entry->key2 == mcumax.hash_key2 &&
                hash_entry->depth > iter_depth &&
                hash_entry->depth < MCUMAX_DEPTH_MAX)
                mcumax_hash_stats.replace_deeper_lost++;

            mcumax_hash_stats.stores++;
            hash_entry->key2 = mcumax.hash_key2;
            hash_entry->score = iter_score;
            hash_entry->depth = iter_depth;

            // Move, type (bound/exact)
            hash_entry->square_from = iter_square_from |
                                      8 * (iter_score > alpha) |
                                      MCUMAX_SQUARE_INVALID * (iter_score < beta);
            hash_entry->square_to = iter_square_to;
        }
#endif

        // Kibitz
        // if (in_root)
        //     printf("%2d %6d %10d %c%c%c%c\n",
        //         iter_depth - 2,
        //         iter_score,
        //         mcumax.node_count,
        //         'a' + (iter_square_from & 0b111),
        //         '8' - (iter_square_from >> 4),
        //         'a' + (iter_square_to & 0b111),
        //         '8' - (iter_square_to >> 4 & 0b111));
    }

    // Delayed-loss bonus
    return iter_score += iter_score < score;
}

/***************************************************************************/

void mcumax_init()
{
    for (uint32_t x = 0; x < 8; x++)
    {
        // Setup pieces (left side)
        mcumax.board[0x10 * 0 + x] = MCUMAX_BOARD_BLACK | mcumax_board_setup[x];
        mcumax.board[0x10 * 1 + x] = MCUMAX_BOARD_BLACK | MCUMAX_PAWN_DOWNSTREAM;
        for (uint32_t y = 2; y < 6; y++)
            mcumax.board[0x10 * y + x] = MCUMAX_EMPTY;
        mcumax.board[0x10 * 6 + x] = MCUMAX_BOARD_WHITE | MCUMAX_PAWN_UPSTREAM;
        mcumax.board[0x10 * 7 + x] = MCUMAX_BOARD_WHITE | mcumax_board_setup[x];

        // Setup weights (right side)
        for (uint32_t y = 0; y < 8; y++)
            mcumax.board[16 * y + x + 8] = (x - 4) * (x - 4) + (y - 4) * (y - 3);
    }

    if (!mcumax_start_board_ready)
    {
        memcpy(mcumax_start_board, mcumax.board, sizeof(mcumax_start_board));
        mcumax_start_board_ready = true;
    }
    mcumax.current_side = MCUMAX_BOARD_WHITE;

    mcumax.score = 0;
    mcumax.en_passant_square = MCUMAX_SQUARE_INVALID;
    mcumax.non_pawn_material = 0;
    mcumax.stop_search = false;
    mcumax.completed_move = MCUMAX_MOVE_INVALID;
    mcumax.square_from = MCUMAX_SQUARE_INVALID;
    mcumax.square_to = MCUMAX_SQUARE_INVALID;
    mcumax.node_count = 0;
    mcumax.valid_moves_buffer = NULL;
    mcumax.valid_moves_buffer_size = 0;
    mcumax.valid_moves_num = 0;
    mcumax.user_callback = NULL;
    mcumax.user_data = NULL;

#ifdef MCUMAX_HASHING_ENABLED
    mcumax.hash_key = 0;
    mcumax.hash_key2 = 0;
    mcumax_hash_clear();
#endif
}

static mcumax_square mcumax_set_piece(mcumax_square square, mcumax_piece piece)
{
    if (square & MCUMAX_BOARD_MASK)
        return square;

    /* Do not set MCUMAX_PIECE_MOVED here: virginity comes from init layout,
     * castling-field parsing, and move execution (matches incremental hash). */
    mcumax.board[square] = piece;

    return square + 1;
}

mcumax_piece mcumax_get_piece(mcumax_square square)
{
    if (square & MCUMAX_BOARD_MASK)
        return MCUMAX_EMPTY;

    return (mcumax.board[square] & 0xf) ^ MCUMAX_BLACK;
}

uint8_t mcumax_get_board_byte(mcumax_square square)
{
    if (square & MCUMAX_BOARD_MASK)
        return MCUMAX_EMPTY;

    return mcumax.board[square];
}

void mcumax_set_fen_position(const char *fen_string)
{
    mcumax_init();

    uint32_t field_index = 0;
    uint32_t board_index = 0;
    uint8_t castle_rights = 0;

    char c;
    while ((c = *fen_string++))
    {
        if (c == ' ')
        {
            if (field_index < 4)
                field_index++;

            continue;
        }

        switch (field_index)
        {
        case 0:
            if (board_index < 0x80)
            {
                switch (c)
                {
                case '8':
                case '7':
                case '6':
                case '5':
                case '4':
                case '3':
                case '2':
                case '1':
                    for (int32_t i = 0; i < (c - '0'); i++)
                        board_index = mcumax_set_piece(board_index, MCUMAX_EMPTY);

                    break;

                case 'P':
                    board_index = mcumax_set_piece(board_index, MCUMAX_PAWN_UPSTREAM | MCUMAX_BOARD_WHITE);

                    break;

                case 'N':
                    board_index = mcumax_set_piece(board_index, MCUMAX_KNIGHT | MCUMAX_BOARD_WHITE);

                    break;

                case 'B':
                    board_index = mcumax_set_piece(board_index, MCUMAX_BISHOP | MCUMAX_BOARD_WHITE);

                    break;

                case 'R':
                    board_index = mcumax_set_piece(board_index, MCUMAX_ROOK | MCUMAX_BOARD_WHITE);

                    break;

                case 'Q':
                    board_index = mcumax_set_piece(board_index, MCUMAX_QUEEN | MCUMAX_BOARD_WHITE);

                    break;

                case 'K':
                    board_index = mcumax_set_piece(board_index, MCUMAX_KING | MCUMAX_BOARD_WHITE);

                    break;

                case 'p':
                    board_index = mcumax_set_piece(board_index, MCUMAX_PAWN_DOWNSTREAM | MCUMAX_BOARD_BLACK);

                    break;

                case 'n':
                    board_index = mcumax_set_piece(board_index, MCUMAX_KNIGHT | MCUMAX_BOARD_BLACK);

                    break;

                case 'b':
                    board_index = mcumax_set_piece(board_index, MCUMAX_BISHOP | MCUMAX_BOARD_BLACK);

                    break;

                case 'r':
                    board_index = mcumax_set_piece(board_index, MCUMAX_ROOK | MCUMAX_BOARD_BLACK);

                    break;

                case 'q':
                    board_index = mcumax_set_piece(board_index, MCUMAX_QUEEN | MCUMAX_BOARD_BLACK);

                    break;

                case 'k':
                    board_index = mcumax_set_piece(board_index, MCUMAX_KING | MCUMAX_BOARD_BLACK);

                    break;

                case '/':
                    board_index = (board_index < 0x80) ? (board_index & 0xf0) + 0x10 : board_index;

                    break;
                }
            }
            break;

        case 1:
            switch (c)
            {
            case 'w':
                mcumax.current_side = MCUMAX_BOARD_WHITE;

                break;

            case 'b':
                mcumax.current_side = MCUMAX_BOARD_BLACK;

                break;
            }
            break;

        case 2:
            switch (c)
            {
            case 'K':
                castle_rights |= MCUMAX_CASTLE_WK;
                break;

            case 'Q':
                castle_rights |= MCUMAX_CASTLE_WQ;
                break;

            case 'k':
                castle_rights |= MCUMAX_CASTLE_BK;
                break;

            case 'q':
                castle_rights |= MCUMAX_CASTLE_BQ;
                break;

            case '-':
                castle_rights = 0;
                break;
            }

            break;

        case 3:
            switch (c)
            {
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
            case 'g':
            case 'h':
                mcumax.en_passant_square &= 0x7f;
                mcumax.en_passant_square |= (c - 'a');

                break;

            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
                mcumax.en_passant_square &= 0x7f;
                mcumax.en_passant_square |= 16 * ('8' - c);

                break;
            }

            break;
        }
    }

    mcumax_apply_moved_flags_from_layout();
    mcumax_apply_castling_rights(castle_rights);

#ifdef MCUMAX_HASHING_ENABLED
    mcumax_hash_sync_after_fen();
#endif
}

mcumax_piece mcumax_get_current_side(void)
{
    return mcumax.current_side;
}

static int32_t mcumax_start_search(enum mcumax_mode mode,
                                   mcumax_move move,
                                   uint32_t depth_max,
                                   uint32_t node_max)
{
    mcumax.square_from = move.from;
    mcumax.square_to = move.to;

    mcumax.node_max = node_max;
    mcumax.node_count = 0;
    mcumax.depth_max = depth_max;

    mcumax.stop_search = false;
    mcumax.completed_move = MCUMAX_MOVE_INVALID;

#ifdef MCUMAX_HASHING_ENABLED
    mcumax_hash_reset_stats();
#endif

    return mcumax_search(-MCUMAX_SCORE_MAX,
                         MCUMAX_SCORE_MAX,
                         mcumax.score,
                         mcumax.en_passant_square,
                         3,
                         mode);
}

uint32_t mcumax_search_valid_moves(mcumax_move *valid_moves_buffer, uint32_t valid_moves_buffer_size)
{
    mcumax.valid_moves_num = 0;
    mcumax.valid_moves_buffer = valid_moves_buffer;
    mcumax.valid_moves_buffer_size = valid_moves_buffer_size;

    mcumax_start_search(MCUMAX_SEARCH_VALID_MOVES, MCUMAX_MOVE_INVALID, 0, 0);

    return mcumax.valid_moves_num;
}

mcumax_move mcumax_search_best_move(uint32_t node_max, uint32_t depth_max)
{
    int32_t score = mcumax_start_search(MCUMAX_SEARCH_BEST_MOVE,
                                        MCUMAX_MOVE_INVALID, depth_max + 3, node_max);

    if (mcumax.stop_search)
        return mcumax.completed_move;
    if (score == MCUMAX_SCORE_MAX)
        return (mcumax_move){mcumax.square_from, mcumax.square_to};
    else
        return MCUMAX_MOVE_INVALID;
}

bool mcumax_play_move(mcumax_move move)
{
    return mcumax_start_search(MCUMAX_PLAY_MOVE, move, MCUMAX_DEPTH_MAX,
                               UINT32_MAX) == MCUMAX_SCORE_MAX;
}

void mcumax_set_callback(mcumax_callback callback, void *userdata)
{
    mcumax.user_callback = callback;
    mcumax.user_data = userdata;
}

void mcumax_stop_search(void)
{
    mcumax.stop_search = true;
}

uint32_t mcumax_get_last_search_nodes(void)
{
    return mcumax.node_count;
}

#if MCUMAX_HASH_BITS > 0

void mcumax_hash_get_keys(uint32_t *key, uint32_t *key2)
{
    if (key != NULL)
        *key = mcumax.hash_key;
    if (key2 != NULL)
        *key2 = mcumax.hash_key2;
}

void mcumax_hash_reset_stats(void)
{
    memset(&mcumax_hash_stats, 0, sizeof(mcumax_hash_stats));
}

void mcumax_hash_get_stats(struct mcumax_hash_stats *out)
{
    if (out != NULL)
        *out = mcumax_hash_stats;
}

size_t mcumax_hash_table_bytes(void)
{
    return (size_t)MCUMAX_HASH_TABLE_SIZE * sizeof(struct HashEntry);
}

bool mcumax_hash_is_active(void)
{
    return mcumax_hash_table != NULL;
}

void mcumax_hash_clear(void)
{
    if (mcumax_hash_table)
        memset(mcumax_hash_table, 0,
               (size_t)MCUMAX_HASH_TABLE_SIZE * sizeof(struct HashEntry));
}

bool mcumax_hash_bind(void *table, bool owned)
{
    if (mcumax_hash_table_owned && mcumax_hash_table)
        free(mcumax_hash_table);
    mcumax_hash_table = (struct HashEntry *)table;
    mcumax_hash_table_owned = owned && (table != NULL);
    if (table)
        mcumax_hash_clear();
    return table != NULL;
}

bool mcumax_hash_alloc(void)
{
    if (mcumax_hash_table)
        return true;
    struct HashEntry *table =
        (struct HashEntry *)malloc(mcumax_hash_table_bytes());
    if (!table)
        return false;
    return mcumax_hash_bind(table, true);
}

void mcumax_hash_shutdown(void)
{
    if (mcumax_hash_table_owned && mcumax_hash_table)
        free(mcumax_hash_table);
    mcumax_hash_table = NULL;
    mcumax_hash_table_owned = false;
}

#endif
