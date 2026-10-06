/* chess_core public API — Task 3 subset: positions, FEN, move generation.
 * Special moves (castling, en-passant capture, promotion) arrive in Task 4;
 * game outcomes in Task 5.
 *
 * Board uses 64-square mailbox, a1=0 (rank-major). Pure C11, no heap,
 * no ESP-IDF/LVGL dependency; identical logic for host tests and firmware.
 */
#ifndef CHESS_CORE_H
#define CHESS_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum chess_error {
  CHESS_OK = 0,
  CHESS_ERR_NULL = 1,
  CHESS_ERR_BAD_FEN = 2,
  CHESS_ERR_BUFFER_TOO_SMALL = 3,
  CHESS_ERR_ILLEGAL_MOVE = 4
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

/* Upper bound for any legal move list; real positions need far fewer. */
#define CHESS_MAX_MOVES 256u

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

typedef struct chess_move {
  uint8_t from; /* 0..63 */
  uint8_t to;   /* 0..63 */
  /* CHESS_EMPTY when not a promotion (promotions arrive in Task 4). */
  chess_piece_type promotion;
} chess_move;

typedef struct chess_undo {
  uint8_t from;
  uint8_t to;
  chess_piece moved;
  chess_piece captured;
  /* Square the captured piece is restored to (differs from `to` for
   * en-passant captures in Task 4). */
  uint8_t capture_square;
  uint8_t castling;
  uint8_t ep_square;
  uint16_t halfmove_clock;
  uint16_t fullmove_number;
  uint8_t white_king;
  uint8_t black_king;
  /* Castling rook squares; CHESS_NO_SQUARE when the move is not a
   * castle (castling arrives in Task 4). */
  uint8_t castle_rook_from;
  uint8_t castle_rook_to;
} chess_undo;

/* Structural validation: king counts/squares, king adjacency, castling bits,
 * ep rank/side consistency, counter ranges. */
chess_error chess_position_validate(const chess_position *pos);

/* Parse six-field FEN. On failure returns an error and leaves *out
 * untouched. Imported FEN starts a fresh repetition history (handled by
 * the game layer in Task 5). Since Task 3, obviously unreachable
 * check positions (both sides checked, non-mover checked) are rejected. */
chess_error chess_position_from_fen(chess_position *out, const char *fen);

/* Export canonical FEN (fits CHESS_FEN_MAX incl. NUL). On small buffers
 * returns CHESS_ERR_BUFFER_TOO_SMALL and leaves buf untouched. */
chess_error chess_position_to_fen(const chess_position *pos, char *buf,
                                  size_t cap);

/* True when `sq` is attacked by color `by`. Independent of move
 * legality: pinned pieces still count as attackers. Out-of-range
 * squares are never attacked. */
bool chess_is_attacked(const chess_position *pos, uint8_t sq, chess_color by);

/* All legal moves for the side to move. Never generates king captures.
 * On small `cap` returns CHESS_ERR_BUFFER_TOO_SMALL with *count set to
 * the required size and `out` untouched. */
chess_error chess_generate_legal(const chess_position *pos, chess_move *out,
                                 size_t cap, size_t *count);

/* Play a fully legal move. Rejects anything outside the legal set with
 * CHESS_ERR_ILLEGAL_MOVE and leaves *pos untouched. */
chess_error chess_make(chess_position *pos, chess_move move, chess_undo *undo);

/* Revert a move made with chess_make. NULL arguments are a no-op. */
void chess_unmake(chess_position *pos, const chess_undo *undo);

#endif /* CHESS_CORE_H */
