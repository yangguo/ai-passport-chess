/* Internal entry points for perft and host tests only.
 * Firmware application code must use the checked chess_make; the
 * unchecked variant skips legality filtering and must only run on
 * moves produced by chess_generate_legal. */
#ifndef CHESS_CORE_INTERNAL_H
#define CHESS_CORE_INTERNAL_H

#include "chess_core.h"

/* Execute a move assumed legal; fills *undo for chess_unmake. */
void chess_make_unchecked(chess_position *pos, chess_move move,
                          chess_undo *undo);

#endif /* CHESS_CORE_INTERNAL_H */
