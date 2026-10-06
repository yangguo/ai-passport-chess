/* In-process mcu-max adapter: our FEN in, bounded engine search, UCI
 * out, core validation before anything is trusted. Square codes are
 * flipped between the two boards (engine rank 0 = rank 8, ours = rank
 * 1); FEN itself crosses verbatim. */
#include "mcu-max.h"

#include "chess_ai.h"
#include "chess_core.h"

static int engine_square_to_ours(mcumax_square e, uint8_t *out) {
  unsigned file = (unsigned)(e & 15u);
  unsigned rank = (unsigned)((e >> 4) & 15u);
  if (file > 7u || rank > 7u) {
    return -1;
  }
  *out = (uint8_t)((7u - rank) * 8u + file);
  return 0;
}

int chess_ai_suggest(const chess_position *pos, uint32_t node_max,
                     unsigned depth_max, chess_move *out) {
  char fen[CHESS_FEN_MAX];
  mcumax_move reply;
  chess_move m;
  chess_position probe;
  chess_undo undo;
  unsigned last;

  if (pos == NULL || out == NULL) {
    return -1;
  }
  if (chess_position_to_fen(pos, fen, sizeof(fen)) != CHESS_OK) {
    return -1;
  }
  /* mcumax_set_fen_position resets the global engine state first,
   * so consecutive calls never leak history. No callback installed:
   * the search always ends on the node/depth budget. */
  mcumax_set_fen_position(fen);
  reply = mcumax_search_best_move(node_max, depth_max);
  if (reply.from == MCUMAX_SQUARE_INVALID ||
      reply.to == MCUMAX_SQUARE_INVALID) {
    return -1;
  }
  if (engine_square_to_ours(reply.from, &m.from) != 0 ||
      engine_square_to_ours(reply.to, &m.to) != 0) {
    return -1;
  }
  /* The engine only ever queens; anything else promoting is mapped
   * the same way and then validated like every other reply. */
  m.promotion = CHESS_EMPTY;
  last = (pos->side_to_move == CHESS_WHITE) ? 7u : 0u;
  if (pos->board[m.from].type == CHESS_PAWN && m.to / 8u == last) {
    m.promotion = CHESS_QUEEN;
  }
  probe = *pos;
  if (chess_make(&probe, m, &undo) != CHESS_OK) {
    return -1;
  }
  *out = m;
  return 0;
}
