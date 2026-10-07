/* Fixed-schema v1 codec: validate everything up front on encode, parse
 * defensively on decode. The CRC proves transport integrity; position
 * legality and result consistency are re-proven from the payload. */
#include <string.h>

#include "chess_core.h"
#include "chess_storage.h"

#define CHESS_ENVELOPE_LEN 24u
#define CHESS_PAYLOAD_FIXED 52u /* 48 fixed fields + 4 settings bytes */
#define CHESS_KEY_LEN 34u

typedef char chess_slot_size_check[(CHESS_ENVELOPE_LEN + 48u +
                                    CHESS_KEY_LEN * CHESS_HISTORY_MAX + 4u) <=
                                           CHESS_SAVE_SLOT_MAX
                                       ? 1
                                       : -1];

uint32_t chess_crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  size_t i;
  unsigned k;
  if (data == NULL) {
    return 0;
  }
  for (i = 0; i < len; i++) {
    crc ^= data[i];
    for (k = 0; k < 8; k++) {
      if ((crc & 1u) != 0u) {
        crc = (crc >> 1) ^ 0xEDB88320u;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

static void put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void put_u64(uint8_t *p, uint64_t v) {
  unsigned i;
  for (i = 0; i < 8u; i++) {
    p[i] = (uint8_t)((v >> (8u * i)) & 0xFFu);
  }
}

static uint16_t get_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const uint8_t *p) {
  uint64_t v = 0;
  unsigned i;
  for (i = 0; i < 8u; i++) {
    v |= (uint64_t)p[i] << (8u * i);
  }
  return v;
}

static uint8_t encode_nibble(chess_piece p) {
  if (p.type == CHESS_EMPTY) {
    return 0;
  }
  return (uint8_t)(p.type + (p.color == CHESS_BLACK ? 6 : 0));
}

chess_error chess_save_encode(const chess_save *save, uint8_t *out,
                              size_t cap, size_t *len) {
  size_t need;
  size_t at;
  unsigned i;
  uint8_t *payload;

  if (save == NULL || out == NULL || len == NULL) {
    return CHESS_ERR_NULL;
  }
  if (!save->game.initialized || save->game.history_len < 1u ||
      save->game.history_len > CHESS_HISTORY_MAX) {
    return CHESS_ERR_CORRUPT;
  }
  if (save->mode > CHESS_MODE_AI || save->human_color > CHESS_BLACK) {
    return CHESS_ERR_CORRUPT;
  }
  if (save->game.decided > CHESS_STATUS_RESIGN_BLACK_WINS ||
      save->game.decided == CHESS_STATUS_INVALID) {
    return CHESS_ERR_CORRUPT;
  }
  need = CHESS_ENVELOPE_LEN + 48u +
         CHESS_KEY_LEN * save->game.history_len + 4u;
  if (cap < need) {
    return CHESS_ERR_BUFFER_TOO_SMALL;
  }

  out[0] = 'C';
  out[1] = 'H';
  out[2] = 'S';
  out[3] = '1';
  put_u16(out + 4, CHESS_SAVE_SCHEMA_V1);
  put_u16(out + 6, CHESS_ENVELOPE_LEN);
  put_u32(out + 8, (uint32_t)(need - CHESS_ENVELOPE_LEN));
  put_u64(out + 12, save->seq);

  payload = out + CHESS_ENVELOPE_LEN;
  for (i = 0; i < 32u; i++) {
    uint8_t lo = encode_nibble(save->game.position.board[2u * i]);
    uint8_t hi = encode_nibble(save->game.position.board[2u * i + 1u]);
    payload[i] = (uint8_t)((hi << 4) | lo);
  }
  payload[32] = (uint8_t)save->game.position.side_to_move;
  payload[33] = save->game.position.castling;
  payload[34] = save->game.position.ep_square;
  payload[35] = 0;
  put_u16(payload + 36, save->game.position.halfmove_clock);
  put_u32(payload + 38, save->game.position.fullmove_number);
  payload[42] = save->mode;
  payload[43] = save->human_color;
  payload[44] = (uint8_t)save->game.decided;
  payload[45] = 0;
  put_u16(payload + 46, save->game.history_len);
  at = 48u;
  for (i = 0; i < save->game.history_len; i++) {
    unsigned k;
    for (k = 0; k < CHESS_KEY_LEN; k++) {
      payload[at + k] = save->game.keys[i][k];
    }
    at += CHESS_KEY_LEN;
  }
  payload[at] = save->settings.difficulty;
  payload[at + 1] = save->settings.language;
  payload[at + 2] = save->settings.brightness;
  payload[at + 3] = 0;

  put_u32(out + 20, chess_crc32(payload, need - CHESS_ENVELOPE_LEN));
  *len = need;
  return CHESS_OK;
}

static chess_error decode_piece(uint8_t code, chess_piece *out) {
  if (code == 0) {
    out->type = CHESS_EMPTY;
    out->color = CHESS_WHITE;
    return CHESS_OK;
  }
  if (code >= 1 && code <= 6) {
    out->type = (chess_piece_type)code;
    out->color = CHESS_WHITE;
    return CHESS_OK;
  }
  if (code >= 7 && code <= 12) {
    out->type = (chess_piece_type)(code - 6);
    out->color = CHESS_BLACK;
    return CHESS_OK;
  }
  return CHESS_ERR_CORRUPT;
}

chess_error chess_save_decode(const uint8_t *buf, size_t len,
                              chess_save *save) {
  const uint8_t *payload;
  uint32_t payload_len;
  uint16_t history_count;
  static chess_save scratch;
  chess_save *tmp = &scratch;
  chess_game *game = &scratch.game;
  unsigned sq;
  unsigned i;
  uint8_t key[34];

  memset(tmp, 0, sizeof(*tmp)); /* keep padding deterministic for memcmp */

  if (buf == NULL || save == NULL) {
    return CHESS_ERR_NULL;
  }
  if (len < CHESS_ENVELOPE_LEN) {
    return CHESS_ERR_CORRUPT;
  }
  if (buf[0] != 'C' || buf[1] != 'H' || buf[2] != 'S' || buf[3] != '1') {
    return CHESS_ERR_CORRUPT;
  }
  if (get_u16(buf + 4) != CHESS_SAVE_SCHEMA_V1) {
    return CHESS_ERR_UNKNOWN_VERSION;
  }
  if (get_u16(buf + 6) != CHESS_ENVELOPE_LEN) {
    return CHESS_ERR_CORRUPT;
  }
  payload_len = get_u32(buf + 8);
  if (payload_len != len - CHESS_ENVELOPE_LEN) {
    return CHESS_ERR_CORRUPT;
  }
  payload = buf + CHESS_ENVELOPE_LEN;
  if (get_u32(buf + 20) != chess_crc32(payload, payload_len)) {
    return CHESS_ERR_CORRUPT;
  }
  if (payload_len < 52u) {
    return CHESS_ERR_CORRUPT;
  }
  history_count = get_u16(payload + 46);
  if (history_count < 1u || history_count > CHESS_HISTORY_MAX) {
    return CHESS_ERR_CORRUPT;
  }
  if (payload_len != 48u + CHESS_KEY_LEN * history_count + 4u) {
    return CHESS_ERR_CORRUPT;
  }

  for (sq = 0; sq < 64u; sq++) {
    uint8_t byte = payload[sq / 2u];
    uint8_t code = (sq % 2u == 0) ? (uint8_t)(byte & 0x0Fu)
                                  : (uint8_t)((byte >> 4) & 0x0Fu);
    if (decode_piece(code, &game->position.board[sq]) != CHESS_OK) {
      return CHESS_ERR_CORRUPT;
    }
  }
  if (payload[32] > CHESS_BLACK || (payload[33] & (uint8_t)~0x0Fu) != 0u) {
    return CHESS_ERR_CORRUPT;
  }
  game->position.side_to_move = (chess_color)payload[32];
  game->position.castling = payload[33];
  game->position.ep_square = payload[34];
  game->position.halfmove_clock = get_u16(payload + 36);
  {
    uint32_t full = get_u32(payload + 38);
    if (full < 1u || full > 65535u) {
      return CHESS_ERR_CORRUPT;
    }
    game->position.fullmove_number = (uint16_t)full;
  }
  /* Recompute king squares; reject unreachable checks like FEN import. */
  game->position.white_king = CHESS_NO_SQUARE;
  game->position.black_king = CHESS_NO_SQUARE;
  for (sq = 0; sq < 64u; sq++) {
    if (game->position.board[sq].type == CHESS_KING) {
      if (game->position.board[sq].color == CHESS_WHITE) {
        game->position.white_king = (uint8_t)sq;
      } else {
        game->position.black_king = (uint8_t)sq;
      }
    }
  }
  if (chess_position_validate(&game->position) != CHESS_OK) {
    return CHESS_ERR_CORRUPT;
  }
  {
    bool white_checked = chess_is_attacked(&game->position,
                                           game->position.white_king,
                                           CHESS_BLACK);
    bool black_checked = chess_is_attacked(&game->position,
                                           game->position.black_king,
                                           CHESS_WHITE);
    if (white_checked && black_checked) {
      return CHESS_ERR_CORRUPT;
    }
    if (game->position.side_to_move == CHESS_WHITE && black_checked) {
      return CHESS_ERR_CORRUPT;
    }
    if (game->position.side_to_move == CHESS_BLACK && white_checked) {
      return CHESS_ERR_CORRUPT;
    }
  }

  if (payload[42] > CHESS_MODE_AI || payload[43] > CHESS_BLACK) {
    return CHESS_ERR_CORRUPT;
  }
  if (payload[44] > CHESS_STATUS_RESIGN_BLACK_WINS ||
      payload[44] == CHESS_STATUS_INVALID) {
    return CHESS_ERR_CORRUPT;
  }
  game->initialized = true;
  game->decided = (chess_status)payload[44];
  game->history_len = history_count;
  for (i = 0; i < history_count; i++) {
    unsigned k;
    for (k = 0; k < CHESS_KEY_LEN; k++) {
      game->keys[i][k] = payload[48u + CHESS_KEY_LEN * i + k];
    }
  }
  /* The newest key must describe the current normalized position. */
  chess_position_key(&game->position, key);
  for (i = 0; i < CHESS_KEY_LEN; i++) {
    if (game->keys[history_count - 1u][i] != key[i]) {
      return CHESS_ERR_CORRUPT;
    }
  }
  /* Result byte must agree with the recomputed outcome. */
  if (chess_game_status(game) != game->decided) {
    return CHESS_ERR_CORRUPT;
  }

  tmp->mode = payload[42];
  tmp->human_color = payload[43];
  tmp->seq = get_u64(buf + 12);
  if (payload[48u + CHESS_KEY_LEN * history_count] > CHESS_DIFF_HARD) {
    tmp->settings.difficulty = CHESS_DIFF_MEDIUM;
  } else {
    tmp->settings.difficulty = payload[48u + CHESS_KEY_LEN * history_count];
  }
  tmp->settings.language = payload[48u + CHESS_KEY_LEN * history_count + 1u];
  if (payload[48u + CHESS_KEY_LEN * history_count + 2u] > CHESS_BRIGHTNESS_MAX) {
    tmp->settings.brightness = CHESS_BRIGHTNESS_MAX;
  } else {
    tmp->settings.brightness =
        payload[48u + CHESS_KEY_LEN * history_count + 2u];
  }
  tmp->settings.reserved = 0;
  *save = *tmp;
  return CHESS_OK;
}
