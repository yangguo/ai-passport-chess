#ifndef CHESS_BOARD_LAYOUT_H
#define CHESS_BOARD_LAYOUT_H
#include "chess_core.h"

/* Square indices remain a1=0; only the display coordinates rotate. */
static inline unsigned chess_board_column(unsigned square, chess_color bottom) {
    return bottom == CHESS_BLACK ? 7u - square % 8u : square % 8u;
}
static inline unsigned chess_board_row(unsigned square, chess_color bottom) {
    return bottom == CHESS_BLACK ? square / 8u : 7u - square / 8u;
}
#endif
