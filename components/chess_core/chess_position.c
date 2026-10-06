/* Structural position validation for Task 2.
 * No attack detection here: check-based FEN rejection belongs to Task 3
 * (is_attacked) and must not be silently skipped once it exists. */
#include "chess_core.h"

static int king_distance_chev(uint8_t a, uint8_t b) {
  int df = (int)(a % 8u) - (int)(b % 8u);
  int dr = (int)(a / 8u) - (int)(b / 8u);
  if (df < 0) {
    df = -df;
  }
  if (dr < 0) {
    dr = -dr;
  }
  return df > dr ? df : dr;
}

chess_error chess_position_validate(const chess_position *pos) {
  if (pos == NULL) {
    return CHESS_ERR_NULL;
  }

  /* Exactly one king per side, and cached squares must match the board. */
  unsigned white_kings = 0;
  unsigned black_kings = 0;
  uint8_t wk = CHESS_NO_SQUARE;
  uint8_t bk = CHESS_NO_SQUARE;
  for (unsigned sq = 0; sq < 64u; sq++) {
    chess_piece p = pos->board[sq];
    if (p.type == CHESS_KING) {
      if (p.color == CHESS_WHITE) {
        white_kings++;
        wk = (uint8_t)sq;
      } else {
        black_kings++;
        bk = (uint8_t)sq;
      }
    } else if (p.type == CHESS_EMPTY) {
      continue;
    } else if (p.type > CHESS_KING) {
      return CHESS_ERR_BAD_FEN;
    }
  }
  if (white_kings != 1u || black_kings != 1u) {
    return CHESS_ERR_BAD_FEN;
  }
  if (pos->white_king != wk || pos->black_king != bk) {
    return CHESS_ERR_BAD_FEN;
  }
  /* Kings must never stand adjacent. */
  if (king_distance_chev(wk, bk) <= 1) {
    return CHESS_ERR_BAD_FEN;
  }

  if (pos->side_to_move != CHESS_WHITE && pos->side_to_move != CHESS_BLACK) {
    return CHESS_ERR_BAD_FEN;
  }
  if ((pos->castling & (uint8_t)~0x0Fu) != 0u) {
    return CHESS_ERR_BAD_FEN;
  }

  /* En-passant square must sit on the rank the mover can capture onto:
   * rank 6 (index 5) with White to move, rank 3 (index 2) with Black. */
  if (pos->ep_square == CHESS_NO_SQUARE) {
    /* No target: nothing to check. */
  } else if (pos->ep_square >= 64u) {
    return CHESS_ERR_BAD_FEN;
  } else {
    unsigned ep_rank = (unsigned)(pos->ep_square / 8u);
    if (pos->side_to_move == CHESS_WHITE && ep_rank != 5u) {
      return CHESS_ERR_BAD_FEN;
    }
    if (pos->side_to_move == CHESS_BLACK && ep_rank != 2u) {
      return CHESS_ERR_BAD_FEN;
    }
  }

  if (pos->fullmove_number < 1u) {
    return CHESS_ERR_BAD_FEN;
  }
  return CHESS_OK;
}
