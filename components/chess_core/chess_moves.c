/* Task 3: ordinary moves with reversible execution.
 * Pseudo-legal generation (pawn pushes/captures without promotion,
 * knights, sliders, king steps) + own-king safety filter via internal
 * make/unmake. Castling, en-passant capture and promotions arrive in
 * Task 4; positions needing them never occur in the Task 3 fixtures,
 * and pawn pushes onto the last rank are withheld until Task 4. */
#include "chess_core.h"
#include "chess_core_internal.h"

static int file_of(unsigned sq) {
  return (int)(sq % 8u);
}

static int rank_of(unsigned sq) {
  return (int)(sq / 8u);
}

static bool on_board(int f, int r) {
  return f >= 0 && f < 8 && r >= 0 && r < 8;
}

static bool is_enemy_king(chess_piece p, chess_color mover) {
  return p.type == CHESS_KING && p.color != mover;
}

bool chess_is_attacked(const chess_position *pos, uint8_t sq, chess_color by) {
  static const int knight_d[8][2] = {{1, 2}, {2, 1},  {2, -1}, {1, -2},
                                     {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
  static const int king_d[8][2] = {{1, 0},  {1, 1},  {0, 1},  {-1, 1},
                                   {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
  static const int rook_d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  static const int bishop_d[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  int f;
  int r;
  int i;

  if (pos == NULL || sq >= 64u) {
    return false;
  }
  f = file_of(sq);
  r = rank_of(sq);

  /* Pawn attackers stand one rank behind the victim. */
  {
    int pr = (by == CHESS_WHITE) ? r - 1 : r + 1;
    if (pr >= 0 && pr < 8) {
      int pf;
      for (pf = f - 1; pf <= f + 1; pf += 2) {
        if (pf >= 0 && pf < 8) {
          chess_piece p = pos->board[(unsigned)(pr * 8 + pf)];
          if (p.type == CHESS_PAWN && p.color == by) {
            return true;
          }
        }
      }
    }
  }

  /* Knight attackers. */
  for (i = 0; i < 8; i++) {
    int nf = f + knight_d[i][0];
    int nr = r + knight_d[i][1];
    if (on_board(nf, nr)) {
      chess_piece p = pos->board[(unsigned)(nr * 8 + nf)];
      if (p.type == CHESS_KNIGHT && p.color == by) {
        return true;
      }
    }
  }

  /* King attackers. */
  for (i = 0; i < 8; i++) {
    int nf = f + king_d[i][0];
    int nr = r + king_d[i][1];
    if (on_board(nf, nr)) {
      chess_piece p = pos->board[(unsigned)(nr * 8 + nf)];
      if (p.type == CHESS_KING && p.color == by) {
        return true;
      }
    }
  }

  /* Sliding attackers: rook/queen orthogonal, bishop/queen diagonal.
   * Blocked pieces still shield; pinned pieces still attack. */
  for (i = 0; i < 4; i++) {
    int nf = f + rook_d[i][0];
    int nr = r + rook_d[i][1];
    while (on_board(nf, nr)) {
      chess_piece p = pos->board[(unsigned)(nr * 8 + nf)];
      if (p.type != CHESS_EMPTY) {
        if (p.color == by &&
            (p.type == CHESS_ROOK || p.type == CHESS_QUEEN)) {
          return true;
        }
        break;
      }
      nf += rook_d[i][0];
      nr += rook_d[i][1];
    }
  }
  for (i = 0; i < 4; i++) {
    int nf = f + bishop_d[i][0];
    int nr = r + bishop_d[i][1];
    while (on_board(nf, nr)) {
      chess_piece p = pos->board[(unsigned)(nr * 8 + nf)];
      if (p.type != CHESS_EMPTY) {
        if (p.color == by &&
            (p.type == CHESS_BISHOP || p.type == CHESS_QUEEN)) {
          return true;
        }
        break;
      }
      nf += bishop_d[i][0];
      nr += bishop_d[i][1];
    }
  }
  return false;
}

/* ---- pseudo-legal generation into a fixed local buffer ---- */

typedef struct pseudo_list {
  chess_move moves[CHESS_MAX_MOVES];
  size_t count;
} pseudo_list;

static void push_pseudo(pseudo_list *list, uint8_t from, uint8_t to) {
  chess_move m;
  if (list->count >= CHESS_MAX_MOVES) {
    return; /* Real positions never reach this; capped defensively. */
  }
  m.from = from;
  m.to = to;
  m.promotion = CHESS_EMPTY;
  list->moves[list->count++] = m;
}

static void gen_pawn(const chess_position *pos, unsigned sq, pseudo_list *out) {
  chess_color mover = pos->side_to_move;
  int f = file_of(sq);
  int r = rank_of(sq);
  int dir = (mover == CHESS_WHITE) ? 1 : -1;
  int start = (mover == CHESS_WHITE) ? 1 : 6;
  int last = (mover == CHESS_WHITE) ? 7 : 0;
  int tr = r + dir;
  int tf;
  unsigned one;

  if (tr < 0 || tr > 7) {
    return;
  }
  one = (unsigned)(tr * 8 + f);
  if (pos->board[one].type == CHESS_EMPTY) {
    if (tr == last) {
      /* Promotion push: withheld until Task 4. */
    } else {
      push_pseudo(out, (uint8_t)sq, (uint8_t)one);
      if (r == start) {
        unsigned two = (unsigned)((r + 2 * dir) * 8 + f);
        if (pos->board[two].type == CHESS_EMPTY) {
          push_pseudo(out, (uint8_t)sq, (uint8_t)two);
        }
      }
    }
  }
  for (tf = f - 1; tf <= f + 1; tf += 2) {
    if (tf >= 0 && tf < 8) {
      chess_piece target = pos->board[(unsigned)(tr * 8 + tf)];
      if (target.type != CHESS_EMPTY && target.color != mover &&
          !is_enemy_king(target, mover) && tr != last) {
        push_pseudo(out, (uint8_t)sq, (uint8_t)(tr * 8 + tf));
      }
      /* Promotion captures and en-passant captures: Task 4. */
    }
  }
}

static void gen_steps(const chess_position *pos, unsigned sq,
                      const int dirs[][2], int ndirs, pseudo_list *out) {
  chess_color mover = pos->side_to_move;
  int f = file_of(sq);
  int r = rank_of(sq);
  int i;

  for (i = 0; i < ndirs; i++) {
    int nf = f + dirs[i][0];
    int nr = r + dirs[i][1];
    if (on_board(nf, nr)) {
      chess_piece target = pos->board[(unsigned)(nr * 8 + nf)];
      if (target.type == CHESS_EMPTY ||
          (target.color != mover && !is_enemy_king(target, mover))) {
        push_pseudo(out, (uint8_t)sq, (uint8_t)(nr * 8 + nf));
      }
    }
  }
}

static void gen_rays(const chess_position *pos, unsigned sq,
                     const int dirs[][2], int ndirs, pseudo_list *out) {
  chess_color mover = pos->side_to_move;
  int f = file_of(sq);
  int r = rank_of(sq);
  int i;

  for (i = 0; i < ndirs; i++) {
    int nf = f + dirs[i][0];
    int nr = r + dirs[i][1];
    while (on_board(nf, nr)) {
      chess_piece target = pos->board[(unsigned)(nr * 8 + nf)];
      if (target.type == CHESS_EMPTY) {
        push_pseudo(out, (uint8_t)sq, (uint8_t)(nr * 8 + nf));
      } else {
        if (target.color != mover && !is_enemy_king(target, mover)) {
          push_pseudo(out, (uint8_t)sq, (uint8_t)(nr * 8 + nf));
        }
        break;
      }
      nf += dirs[i][0];
      nr += dirs[i][1];
    }
  }
}

static void gen_pseudo(const chess_position *pos, pseudo_list *out) {
  static const int knight_d[8][2] = {{1, 2}, {2, 1},  {2, -1}, {1, -2},
                                     {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
  static const int king_d[8][2] = {{1, 0},  {1, 1},  {0, 1},  {-1, 1},
                                   {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
  static const int rook_d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  static const int bishop_d[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  unsigned sq;

  out->count = 0;
  for (sq = 0; sq < 64u; sq++) {
    chess_piece p = pos->board[sq];
    if (p.type == CHESS_EMPTY || p.color != pos->side_to_move) {
      continue;
    }
    switch (p.type) {
    case CHESS_PAWN:
      gen_pawn(pos, sq, out);
      break;
    case CHESS_KNIGHT:
      gen_steps(pos, sq, knight_d, 8, out);
      break;
    case CHESS_BISHOP:
      gen_rays(pos, sq, bishop_d, 4, out);
      break;
    case CHESS_ROOK:
      gen_rays(pos, sq, rook_d, 4, out);
      break;
    case CHESS_QUEEN:
      gen_rays(pos, sq, rook_d, 4, out);
      gen_rays(pos, sq, bishop_d, 4, out);
      break;
    case CHESS_KING:
      /* Castling withheld until Task 4. */
      gen_steps(pos, sq, king_d, 8, out);
      break;
    default:
      break;
    }
  }
}

/* ---- make / unmake ---- */

static void clear_right_for_corner(uint8_t *rights, uint8_t sq) {
  switch (sq) {
  case 0:
    *rights = (uint8_t)(*rights & ~CHESS_CASTLE_WQ);
    break;
  case 7:
    *rights = (uint8_t)(*rights & ~CHESS_CASTLE_WK);
    break;
  case 56:
    *rights = (uint8_t)(*rights & ~CHESS_CASTLE_BQ);
    break;
  case 63:
    *rights = (uint8_t)(*rights & ~CHESS_CASTLE_BK);
    break;
  default:
    break;
  }
}

void chess_make_unchecked(chess_position *pos, chess_move move,
                          chess_undo *undo) {
  chess_color mover = pos->side_to_move;
  chess_piece mp = pos->board[move.from];
  chess_piece cp = pos->board[move.to];
  int span = (int)move.to - (int)move.from;

  undo->from = move.from;
  undo->to = move.to;
  undo->moved = mp;
  undo->captured = cp;
  undo->capture_square = move.to;
  undo->castling = pos->castling;
  undo->ep_square = pos->ep_square;
  undo->halfmove_clock = pos->halfmove_clock;
  undo->fullmove_number = pos->fullmove_number;
  undo->white_king = pos->white_king;
  undo->black_king = pos->black_king;
  undo->castle_rook_from = CHESS_NO_SQUARE;
  undo->castle_rook_to = CHESS_NO_SQUARE;

  pos->board[move.to] = mp;
  pos->board[move.from].type = CHESS_EMPTY;
  pos->board[move.from].color = CHESS_WHITE;

  if (mp.type == CHESS_KING) {
    if (mover == CHESS_WHITE) {
      pos->white_king = move.to;
      pos->castling =
          (uint8_t)(pos->castling & ~(CHESS_CASTLE_WK | CHESS_CASTLE_WQ));
    } else {
      pos->black_king = move.to;
      pos->castling =
          (uint8_t)(pos->castling & ~(CHESS_CASTLE_BK | CHESS_CASTLE_BQ));
    }
  }
  if (mp.type == CHESS_ROOK) {
    clear_right_for_corner(&pos->castling, move.from);
  }
  if (cp.type != CHESS_EMPTY) {
    clear_right_for_corner(&pos->castling, move.to);
  }

  /* Double pawn push opens an ep target; anything else closes it. */
  if (mp.type == CHESS_PAWN && (span == 16 || span == -16)) {
    pos->ep_square = (uint8_t)(((int)move.from + (int)move.to) / 2);
  } else {
    pos->ep_square = CHESS_NO_SQUARE;
  }

  if (mp.type == CHESS_PAWN || cp.type != CHESS_EMPTY) {
    pos->halfmove_clock = 0;
  } else if (pos->halfmove_clock < 65535u) {
    pos->halfmove_clock++;
  }
  if (mover == CHESS_BLACK && pos->fullmove_number < 65535u) {
    pos->fullmove_number++;
  }
  pos->side_to_move = (chess_color)(mover ^ 1u);
}

void chess_unmake(chess_position *pos, const chess_undo *undo) {
  if (pos == NULL || undo == NULL) {
    return;
  }
  pos->side_to_move = (chess_color)(pos->side_to_move ^ 1u);
  pos->castling = undo->castling;
  pos->ep_square = undo->ep_square;
  pos->halfmove_clock = undo->halfmove_clock;
  pos->fullmove_number = undo->fullmove_number;
  pos->white_king = undo->white_king;
  pos->black_king = undo->black_king;

  if (undo->castle_rook_from != CHESS_NO_SQUARE) {
    pos->board[undo->castle_rook_from] =
        pos->board[undo->castle_rook_to];
    pos->board[undo->castle_rook_to].type = CHESS_EMPTY;
    pos->board[undo->castle_rook_to].color = CHESS_WHITE;
  }
  pos->board[undo->from] = undo->moved;
  pos->board[undo->to].type = CHESS_EMPTY;
  pos->board[undo->to].color = CHESS_WHITE;
  if (undo->captured.type != CHESS_EMPTY) {
    pos->board[undo->capture_square] = undo->captured;
  }
}

chess_error chess_generate_legal(const chess_position *pos, chess_move *out,
                                 size_t cap, size_t *count) {
  pseudo_list pseudo;
  chess_move legal[CHESS_MAX_MOVES];
  size_t legal_count = 0;
  size_t i;

  if (pos == NULL || out == NULL || count == NULL) {
    return CHESS_ERR_NULL;
  }
  gen_pseudo(pos, &pseudo);
  for (i = 0; i < pseudo.count; i++) {
    /* chess_position is small enough to copy for the safety probe. */
    chess_position probe = *pos;
    chess_undo undo;
    chess_color mover = pos->side_to_move;
    uint8_t ksq;
    chess_make_unchecked(&probe, pseudo.moves[i], &undo);
    ksq = (mover == CHESS_WHITE) ? probe.white_king : probe.black_king;
    if (!chess_is_attacked(&probe, ksq, probe.side_to_move)) {
      if (legal_count >= CHESS_MAX_MOVES) {
        return CHESS_ERR_BUFFER_TOO_SMALL;
      }
      legal[legal_count++] = pseudo.moves[i];
    }
  }
  if (cap < legal_count) {
    *count = legal_count;
    return CHESS_ERR_BUFFER_TOO_SMALL;
  }
  for (i = 0; i < legal_count; i++) {
    out[i] = legal[i];
  }
  *count = legal_count;
  return CHESS_OK;
}

chess_error chess_make(chess_position *pos, chess_move move, chess_undo *undo) {
  chess_move legal[CHESS_MAX_MOVES];
  size_t count = 0;
  size_t i;
  chess_error err;

  if (pos == NULL || undo == NULL) {
    return CHESS_ERR_NULL;
  }
  if (move.from >= 64u || move.to >= 64u || move.from == move.to) {
    return CHESS_ERR_ILLEGAL_MOVE;
  }
  /* The generator never emits promotions yet (Task 4); anything else
   * must match the legal set exactly. Generation is side-effect free. */
  err = chess_generate_legal(pos, legal, CHESS_MAX_MOVES, &count);
  if (err != CHESS_OK) {
    return err;
  }
  for (i = 0; i < count; i++) {
    if (legal[i].from == move.from && legal[i].to == move.to &&
        legal[i].promotion == move.promotion) {
      chess_make_unchecked(pos, move, undo);
      return CHESS_OK;
    }
  }
  return CHESS_ERR_ILLEGAL_MOVE;
}
