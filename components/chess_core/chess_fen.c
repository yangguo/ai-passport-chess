/* Bounded FEN parse/export for Task 2.
 * Parsing builds a local position and commits to *out only on success,
 * so a failed import never modifies the caller's position. Export renders
 * into a stack buffer first and copies out only when it fits. */
#include "chess_core.h"

/* Split one space-separated field; rejects empty fields. */
static chess_error take_field(const char **cursor, const char **start,
                              size_t *len) {
  const char *p = *cursor;
  if (*p == '\0' || *p == ' ') {
    return CHESS_ERR_BAD_FEN;
  }
  const char *s = p;
  while (*p != '\0' && *p != ' ') {
    p++;
  }
  *start = s;
  *len = (size_t)(p - s);
  *cursor = p;
  return CHESS_OK;
}

/* Strict unsigned decimal fitting uint16_t. */
static chess_error parse_u16(const char *s, size_t len, uint16_t min,
                             uint16_t *out) {
  unsigned long acc = 0;
  size_t i;
  if (len == 0 || len > 5) {
    return CHESS_ERR_BAD_FEN;
  }
  for (i = 0; i < len; i++) {
    if (s[i] < '0' || s[i] > '9') {
      return CHESS_ERR_BAD_FEN;
    }
    acc = acc * 10u + (unsigned long)(s[i] - '0');
    if (acc > 65535u) {
      return CHESS_ERR_BAD_FEN;
    }
  }
  if (acc < min) {
    return CHESS_ERR_BAD_FEN;
  }
  *out = (uint16_t)acc;
  return CHESS_OK;
}

static chess_error piece_from_char(char c, chess_piece *out) {
  chess_color color;
  char lower;
  if (c >= 'A' && c <= 'Z') {
    color = CHESS_WHITE;
    lower = (char)(c - 'A' + 'a');
  } else {
    color = CHESS_BLACK;
    lower = c;
  }
  switch (lower) {
  case 'p':
    out->type = CHESS_PAWN;
    break;
  case 'n':
    out->type = CHESS_KNIGHT;
    break;
  case 'b':
    out->type = CHESS_BISHOP;
    break;
  case 'r':
    out->type = CHESS_ROOK;
    break;
  case 'q':
    out->type = CHESS_QUEEN;
    break;
  case 'k':
    out->type = CHESS_KING;
    break;
  default:
    return CHESS_ERR_BAD_FEN;
  }
  out->color = color;
  return CHESS_OK;
}

static char piece_to_char(chess_piece p) {
  static const char names[] = "pnbrqk";
  char c = names[p.type - 1];
  if (p.color == CHESS_WHITE) {
    c = (char)(c - 'a' + 'A');
  }
  return c;
}

/* Parse the placement field (rank 8 first). Records king squares. */
static chess_error parse_placement(const char *s, size_t len,
                                  chess_position *pos) {
  size_t i = 0;
  int rank = 7;
  int file = 0;
  unsigned white_kings = 0;
  unsigned black_kings = 0;

  for (unsigned sq = 0; sq < 64u; sq++) {
    pos->board[sq].type = CHESS_EMPTY;
    pos->board[sq].color = CHESS_WHITE;
  }
  pos->white_king = CHESS_NO_SQUARE;
  pos->black_king = CHESS_NO_SQUARE;

  while (i < len) {
    char c = s[i++];
    if (c == '/') {
      if (file != 8 || rank == 0) {
        return CHESS_ERR_BAD_FEN;
      }
      rank--;
      file = 0;
    } else if (c >= '1' && c <= '8') {
      file += c - '0';
      if (file > 8) {
        return CHESS_ERR_BAD_FEN;
      }
    } else {
      chess_piece p;
      unsigned sq;
      if (file >= 8 || rank < 0) {
        return CHESS_ERR_BAD_FEN;
      }
      if (piece_from_char(c, &p) != CHESS_OK) {
        return CHESS_ERR_BAD_FEN;
      }
      /* Pawns can never stand on the first or last rank. */
      if (p.type == CHESS_PAWN && (rank == 0 || rank == 7)) {
        return CHESS_ERR_BAD_FEN;
      }
      sq = (unsigned)(rank * 8 + file);
      pos->board[sq] = p;
      if (p.type == CHESS_KING) {
        if (p.color == CHESS_WHITE) {
          white_kings++;
          pos->white_king = (uint8_t)sq;
        } else {
          black_kings++;
          pos->black_king = (uint8_t)sq;
        }
        if (white_kings > 1u || black_kings > 1u) {
          return CHESS_ERR_BAD_FEN;
        }
      }
      file++;
    }
  }
  if (rank != 0 || file != 8) {
    return CHESS_ERR_BAD_FEN;
  }
  return CHESS_OK;
}

chess_error chess_position_from_fen(chess_position *out, const char *fen) {
  chess_position pos;
  const char *cursor;
  const char *field;
  size_t len;
  unsigned i;
  chess_error err;

  if (out == NULL || fen == NULL) {
    return CHESS_ERR_NULL;
  }

  /* Start from a clean slate; commit to *out only at the end. */
  for (unsigned sq = 0; sq < 64u; sq++) {
    pos.board[sq].type = CHESS_EMPTY;
    pos.board[sq].color = CHESS_WHITE;
  }
  pos.side_to_move = CHESS_WHITE;
  pos.castling = 0;
  pos.ep_square = CHESS_NO_SQUARE;
  pos.halfmove_clock = 0;
  pos.fullmove_number = 1;
  pos.white_king = CHESS_NO_SQUARE;
  pos.black_king = CHESS_NO_SQUARE;

  cursor = fen;
  for (i = 0; i < 6u; i++) {
    err = take_field(&cursor, &field, &len);
    if (err != CHESS_OK) {
      return err;
    }
    if (i < 5u) {
      if (*cursor != ' ') {
        return CHESS_ERR_BAD_FEN;
      }
      cursor++;
    } else if (*cursor != '\0') {
      return CHESS_ERR_BAD_FEN;
    }

    switch (i) {
    case 0:
      err = parse_placement(field, len, &pos);
      break;
    case 1:
      if (len != 1 || (field[0] != 'w' && field[0] != 'b')) {
        return CHESS_ERR_BAD_FEN;
      }
      pos.side_to_move = field[0] == 'w' ? CHESS_WHITE : CHESS_BLACK;
      err = CHESS_OK;
      break;
    case 2:
      if (len == 1 && field[0] == '-') {
        pos.castling = 0;
        err = CHESS_OK;
      } else if (len >= 1 && len <= 4) {
        uint8_t rights = 0;
        for (size_t k = 0; k < len; k++) {
          uint8_t bit;
          switch (field[k]) {
          case 'K':
            bit = CHESS_CASTLE_WK;
            break;
          case 'Q':
            bit = CHESS_CASTLE_WQ;
            break;
          case 'k':
            bit = CHESS_CASTLE_BK;
            break;
          case 'q':
            bit = CHESS_CASTLE_BQ;
            break;
          default:
            return CHESS_ERR_BAD_FEN;
          }
          if ((rights & bit) != 0u) {
            return CHESS_ERR_BAD_FEN; /* duplicate flag */
          }
          rights |= bit;
        }
        pos.castling = rights;
        err = CHESS_OK;
      } else {
        return CHESS_ERR_BAD_FEN;
      }
      break;
    case 3:
      if (len == 1 && field[0] == '-') {
        pos.ep_square = CHESS_NO_SQUARE;
        err = CHESS_OK;
      } else if (len == 2 && field[0] >= 'a' && field[0] <= 'h' &&
                 (field[1] == '3' || field[1] == '6')) {
        unsigned file = (unsigned)(field[0] - 'a');
        unsigned rank = field[1] == '3' ? 2u : 5u;
        pos.ep_square = (uint8_t)(rank * 8u + file);
        err = CHESS_OK;
      } else {
        return CHESS_ERR_BAD_FEN;
      }
      break;
    case 4:
      err = parse_u16(field, len, 0, &pos.halfmove_clock);
      break;
    default:
      err = parse_u16(field, len, 1, &pos.fullmove_number);
      break;
    }
    if (err != CHESS_OK) {
      return err;
    }
  }

  err = chess_position_validate(&pos);
  if (err != CHESS_OK) {
    return err;
  }
  *out = pos;
  return CHESS_OK;
}

chess_error chess_position_to_fen(const chess_position *pos, char *buf,
                                  size_t cap) {
  char tmp[CHESS_FEN_MAX];
  size_t n = 0;
  int rank;
  unsigned long v;
  char digits[8];
  int ndigits;
  size_t k;

  if (pos == NULL || buf == NULL) {
    return CHESS_ERR_NULL;
  }

#define EMIT(c)                                  \
  do {                                           \
    if (n >= sizeof(tmp)) {                      \
      return CHESS_ERR_BUFFER_TOO_SMALL;         \
    }                                            \
    tmp[n++] = (char)(c);                        \
  } while (0)

  for (rank = 7; rank >= 0; rank--) {
    int file = 0;
    int empty = 0;
    if (rank != 7) {
      EMIT('/');
    }
    while (file < 8) {
      chess_piece p = pos->board[(unsigned)(rank * 8 + file)];
      if (p.type == CHESS_EMPTY) {
        empty++;
      } else {
        if (empty > 0) {
          EMIT('0' + empty);
          empty = 0;
        }
        EMIT(piece_to_char(p));
      }
      file++;
    }
    if (empty > 0) {
      EMIT('0' + empty);
    }
  }

  EMIT(' ');
  EMIT(pos->side_to_move == CHESS_WHITE ? 'w' : 'b');
  EMIT(' ');
  if (pos->castling == 0u) {
    EMIT('-');
  } else {
    if ((pos->castling & CHESS_CASTLE_WK) != 0u) {
      EMIT('K');
    }
    if ((pos->castling & CHESS_CASTLE_WQ) != 0u) {
      EMIT('Q');
    }
    if ((pos->castling & CHESS_CASTLE_BK) != 0u) {
      EMIT('k');
    }
    if ((pos->castling & CHESS_CASTLE_BQ) != 0u) {
      EMIT('q');
    }
  }
  EMIT(' ');
  if (pos->ep_square == CHESS_NO_SQUARE) {
    EMIT('-');
  } else if (pos->ep_square < 64u) {
    EMIT('a' + (pos->ep_square % 8u));
    EMIT('1' + (pos->ep_square / 8u));
  } else {
    return CHESS_ERR_BAD_FEN;
  }

  /* Halfmove and fullmove counters. */
  for (k = 0; k < 2u; k++) {
    v = k == 0u ? pos->halfmove_clock : pos->fullmove_number;
    ndigits = 0;
    EMIT(' ');
    do {
      digits[ndigits++] = (char)('0' + v % 10u);
      v /= 10u;
    } while (v > 0);
    while (ndigits > 0) {
      EMIT(digits[--ndigits]);
    }
  }
  EMIT('\0');

#undef EMIT

  if (n > cap) {
    return CHESS_ERR_BUFFER_TOO_SMALL;
  }
  for (k = 0; k < n; k++) {
    buf[k] = tmp[k];
  }
  return CHESS_OK;
}
