/* Versioned chess saves (Task 7): explicit little-endian envelope plus
 * CRC32, A/B slots with highest-valid-sequence recovery. The codec is
 * pure C11 and host-runnable; the NVS adapter talks to an injected
 * backend so host tests use a fake and firmware plugs the real NVS.
 *
 * Wire schema v1 (fixed, fixture-locked):
 *   envelope 24B: magic "CHS1", schema u16=1, header_len u16=24,
 *     payload_len u32, seq u64, payload CRC32 (IEEE, see chess_crc32).
 *   payload: board32[32] nibbles (a1 first, empty 0, white 1-6,
 *     black 7-12), side u8, rights u8, raw ep u8 (square or 64),
 *     pad u8, halfmove u16, fullmove u32, mode u8, human color u8,
 *     result u8 (chess_status), pad u8, history_count u16,
 *     history count*34B normalized keys, settings u8[4]
 *     (difficulty, language, brightness, reserved).
 * Maximal slot: 24 + 42 + 6 + 151*34 + 4 = 5210 bytes <= 6 KiB.
 *
 * raw ep serves FEN fidelity; repetition keys always use the core's
 * normalized form: never confuse the two. Unknown settings enums fall
 * back to compatible defaults; unknown game/mode/result values fail
 * the slot instead.
 */
#ifndef CHESS_STORAGE_H
#define CHESS_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chess_core.h"

#define CHESS_SAVE_SCHEMA_V1 1u
#define CHESS_SAVE_SLOT_MAX 6144u

#define CHESS_MODE_LOCAL 0u
#define CHESS_MODE_AI 1u

#define CHESS_DIFF_EASY 0u
#define CHESS_DIFF_MEDIUM 1u
#define CHESS_DIFF_HARD 2u
#define CHESS_BRIGHTNESS_MAX 100u

typedef struct chess_settings {
  uint8_t difficulty; /* 0 easy, 1 medium, 2 hard */
  uint8_t language;   /* 0 default; kept opaque for forward compat */
  uint8_t brightness; /* 0..100, clamped on load */
  uint8_t reserved;
} chess_settings;

typedef struct chess_save {
  chess_game game;
  chess_settings settings;
  uint8_t mode;        /* CHESS_MODE_* */
  uint8_t human_color; /* chess_color */
  uint64_t seq;
} chess_save;

/* IEEE CRC32 (polynomial 0xEDB88320); "123456789" -> 0xCBF43926. */
uint32_t chess_crc32(const uint8_t *data, size_t len);

/* Encode one slot image. Failures leave `out` untouched. */
chess_error chess_save_encode(const chess_save *save, uint8_t *out,
                              size_t cap, size_t *len);

/* Decode and fully validate: envelope, CRC, position legality (never
 * proven by CRC alone), last-history-equals-current, result consistency.
 * Unknown schema -> CHESS_ERR_UNKNOWN_VERSION; anything else broken ->
 * CHESS_ERR_CORRUPT. Failures leave `save` untouched. */
chess_error chess_save_decode(const uint8_t *buf, size_t len,
                              chess_save *save);

/* Slot backend (0 = A, 1 = B). read returns CHESS_ERR_NO_SAVE for an
 * empty slot. write stages bytes; commit persists them: the commit
 * boundary is the simulated power-loss point. No erase API exists on
 * purpose: recovery must never wipe the project namespace. */
typedef struct chess_nvs_backend {
  chess_error (*read)(void *ctx, int slot, uint8_t *out, size_t cap,
                      size_t *len);
  chess_error (*write)(void *ctx, int slot, const uint8_t *data, size_t len);
  chess_error (*commit)(void *ctx);
  void *ctx;
} chess_nvs_backend;

/* Load the highest-sequence valid save. A newer unknown-version slot
 * blocks strict loads (CHESS_ERR_UNKNOWN_VERSION: prompt, let the user
 * pick the older save explicitly); load_compat skips such slots.
 * Present-but-broken data -> CHESS_ERR_CORRUPT; nothing stored ->
 * CHESS_ERR_NO_SAVE. */
chess_error chess_save_load(chess_nvs_backend *be, chess_save *save);
chess_error chess_save_load_compat(chess_nvs_backend *be, chess_save *save);

/* Persist a save to the older/invalid slot with an assigned sequence
 * (requested seq wins when ahead of best+1, else best+1; 0 means auto),
 * then commit and read back for verification. Only a verified slot
 * reports success. Refuses to overwrite newer unknown-version slots
 * and to wrap the u64 sequence. */
chess_error chess_save_store(chess_nvs_backend *be, const chess_save *save);

#endif /* CHESS_STORAGE_H */
