/* In-process mcu-max adapter (host): FEN in, bounded search, UCI out,
 * core-validated. No callbacks installed: every search ends by
 * node/depth budget (the firmware worker adds deadline/cancel later).
 * Promotion replies are always queens — the engine's documented limit;
 * the core still accepts all four promotions and validates everything.
 */
#ifndef CHESS_AI_H
#define CHESS_AI_H

#include <stdint.h>

#include "chess_core.h"

/* Suggest a move for the side to move. Returns 0 with *out set, or
 * nonzero on any failure (null args, no FEN, no reply from the
 * engine, out-of-range squares, core rejection). Not reentrant:
 * the engine holds global state; one caller at a time. */
int chess_ai_suggest(const chess_position *pos, uint32_t node_max,
                     unsigned depth_max, chess_move *out);

#endif /* CHESS_AI_H */
