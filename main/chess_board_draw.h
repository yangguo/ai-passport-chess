/* Board snapshot renderer (Task 8): one LVGL object, one draw event,
 * absolute coordinates. The draw callback only reads the snapshot;
 * the app updates it under the LVGL lock, then invalidates.
 * Layout: board origin (0,30), 30px cells, the chosen player at the bottom.
 * Pieces are procedural LVGL primitives (no Flash assets for M1):
 * silhouettes differ by shape family so knight/bishop stay apart.
 */
#ifndef CHESS_BOARD_DRAW_H
#define CHESS_BOARD_DRAW_H

#include "chess_core.h"
#include "lvgl.h"

#define CHESS_BOARD_X 0
#define CHESS_BOARD_Y 30
#define CHESS_BOARD_CELL 30

typedef struct chess_snapshot {
    chess_piece cells[64]; /* a1 = index 0 */
    uint8_t selected;      /* CHESS_NO_SQUARE when none */
    uint8_t targets[64];
    uint8_t ntargets;
    uint8_t hover;         /* focused square, CHESS_NO_SQUARE when none */
    uint8_t check;         /* checked king square, CHESS_NO_SQUARE when none */
    uint8_t last_from;
    uint8_t last_to;       /* CHESS_NO_SQUARE when no last move */
    chess_color side;
    chess_color bottom; /* stable perspective, independent of side-to-move */
} chess_snapshot;

/* Build the board widget once; later snapshots go through update. */
lv_obj_t *chess_board_create(lv_obj_t *parent, const chess_snapshot *snap);
void chess_board_update(const chess_snapshot *snap);

#endif /* CHESS_BOARD_DRAW_H */
