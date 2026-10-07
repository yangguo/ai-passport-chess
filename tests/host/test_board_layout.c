#include "chess_board_layout.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* Hand-checked corners: White sees a8 top-left, Black sees h1. */
    assert(chess_board_column(56, CHESS_WHITE) == 0);
    assert(chess_board_row(56, CHESS_WHITE) == 0);
    assert(chess_board_column(7, CHESS_WHITE) == 7);
    assert(chess_board_row(7, CHESS_WHITE) == 7);
    assert(chess_board_column(7, CHESS_BLACK) == 0);
    assert(chess_board_row(7, CHESS_BLACK) == 0);
    assert(chess_board_column(56, CHESS_BLACK) == 7);
    assert(chess_board_row(56, CHESS_BLACK) == 7);
    /* e2-e4 stays e2-e4, but appears on Black's upper half. */
    assert(chess_board_column(12, CHESS_BLACK) == 3);
    assert(chess_board_row(12, CHESS_BLACK) == 1);
    assert(chess_board_row(28, CHESS_BLACK) == 3);
    puts("PASS: both board perspectives and unchanged chess coordinates");
    return 0;
}
