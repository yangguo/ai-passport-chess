/* Task 5: game outcomes, repetition and draw claims.
 * The 34-byte key compares by exact bytes (never hashes); the window
 * restarts past every irreversible move, so 151 entries always suffice:
 * 150 reversible halfmoves force the 75-move rule first. */
#include "chess_core.h"

static uint8_t piece_code(chess_piece p) {
  if (p.type == CHESS_EMPTY) {
    return 0;
  }
  return (uint8_t)(p.type + (p.color == CHESS_BLACK ? 6 : 0));
}

static bool has_legal_ep_capture(const chess_position *pos) {
  chess_move moves[CHESS_MAX_MOVES];
  size_t count = 0;
  size_t i;
  if (pos->ep_square == CHESS_NO_SQUARE) {
    return false;
  }
  if (chess_generate_legal(pos, moves, CHESS_MAX_MOVES, &count) != CHESS_OK) {
    return false;
  }
  for (i = 0; i < count; i++) {
    if (moves[i].to == pos->ep_square &&
        pos->board[moves[i].from].type == CHESS_PAWN) {
      return true;
    }
  }
  return false;
}

void chess_position_key(const chess_position *pos, uint8_t key[34]) {
  unsigned i;
  if (pos == NULL || key == NULL) {
    return;
  }
  for (i = 0; i < 32u; i++) {
    uint8_t lo = piece_code(pos->board[2u * i]);
    uint8_t hi = piece_code(pos->board[2u * i + 1u]);
    key[i] = (uint8_t)((hi << 4) | lo);
  }
  key[32] = (uint8_t)(pos->castling | (pos->side_to_move == CHESS_BLACK ? 0x10u : 0u));
  if (has_legal_ep_capture(pos)) {
    key[33] = (uint8_t)(pos->ep_square % 8u);
  } else {
    key[33] = 0xFFu;
  }
}

static bool keys_equal(const uint8_t a[34], const uint8_t b[34]) {
  unsigned i;
  for (i = 0; i < 34u; i++) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

static unsigned count_key(const chess_game *game, const uint8_t key[34]) {
  unsigned found = 0;
  unsigned i;
  for (i = 0; i < game->history_len; i++) {
    if (keys_equal(game->keys[i], key)) {
      found++;
    }
  }
  return found;
}

chess_error chess_game_init(chess_game *game, const chess_position *pos) {
  uint8_t key[34];
  unsigned i;
  if (game == NULL || pos == NULL) {
    return CHESS_ERR_NULL;
  }
  if (chess_position_validate(pos) != CHESS_OK) {
    return CHESS_ERR_BAD_FEN;
  }
  game->position = *pos;
  chess_position_key(&game->position, key);
  for (i = 0; i < 34u; i++) {
    game->keys[0][i] = key[i];
  }
  game->history_len = 1;
  game->initialized = true;
  game->decided = CHESS_STATUS_ONGOING;
  return CHESS_OK;
}

chess_error chess_game_init_fen(chess_game *game, const char *fen) {
  chess_position pos;
  chess_error err;
  if (game == NULL || fen == NULL) {
    return CHESS_ERR_NULL;
  }
  err = chess_position_from_fen(&pos, fen);
  if (err != CHESS_OK) {
    return err;
  }
  return chess_game_init(game, &pos);
}

/* M1 dead-position whitelist: only bare kings, a lone minor, or bishops
 * confined to one square color. Anything richer (or opposite-color
 * bishops, or two knights) is left ongoing: complex dead-end pawn
 * structures stay a documented limitation, never a false draw. */
static bool is_dead(const chess_position *pos) {
  unsigned knights = 0;
  unsigned bishops = 0;
  int bishop_parity = -1;
  unsigned sq;

  for (sq = 0; sq < 64u; sq++) {
    chess_piece p = pos->board[sq];
    if (p.type == CHESS_PAWN || p.type == CHESS_ROOK || p.type == CHESS_QUEEN) {
      return false;
    }
    if (p.type == CHESS_KNIGHT) {
      knights++;
    }
    if (p.type == CHESS_BISHOP) {
      int parity = (int)((sq % 8u) + (sq / 8u)) & 1;
      if (bishop_parity < 0) {
        bishop_parity = parity;
      } else if (bishop_parity != parity) {
        return false;
      }
      bishops++;
    }
  }
  if (knights >= 2u) {
    return false;
  }
  if (knights == 1u && bishops > 0u) {
    return false;
  }
  return true;
}

chess_status chess_game_status(const chess_game *game) {
  chess_position pos;
  chess_move moves[CHESS_MAX_MOVES];
  size_t count = 0;
  uint8_t key[34];
  bool in_check;

  if (game == NULL || !game->initialized) {
    return CHESS_STATUS_INVALID;
  }
  if (game->decided != CHESS_STATUS_ONGOING) {
    return game->decided;
  }
  pos = game->position;
  if (chess_generate_legal(&pos, moves, CHESS_MAX_MOVES, &count) != CHESS_OK) {
    return CHESS_STATUS_INVALID;
  }
  in_check = chess_is_attacked(
      &pos, pos.side_to_move == CHESS_WHITE ? pos.white_king : pos.black_king,
      (chess_color)(pos.side_to_move ^ 1u));
  if (count == 0) {
    if (!in_check) {
      return CHESS_STATUS_STALEMATE;
    }
    return pos.side_to_move == CHESS_WHITE ? CHESS_STATUS_CHECKMATE_BLACK_WINS
                                           : CHESS_STATUS_CHECKMATE_WHITE_WINS;
  }
  if (is_dead(&pos)) {
    return CHESS_STATUS_DRAW_DEAD;
  }
  chess_position_key(&pos, key);
  if (count_key(game, key) >= 5u) {
    return CHESS_STATUS_DRAW_FIVEFOLD;
  }
  if (pos.halfmove_clock >= 150u) {
    return CHESS_STATUS_DRAW_SEVENTY_FIVE;
  }
  return CHESS_STATUS_ONGOING;
}

chess_error chess_game_apply(chess_game *game, chess_move move) {
  chess_undo undo;
  chess_error err;
  uint8_t key[34];
  unsigned i;
  bool irreversible;

  if (game == NULL || !game->initialized) {
    return CHESS_ERR_NULL;
  }
  if (chess_game_status(game) != CHESS_STATUS_ONGOING) {
    return CHESS_ERR_GAME_OVER;
  }
  /* A full window with an ongoing game is unreachable (150 reversible
   * halfmoves end it first); refuse rather than discard history. */
  if (game->history_len >= CHESS_HISTORY_MAX) {
    return CHESS_ERR_GAME_OVER;
  }
  err = chess_make(&game->position, move, &undo);
  if (err != CHESS_OK) {
    return err;
  }
  irreversible = undo.moved.type == CHESS_PAWN ||
                 undo.captured.type != CHESS_EMPTY ||
                 undo.castling != game->position.castling;
  if (irreversible) {
    game->history_len = 0;
  }
  chess_position_key(&game->position, key);
  for (i = 0; i < 34u; i++) {
    game->keys[game->history_len][i] = key[i];
  }
  game->history_len++;
  return CHESS_OK;
}

chess_error chess_game_claim_draw(chess_game *game,
                                  const chess_move *intended) {
  chess_position after;
  chess_undo undo;
  chess_error err;
  uint8_t key[34];

  if (game == NULL || !game->initialized) {
    return CHESS_ERR_NULL;
  }
  if (chess_game_status(game) != CHESS_STATUS_ONGOING) {
    return CHESS_ERR_GAME_OVER;
  }
  if (intended == NULL) {
    chess_position_key(&game->position, key);
    if (count_key(game, key) >= 3u) {
      game->decided = CHESS_STATUS_DRAW_CLAIMED_THREEFOLD;
      return CHESS_OK;
    }
    if (game->position.halfmove_clock >= 100u) {
      game->decided = CHESS_STATUS_DRAW_CLAIMED_FIFTY;
      return CHESS_OK;
    }
    return CHESS_ERR_NO_CLAIM;
  }
  after = game->position;
  err = chess_make(&after, *intended, &undo);
  if (err != CHESS_OK) {
    return err;
  }
  chess_position_key(&after, key);
  if (count_key(game, key) >= 2u) {
    game->decided = CHESS_STATUS_DRAW_CLAIMED_THREEFOLD;
    return CHESS_OK;
  }
  if (after.halfmove_clock >= 100u) {
    game->decided = CHESS_STATUS_DRAW_CLAIMED_FIFTY;
    return CHESS_OK;
  }
  return CHESS_ERR_NO_CLAIM;
}

chess_error chess_game_agree_draw(chess_game *game) {
  if (game == NULL || !game->initialized) {
    return CHESS_ERR_NULL;
  }
  if (chess_game_status(game) != CHESS_STATUS_ONGOING) {
    return CHESS_ERR_GAME_OVER;
  }
  game->decided = CHESS_STATUS_DRAW_AGREED;
  return CHESS_OK;
}

chess_error chess_game_resign(chess_game *game, chess_color color) {
  if (game == NULL || !game->initialized) {
    return CHESS_ERR_NULL;
  }
  if (color != CHESS_WHITE && color != CHESS_BLACK) {
    return CHESS_ERR_ILLEGAL_MOVE;
  }
  if (chess_game_status(game) != CHESS_STATUS_ONGOING) {
    return CHESS_ERR_GAME_OVER;
  }
  game->decided = color == CHESS_WHITE ? CHESS_STATUS_RESIGN_BLACK_WINS
                                       : CHESS_STATUS_RESIGN_WHITE_WINS;
  return CHESS_OK;
}
