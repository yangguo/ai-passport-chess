/* Position-keyed opening book (adapter layer). Generated table in opening_book.c. */
#ifndef OPENING_BOOK_H
#define OPENING_BOOK_H

#include <stdbool.h>
#include <stddef.h>

#include "chess_core.h"

#define OPENING_BOOK_MAX_ALT 4u

bool chess_opening_book_probe(const chess_position *pos, uint32_t book_seed,
                              bool book_enabled, chess_move *out);

size_t chess_opening_book_position_count(void);

int chess_opening_book_selfcheck(void);

#endif /* OPENING_BOOK_H */
