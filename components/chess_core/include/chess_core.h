/* chess_core public API — Task 2 subset: positions and FEN only.
 * Move generation, game outcomes and perft arrive in Tasks 3-5.
 *
 * Board uses 64-square mailbox, a1=0 (rank-major). Pure C11, no heap,
 * no ESP-IDF/LVGL dependency; identical logic for host tests and firmware.
 */
#ifndef CHESS_CORE_H
#define CHESS_CORE_H

#include <stddef.h>
#include <stdint.h>

typedef enum chess_error {
  CHESS_OK = 0,
  CHESS_ERR_NULL = 1,
  CHESS_ERR_BAD_FEN = 2,
  CHESS_ERR_BUFFER_TOO_SMALL = 3
} chess_error;

typedef enum chess_color {
  CHESS_WHITE = 0,
  CHESS_BLACK = 1
} chess_color;

typedef enum chess_piece_type {
  CHESS_EMPTY = 0,
  CHESS_PAWN = 1,
  CHESS_KNIGHT = 2,
  CHESS_BISHOP = 3,
  CHESS_ROOK = 4,
  CHESS_QUEEN = 5,
  CHESS_KING = 6
} chess_piece_type;

typedef struct chess_piece {
  chess_piece_type type;
  /* Meaningful only when type != CHESS_EMPTY. */
  chess_color color;
} chess_piece;

#define CHESS_NO_SQUARE 64u
#define CHESS_FEN_MAX 128u

#define CHESS_CASTLE_WK 0x01u
#define CHESS_CASTLE_WQ 0x02u
#define CHESS_CASTLE_BK 0x04u
#define CHESS_CASTLE_BQ 0x08u

typedef struct chess_position {
  chess_piece board[64];
  chess_color side_to_move;
  /* CHESS_CASTLE_* bits. */
  uint8_t castling;
  /* 0..63, or CHESS_NO_SQUARE when no en-passant target. */
  uint8_t ep_square;
  uint16_t halfmove_clock;
  uint16_t fullmove_number;
  uint8_t white_king;
  uint8_t black_king;
} chess_position;

/* Structural validation: king counts/squares, king adjacency, castling bits,
 * ep rank/side consistency, counter ranges. Check-based rejection (both
 * sides in check, non-mover in check) needs is_attacked and is enforced
 * from Task 3. */
chess_error chess_position_validate(const chess_position *pos);

/* Parse six-field FEN. On failure returns an error and leaves *out
 * untouched. Imported FEN starts a fresh repetition history (handled by
 * the game layer in Task 5). */
chess_error chess_position_from_fen(chess_position *out, const char *fen);

/* Export canonical FEN (fits CHESS_FEN_MAX incl. NUL). On small buffers
 * returns CHESS_ERR_BUFFER_TOO_SMALL and leaves buf untouched. */
chess_error chess_position_to_fen(const chess_position *pos, char *buf,
                                  size_t cap);

#endif /* CHESS_CORE_H */
