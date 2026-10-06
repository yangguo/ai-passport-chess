/* Task 7 RED: storage codec + NVS adapter with a fake backend.
 * Expected to FAIL to compile: chess_storage.h does not exist yet. */
#include <stdio.h>
#include <string.h>

#include "chess_core.h"
#include "chess_storage.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      failures++;                                                            \
    }                                                                        \
  } while (0)

#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

/* ---- fake NVS backend: staged write + explicit commit point ---- */

typedef struct fake_nvs {
  uint8_t slots[2][CHESS_SAVE_SLOT_MAX];
  size_t lens[2];
  bool occupied[2];
  uint8_t staged[CHESS_SAVE_SLOT_MAX];
  size_t staged_len;
  int staged_slot; /* -1 when nothing staged */
  bool drop_commit; /* power loss between write and commit */
  int read_error_slot; /* -1 none, else forced I/O error */
} fake_nvs;

static chess_error fake_read(void *ctx, int slot, uint8_t *out, size_t cap,
                             size_t *len) {
  fake_nvs *be = (fake_nvs *)ctx;
  if (slot < 0 || slot > 1 || out == NULL || len == NULL) {
    return CHESS_ERR_NULL;
  }
  if (slot == be->read_error_slot) {
    return CHESS_ERR_CORRUPT; /* simulated I/O fault */
  }
  if (!be->occupied[slot]) {
    return CHESS_ERR_NO_SAVE;
  }
  if (cap < be->lens[slot]) {
    return CHESS_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(out, be->slots[slot], be->lens[slot]);
  *len = be->lens[slot];
  return CHESS_OK;
}

static chess_error fake_write(void *ctx, int slot, const uint8_t *data,
                              size_t len) {
  fake_nvs *be = (fake_nvs *)ctx;
  if (slot < 0 || slot > 1 || (len > 0 && data == NULL)) {
    return CHESS_ERR_NULL;
  }
  if (len > CHESS_SAVE_SLOT_MAX) {
    return CHESS_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(be->staged, data, len);
  be->staged_len = len;
  be->staged_slot = slot;
  return CHESS_OK;
}

static chess_error fake_commit(void *ctx) {
  fake_nvs *be = (fake_nvs *)ctx;
  if (be->drop_commit) {
    be->staged_slot = -1; /* staged bytes never reach the slot */
    return CHESS_OK;
  }
  if (be->staged_slot < 0) {
    return CHESS_OK;
  }
  memcpy(be->slots[be->staged_slot], be->staged, be->staged_len);
  be->lens[be->staged_slot] = be->staged_len;
  be->occupied[be->staged_slot] = true;
  be->staged_slot = -1;
  return CHESS_OK;
}

static void fake_init(fake_nvs *be) {
  memset(be, 0, sizeof(*be));
  be->staged_slot = -1;
  be->read_error_slot = -1;
}

static chess_nvs_backend as_backend(fake_nvs *be) {
  chess_nvs_backend out;
  out.read = fake_read;
  out.write = fake_write;
  out.commit = fake_commit;
  out.ctx = be;
  return out;
}

static void default_save(chess_save *s) {
  memset(s, 0, sizeof(*s));
  CHECK(chess_game_init_fen(&s->game, START_FEN) == CHESS_OK);
  s->settings.difficulty = CHESS_DIFF_MEDIUM;
  s->settings.language = 1;
  s->settings.brightness = 80;
  s->mode = CHESS_MODE_LOCAL;
  s->human_color = CHESS_WHITE;
  s->seq = 7;
}

/* Semantic equality: bytes past history_len are stale window remains,
 * never encoded and never scanned, so they are not state. */
static bool saves_equal(const chess_save *a, const chess_save *b) {
  unsigned i;
  if (memcmp(&a->game.position, &b->game.position, sizeof(chess_position)) !=
      0) {
    return false;
  }
  if (a->game.history_len != b->game.history_len ||
      a->game.initialized != b->game.initialized ||
      a->game.decided != b->game.decided) {
    return false;
  }
  for (i = 0; i < a->game.history_len; i++) {
    if (memcmp(a->game.keys[i], b->game.keys[i], 34) != 0) {
      return false;
    }
  }
  if (memcmp(&a->settings, &b->settings, sizeof(chess_settings)) != 0) {
    return false;
  }
  return a->mode == b->mode && a->human_color == b->human_color &&
         a->seq == b->seq;
}

static void test_crc_vector(void) {
  /* Standard IEEE CRC32 check value. */
  static const uint8_t kMsg[] = "123456789";
  CHECK(chess_crc32(kMsg, 9) == 0xCBF43926u);
  CHECK(chess_crc32(kMsg, 0) == 0u);
}

static void test_golden_envelope(void) {
  chess_save s;
  uint8_t buf[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  default_save(&s);
  CHECK(chess_save_encode(&s, buf, sizeof(buf), &len) == CHESS_OK);
  CHECK(len > 24 && len <= CHESS_SAVE_SLOT_MAX);
  CHECK(buf[0] == 'C' && buf[1] == 'H' && buf[2] == 'S' && buf[3] == '1');
  CHECK(buf[4] == 1 && buf[5] == 0); /* schema v1 LE */
  CHECK(buf[6] == 24 && buf[7] == 0); /* header_len */
  {
    uint32_t payload = (uint32_t)(buf[8] | ((uint32_t)buf[9] << 8) |
                                  ((uint32_t)buf[10] << 16) |
                                  ((uint32_t)buf[11] << 24));
    CHECK(payload == len - 24);
  }
  CHECK(buf[12] == 7); /* seq 7 LE, rest zero */
  CHECK(buf[13] == 0 && buf[19] == 0);
}

static void test_round_trip(void) {
  chess_save s;
  chess_save back;
  uint8_t buf[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  default_save(&s);
  CHECK(chess_game_apply(&s.game, (chess_move){12, 28, CHESS_EMPTY}) ==
        CHESS_OK); /* e2e4 */
  CHECK(chess_save_encode(&s, buf, sizeof(buf), &len) == CHESS_OK);
  memset(&back, 0, sizeof(back));
  CHECK(chess_save_decode(buf, len, &back) == CHESS_OK);
  CHECK(back.seq == 7);
  CHECK(saves_equal(&s, &back));
}

static void test_rich_history_round_trip(void) {
  /* Repetition window, castling rights, ep square, counters, decision. */
  chess_save s;
  chess_save back;
  uint8_t buf[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  static const char *kMoves[] = {"g1f3", "g8f6", "f3g1", "f6g8",
                                 "e2e4", "e7e5", "g1f3"};
  size_t i;
  default_save(&s);
  for (i = 0; i < sizeof(kMoves) / sizeof(kMoves[0]); i++) {
    uint8_t f = (uint8_t)(kMoves[i][0] - 'a' + (kMoves[i][1] - '1') * 8);
    uint8_t t = (uint8_t)(kMoves[i][2] - 'a' + (kMoves[i][3] - '1') * 8);
    CHECK(chess_game_apply(&s.game, (chess_move){f, t, CHESS_EMPTY}) ==
          CHESS_OK);
  }
  CHECK(chess_game_resign(&s.game, CHESS_BLACK) == CHESS_OK);
  s.seq = 42;
  CHECK(chess_save_encode(&s, buf, sizeof(buf), &len) == CHESS_OK);
  memset(&back, 0, sizeof(back));
  CHECK(chess_save_decode(buf, len, &back) == CHESS_OK);
  CHECK(saves_equal(&s, &back));
  CHECK(chess_game_status(&back.game) == CHESS_STATUS_RESIGN_WHITE_WINS);
}

static void test_max_slot_size(void) {
  chess_save s;
  uint8_t buf[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  uint8_t key[34];
  unsigned i;
  default_save(&s);
  /* Structurally maximal window: every entry present. */
  chess_position_key(&s.game.position, key);
  for (i = 0; i < CHESS_HISTORY_MAX; i++) {
    memcpy(s.game.keys[i], key, sizeof(key));
  }
  s.game.history_len = CHESS_HISTORY_MAX;
  CHECK(chess_save_encode(&s, buf, sizeof(buf), &len) == CHESS_OK);
  CHECK(len <= CHESS_SAVE_SLOT_MAX);
}

static void test_unknown_version(void) {
  chess_save s;
  uint8_t buf[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  chess_save back;
  default_save(&s);
  CHECK(chess_save_encode(&s, buf, sizeof(buf), &len) == CHESS_OK);
  buf[4] = 2; /* schema v2, same bytes otherwise */
  CHECK(chess_save_decode(buf, len, &back) == CHESS_ERR_UNKNOWN_VERSION);
}

static void test_truncated(void) {
  chess_save s;
  uint8_t buf[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  size_t cuts[] = {0, 3, 10, 23, 24, 40};
  size_t i;
  chess_save back;
  default_save(&s);
  CHECK(chess_save_encode(&s, buf, sizeof(buf), &len) == CHESS_OK);
  for (i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
    if (cuts[i] > len) {
      continue;
    }
    CHECK(chess_save_decode(buf, cuts[i], &back) == CHESS_ERR_CORRUPT);
  }
}

static void test_bitflips_rejected(void) {
  chess_save s;
  uint8_t buf[CHESS_SAVE_SLOT_MAX];
  uint8_t bad[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  chess_save back;
  /* Offsets that must always fail: magic, schema, CRC, a board nibble
   * (illegal piece code), the mode byte (out of range). */
  size_t spots[] = {0, 4, 20, 24, 24 + 41 + 1};
  size_t i;
  default_save(&s);
  CHECK(chess_game_apply(&s.game, (chess_move){12, 28, CHESS_EMPTY}) ==
        CHESS_OK);
  CHECK(chess_save_encode(&s, buf, sizeof(buf), &len) == CHESS_OK);
  for (i = 0; i < sizeof(spots) / sizeof(spots[0]); i++) {
    memcpy(bad, buf, len);
    bad[spots[i]] ^= 0xFFu;
    if (spots[i] == 4) {
      CHECK(chess_save_decode(bad, len, &back) == CHESS_ERR_UNKNOWN_VERSION);
    } else {
      if (chess_save_decode(bad, len, &back) != CHESS_ERR_CORRUPT) {
        printf("FAIL %s:%d: bitflip at %zu not rejected\n", __FILE__,
               __LINE__, spots[i]);
        failures++;
      }
    }
  }
}

static void test_settings_compat_defaults(void) {
  chess_save s;
  uint8_t buf[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  chess_save back;
  default_save(&s);
  CHECK(chess_save_encode(&s, buf, sizeof(buf), &len) == CHESS_OK);
  /* Difficulty enum runs 0..2; 9 must fall back to medium, and an
   * overbright value clamps instead of failing the slot. */
  buf[len - 4] = 9;
  buf[len - 2] = 250;
  /* Patched payload needs a fresh CRC (bitflip tests rely on the opposite). */
  {
    uint32_t crc = chess_crc32(buf + 24, len - 24);
    buf[20] = (uint8_t)(crc & 0xFFu);
    buf[21] = (uint8_t)((crc >> 8) & 0xFFu);
    buf[22] = (uint8_t)((crc >> 16) & 0xFFu);
    buf[23] = (uint8_t)((crc >> 24) & 0xFFu);
  }
  CHECK(chess_save_decode(buf, len, &back) == CHESS_OK);
  CHECK(back.settings.difficulty == CHESS_DIFF_MEDIUM);
  CHECK(back.settings.brightness == 100);
}

static void test_ab_recovery_and_seq(void) {
  fake_nvs raw;
  chess_nvs_backend be;
  chess_save loaded;
  fake_init(&raw);
  be = as_backend(&raw);

  /* Empty backend: nothing to recover. */
  CHECK(chess_save_load(&be, &loaded) == CHESS_ERR_NO_SAVE);

  /* Store twice: second save must land in the other slot with seq+1. */
  {
    chess_save s;
    default_save(&s);
    CHECK(chess_save_store(&be, &s) == CHESS_OK);
    CHECK(chess_save_load(&be, &loaded) == CHESS_OK);
    CHECK(loaded.seq == 7 && saves_equal(&s, &loaded));
    s.seq = 0; /* ignored: adapter assigns best+1 */
    CHECK(chess_game_apply(&s.game, (chess_move){12, 28, CHESS_EMPTY}) ==
          CHESS_OK);
    CHECK(chess_save_store(&be, &s) == CHESS_OK);
    CHECK(chess_save_load(&be, &loaded) == CHESS_OK);
    CHECK(loaded.seq == 8);
    CHECK(loaded.game.position.board[28].type == CHESS_PAWN);
  }

  /* Corrupt the newest slot: fall back to the older committed save. */
  raw.slots[1][30] ^= 0xFFu;
  CHECK(chess_save_load(&be, &loaded) == CHESS_OK);
  CHECK(loaded.seq == 7);

  /* Both slots corrupt: repair prompt, never silent garbage. */
  raw.slots[0][10] ^= 0xFFu;
  CHECK(chess_save_load(&be, &loaded) == CHESS_ERR_CORRUPT);
}

static void test_commit_loss_keeps_old_save(void) {
  fake_nvs raw;
  chess_nvs_backend be;
  chess_save loaded;
  chess_save s;
  fake_init(&raw);
  be = as_backend(&raw);
  default_save(&s);
  CHECK(chess_save_store(&be, &s) == CHESS_OK);

  /* Power dies between write and commit: the old slot must survive. */
  raw.drop_commit = true;
  s.seq = 0;
  CHECK(chess_game_apply(&s.game, (chess_move){12, 28, CHESS_EMPTY}) ==
        CHESS_OK);
  CHECK(chess_save_store(&be, &s) == CHESS_ERR_CORRUPT);
  raw.drop_commit = false;
  CHECK(chess_save_load(&be, &loaded) == CHESS_OK);
  CHECK(loaded.seq == 7);
  CHECK(loaded.game.position.board[28].type == CHESS_EMPTY);
}

static void test_unknown_newer_strict_vs_compat(void) {
  fake_nvs raw;
  chess_nvs_backend be;
  chess_save loaded;
  chess_save s;
  uint8_t blob[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  fake_init(&raw);
  be = as_backend(&raw);
  default_save(&s);
  CHECK(chess_save_store(&be, &s) == CHESS_OK);
  /* A newer-but-future slot blocks strict loads, not compat ones. */
  CHECK(chess_save_encode(&s, blob, sizeof(blob), &len) == CHESS_OK);
  blob[4] = 9;
  blob[12] = 200; /* higher seq */
  CHECK(fake_write(&raw, 1, blob, len) == CHESS_OK);
  CHECK(fake_commit(&raw) == CHESS_OK);
  CHECK(chess_save_load(&be, &loaded) == CHESS_ERR_UNKNOWN_VERSION);
  CHECK(chess_save_load_compat(&be, &loaded) == CHESS_OK);
  CHECK(loaded.seq == 7);
}

static void test_cross_restart_repetition(void) {
  fake_nvs raw;
  chess_nvs_backend be;
  chess_save s;
  chess_save loaded;
  static const char *kFirst[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
  static const char *kSecond[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
  size_t i;
  fake_init(&raw);
  be = as_backend(&raw);
  default_save(&s);
  for (i = 0; i < 4; i++) {
    uint8_t f = (uint8_t)(kFirst[i][0] - 'a' + (kFirst[i][1] - '1') * 8);
    uint8_t t = (uint8_t)(kFirst[i][2] - 'a' + (kFirst[i][3] - '1') * 8);
    CHECK(chess_game_apply(&s.game, (chess_move){f, t, CHESS_EMPTY}) ==
          CHESS_OK);
  }
  CHECK(chess_save_store(&be, &s) == CHESS_OK);
  /* "Restart": decode into a fresh struct, keep playing the dance. */
  CHECK(chess_save_load(&be, &loaded) == CHESS_OK);
  for (i = 0; i < 4; i++) {
    uint8_t f = (uint8_t)(kSecond[i][0] - 'a' + (kSecond[i][1] - '1') * 8);
    uint8_t t = (uint8_t)(kSecond[i][2] - 'a' + (kSecond[i][3] - '1') * 8);
    CHECK(chess_game_apply(&loaded.game, (chess_move){f, t, CHESS_EMPTY}) ==
          CHESS_OK);
  }
  CHECK(chess_game_claim_draw(&loaded.game, NULL) == CHESS_OK);
  CHECK(chess_game_status(&loaded.game) ==
        CHESS_STATUS_DRAW_CLAIMED_THREEFOLD);
}

static void test_null_args(void) {
  chess_save s;
  uint8_t buf[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  default_save(&s);
  CHECK(chess_save_encode(NULL, buf, sizeof(buf), &len) == CHESS_ERR_NULL);
  CHECK(chess_save_encode(&s, NULL, sizeof(buf), &len) == CHESS_ERR_NULL);
  CHECK(chess_save_encode(&s, buf, sizeof(buf), NULL) == CHESS_ERR_NULL);
  CHECK(chess_save_decode(NULL, 10, &s) == CHESS_ERR_NULL);
  CHECK(chess_save_decode(buf, 10, NULL) == CHESS_ERR_NULL);
  CHECK(chess_save_load(NULL, &s) == CHESS_ERR_NULL);
  CHECK(chess_save_store(NULL, &s) == CHESS_ERR_NULL);
}

int main(void) {
  test_crc_vector();
  test_golden_envelope();
  test_round_trip();
  test_rich_history_round_trip();
  test_max_slot_size();
  test_unknown_version();
  test_truncated();
  test_bitflips_rejected();
  test_settings_compat_defaults();
  test_ab_recovery_and_seq();
  test_commit_loss_keeps_old_save();
  test_unknown_newer_strict_vs_compat();
  test_cross_restart_repetition();
  test_null_args();

  if (failures == 0) {
    printf("PASS: all test_codec checks passed\n");
    return 0;
  }
  printf("FAILURES: %d\n", failures);
  return 1;
}
