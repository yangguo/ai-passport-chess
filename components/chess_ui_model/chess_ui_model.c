/* Three-button reducer: state transitions only, no I/O, no core writes. */
#include "chess_ui_model.h"

static void out_none(chess_ui_command *out) {
  out->kind = CHESS_CMD_NONE;
  out->move.from = 0;
  out->move.to = 0;
  out->move.promotion = CHESS_EMPTY;
}

static void rebuild_pieces(chess_ui_model *m) {
  size_t i;
  m->npieces = 0;
  for (i = 0; i < m->nlegal; i++) {
    uint8_t from = m->legal[i].from;
    size_t k;
    bool seen = false;
    size_t at = m->npieces;
    for (k = 0; k < m->npieces; k++) {
      if (m->pieces[k] == from) {
        seen = true;
        break;
      }
      if (m->pieces[k] > from) {
        at = k;
        break;
      }
    }
    if (seen || m->npieces >= CHESS_MODEL_MAX_CANDIDATES) {
      continue;
    }
    for (k = m->npieces; k > at; k--) {
      m->pieces[k] = m->pieces[k - 1];
    }
    m->pieces[at] = from;
    m->npieces++;
  }
  m->piece_idx = 0;
}

static void rebuild_targets(chess_ui_model *m) {
  size_t i;
  m->ntargets = 0;
  for (i = 0; i < m->nlegal; i++) {
    uint8_t to;
    size_t k;
    bool seen = false;
    size_t at;
    if (m->legal[i].from != m->selected_piece) {
      continue;
    }
    to = m->legal[i].to;
    at = m->ntargets;
    for (k = 0; k < m->ntargets; k++) {
      if (m->targets[k] == to) {
        seen = true;
        break;
      }
      if (m->targets[k] > to) {
        at = k;
        break;
      }
    }
    if (seen || m->ntargets >= CHESS_MODEL_MAX_CANDIDATES) {
      continue;
    }
    for (k = m->ntargets; k > at; k--) {
      m->targets[k] = m->targets[k - 1];
    }
    m->targets[at] = to;
    m->ntargets++;
  }
  m->target_idx = 0;
}

static bool target_promotes(const chess_ui_model *m, uint8_t target) {
  size_t i;
  for (i = 0; i < m->nlegal; i++) {
    if (m->legal[i].from == m->selected_piece &&
        m->legal[i].to == target &&
        m->legal[i].promotion != CHESS_EMPTY) {
      return true;
    }
  }
  return false;
}

static void rebuild_pause(chess_ui_model *m) {
  m->npause = 0;
  m->pause_items[m->npause++] = CHESS_CMD_RESUME;
  m->pause_items[m->npause++] = CHESS_CMD_CLAIM_CURRENT;
  if (m->has_preselect) {
    m->pause_items[m->npause++] = CHESS_CMD_CLAIM_PRESELECT;
  }
  m->pause_items[m->npause++] = CHESS_CMD_RESIGN;
  m->pause_items[m->npause++] = CHESS_CMD_NEW_GAME;
  m->pause_items[m->npause++] = CHESS_CMD_GO_HOME;
  m->pause_idx = 0;
}

static void to_piece_screen(chess_ui_model *m) {
  m->screen = CHESS_SCREEN_SELECT_PIECE;
  m->selected_piece = CHESS_NO_SQUARE;
  m->ntargets = 0;
  m->target_idx = 0;
}

chess_error chess_ui_model_init(chess_ui_model *m) {
  size_t i;
  if (m == NULL) {
    return CHESS_ERR_NULL;
  }
  for (i = 0; i < sizeof(*m); i++) {
    ((uint8_t *)m)[i] = 0;
  }
  m->screen = CHESS_SCREEN_SELECT_PIECE;
  m->selected_piece = CHESS_NO_SQUARE;
  m->confirm_return = CHESS_SCREEN_PAUSE;
  return CHESS_OK;
}

chess_error chess_ui_model_set_moves(chess_ui_model *m,
                                     const chess_move *moves, size_t n) {
  size_t i;
  if (m == NULL) {
    return CHESS_ERR_NULL;
  }
  if (n > 0 && moves == NULL) {
    return CHESS_ERR_NULL;
  }
  if (n > CHESS_MODEL_MAX_MOVES) {
    return CHESS_ERR_BUFFER_TOO_SMALL;
  }
  for (i = 0; i < n; i++) {
    m->legal[i] = moves[i];
  }
  m->nlegal = n;
  rebuild_pieces(m);
  to_piece_screen(m);
  m->has_preselect = false;
  m->confirm_yes = false;
  m->error_code = 0;
  m->error_idx = 0;
  return CHESS_OK;
}

chess_error chess_ui_model_set_error(chess_ui_model *m, uint16_t code) {
  if (m == NULL) {
    return CHESS_ERR_NULL;
  }
  m->screen = CHESS_SCREEN_ERROR;
  m->error_code = code;
  m->error_idx = 0;
  return CHESS_OK;
}

static void on_up_down(chess_ui_model *m, chess_ui_event ev,
                       chess_ui_command *out) {
  int step = (ev == CHESS_EVT_DOWN) ? 1 : -1;
  out_none(out);
  switch (m->screen) {
  case CHESS_SCREEN_SELECT_PIECE:
    if (m->npieces == 0) {
      return;
    }
    m->has_preselect = false;
    m->piece_idx =
        (size_t)(((int)m->piece_idx + step + (int)m->npieces) %
                 (int)m->npieces);
    return;
  case CHESS_SCREEN_SELECT_TARGET:
    if (m->ntargets == 0) {
      return;
    }
    m->has_preselect = false;
    m->target_idx =
        (size_t)(((int)m->target_idx + step + (int)m->ntargets) %
                 (int)m->ntargets);
    return;
  case CHESS_SCREEN_PROMOTION:
    m->has_preselect = false;
    m->promo_idx = (size_t)(((int)m->promo_idx + step + 4) % 4);
    return;
  case CHESS_SCREEN_PAUSE:
    if (m->npause == 0) {
      return;
    }
    m->pause_idx =
        (size_t)(((int)m->pause_idx + step + (int)m->npause) %
                 (int)m->npause);
    return;
  case CHESS_SCREEN_CONFIRM:
    m->confirm_yes = !m->confirm_yes;
    return;
  case CHESS_SCREEN_ERROR:
    m->error_idx = (m->error_idx == 0) ? 1 : 0;
    return;
  }
}

static void on_ok_piece(chess_ui_model *m, chess_ui_command *out) {
  out_none(out);
  if (m->npieces == 0) {
    return;
  }
  m->selected_piece = m->pieces[m->piece_idx];
  rebuild_targets(m);
  if (m->ntargets == 0) {
    m->selected_piece = CHESS_NO_SQUARE;
    return;
  }
  m->screen = CHESS_SCREEN_SELECT_TARGET;
}

static void on_ok_target(chess_ui_model *m, chess_ui_command *out) {
  uint8_t target;
  out_none(out);
  if (m->ntargets == 0) {
    return;
  }
  target = m->targets[m->target_idx];
  if (target_promotes(m, target)) {
    m->promos[0] = CHESS_QUEEN;
    m->promos[1] = CHESS_ROOK;
    m->promos[2] = CHESS_BISHOP;
    m->promos[3] = CHESS_KNIGHT;
    m->promo_idx = 0;
    m->screen = CHESS_SCREEN_PROMOTION;
    return;
  }
  out->kind = CHESS_CMD_SUBMIT_MOVE;
  out->move.from = m->selected_piece;
  out->move.to = target;
  out->move.promotion = CHESS_EMPTY;
}

static void on_ok_promotion(chess_ui_model *m, chess_ui_command *out) {
  out_none(out);
  out->kind = CHESS_CMD_SUBMIT_MOVE;
  out->move.from = m->selected_piece;
  out->move.to = m->targets[m->target_idx];
  out->move.promotion = m->promos[m->promo_idx];
}

static void on_ok_pause(chess_ui_model *m, chess_ui_command *out) {
  chess_ui_cmd item;
  out_none(out);
  if (m->npause == 0) {
    return;
  }
  item = m->pause_items[m->pause_idx];
  switch (item) {
  case CHESS_CMD_RESUME:
    out->kind = CHESS_CMD_RESUME;
    to_piece_screen(m);
    return;
  case CHESS_CMD_CLAIM_CURRENT:
    out->kind = CHESS_CMD_CLAIM_CURRENT;
    return;
  case CHESS_CMD_CLAIM_PRESELECT:
    out->kind = CHESS_CMD_CLAIM_PRESELECT;
    out->move = m->preselect;
    m->has_preselect = false; /* one-shot */
    return;
  case CHESS_CMD_RESIGN:
  case CHESS_CMD_NEW_GAME:
    m->confirm_action = item;
    m->confirm_return = CHESS_SCREEN_PAUSE;
    m->confirm_yes = false;
    m->screen = CHESS_SCREEN_CONFIRM;
    return;
  case CHESS_CMD_GO_HOME:
    out->kind = CHESS_CMD_GO_HOME;
    to_piece_screen(m);
    return;
  default:
    return;
  }
}

static void on_ok_confirm(chess_ui_model *m, chess_ui_command *out) {
  out_none(out);
  if (m->confirm_yes) {
    out->kind = m->confirm_action;
    to_piece_screen(m);
    return;
  }
  m->screen = m->confirm_return;
}

static void on_ok_error(chess_ui_model *m, chess_ui_command *out) {
  out_none(out);
  if (m->error_idx == 0) {
    out->kind = CHESS_CMD_ERROR_RETRY;
    return;
  }
  m->confirm_action = CHESS_CMD_NEW_GAME;
  m->confirm_return = CHESS_SCREEN_ERROR;
  m->confirm_yes = false;
  m->screen = CHESS_SCREEN_CONFIRM;
}

static void on_ok(chess_ui_model *m, chess_ui_command *out) {
  switch (m->screen) {
  case CHESS_SCREEN_SELECT_PIECE:
    on_ok_piece(m, out);
    return;
  case CHESS_SCREEN_SELECT_TARGET:
    on_ok_target(m, out);
    return;
  case CHESS_SCREEN_PROMOTION:
    on_ok_promotion(m, out);
    return;
  case CHESS_SCREEN_PAUSE:
    on_ok_pause(m, out);
    return;
  case CHESS_SCREEN_CONFIRM:
    on_ok_confirm(m, out);
    return;
  case CHESS_SCREEN_ERROR:
    on_ok_error(m, out);
    return;
  }
}

static void arm_preselect(chess_ui_model *m) {
  /* Promotion screen focuses a complete move; a promotion-bound target
   * square falls back to the queen representative. */
  if (m->screen == CHESS_SCREEN_PROMOTION) {
    m->preselect.from = m->selected_piece;
    m->preselect.to = m->targets[m->target_idx];
    m->preselect.promotion = m->promos[m->promo_idx];
  } else {
    uint8_t target = m->targets[m->target_idx];
    m->preselect.from = m->selected_piece;
    m->preselect.to = target;
    m->preselect.promotion =
        target_promotes(m, target) ? CHESS_QUEEN : CHESS_EMPTY;
  }
  m->has_preselect = true;
}

static void on_long(chess_ui_model *m, chess_ui_command *out) {
  out_none(out);
  switch (m->screen) {
  case CHESS_SCREEN_PROMOTION:
    /* Back to the target square (spec table); the focused promotion
     * move stays armed as the preselect claim. */
    if (m->ntargets != 0) {
      arm_preselect(m);
    }
    m->screen = CHESS_SCREEN_SELECT_TARGET;
    return;
  case CHESS_SCREEN_SELECT_TARGET:
    if (m->ntargets != 0) {
      arm_preselect(m);
    }
    to_piece_screen(m);
    return;
  case CHESS_SCREEN_SELECT_PIECE:
    rebuild_pause(m);
    m->screen = CHESS_SCREEN_PAUSE;
    return;
  case CHESS_SCREEN_PAUSE:
    out->kind = CHESS_CMD_RESUME;
    to_piece_screen(m);
    return;
  case CHESS_SCREEN_CONFIRM:
    m->screen = m->confirm_return;
    return;
  case CHESS_SCREEN_ERROR:
    out->kind = CHESS_CMD_ERROR_BACK;
    return;
  }
}

chess_error chess_ui_model_event(chess_ui_model *m, chess_ui_event ev,
                                 chess_ui_command *out) {
  if (m == NULL || out == NULL) {
    return CHESS_ERR_NULL;
  }
  switch (ev) {
  case CHESS_EVT_UP:
  case CHESS_EVT_DOWN:
    on_up_down(m, ev, out);
    return CHESS_OK;
  case CHESS_EVT_OK:
    on_ok(m, out);
    return CHESS_OK;
  case CHESS_EVT_LONG:
    on_long(m, out);
    return CHESS_OK;
  case CHESS_EVT_RELEASE:
    out_none(out);
    return CHESS_OK;
  }
  out_none(out);
  return CHESS_OK;
}
