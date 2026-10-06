/* A/B slot recovery (Task 7): highest valid sequence wins, unknown
 * newer versions are surfaced (strict) or skipped (compat), and stores
 * verify by read-back. Nothing here ever erases: there is no erase
 * path to misuse. Stack note: encode/read-back buffers are ~6 KiB;
 * the firmware caller must size the storage worker stack accordingly. */
#include <string.h>

#include "chess_core.h"
#include "chess_storage.h"

typedef struct slot_view {
  bool present;      /* bytes were readable */
  bool decodes;      /* full schema + CRC + consistency */
  bool unknown;      /* newer schema (seq still trustworthy) */
  uint64_t seq;
  chess_save save;   /* valid only when decodes */
} slot_view;

static bool parse_envelope_seq(const uint8_t *buf, size_t len,
                               uint64_t *seq) {
  uint32_t payload_len;
  if (len < 24u) {
    return false;
  }
  if (buf[0] != 'C' || buf[1] != 'H' || buf[2] != 'S' || buf[3] != '1') {
    return false;
  }
  payload_len = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
                ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
  if (payload_len != len - 24u) {
    return false;
  }
  *seq = 0;
  {
    unsigned i;
    for (i = 0; i < 8u; i++) {
      *seq |= (uint64_t)buf[12 + i] << (8u * i);
    }
  }
  return true;
}

static chess_error scan_slot(chess_nvs_backend *be, int slot,
                             slot_view *view) {
  static uint8_t raw[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  chess_error err;

  view->present = false;
  view->decodes = false;
  view->unknown = false;
  view->seq = 0;
  memset(&view->save, 0, sizeof(view->save)); /* deterministic padding */
  err = be->read(be->ctx, slot, raw, sizeof(raw), &len);
  if (err == CHESS_ERR_NO_SAVE) {
    return CHESS_OK; /* empty slot: a valid scan outcome */
  }
  if (err != CHESS_OK) {
    return err; /* I/O faults propagate, never masked as "empty" */
  }
  view->present = true;
  {
    chess_save save;
    err = chess_save_decode(raw, len, &save);
    if (err == CHESS_OK) {
      view->decodes = true;
      view->seq = save.seq;
      view->save = save;
      return CHESS_OK;
    }
    if (err == CHESS_ERR_UNKNOWN_VERSION &&
        parse_envelope_seq(raw, len, &view->seq)) {
      view->unknown = true;
      return CHESS_OK;
    }
    return CHESS_OK; /* present but broken: caller decides the signal */
  }
}

static chess_error scan_both(chess_nvs_backend *be, slot_view views[2]) {
  chess_error err;
  err = scan_slot(be, 0, &views[0]);
  if (err != CHESS_OK) {
    return err;
  }
  return scan_slot(be, 1, &views[1]);
}

static chess_error load_impl(chess_nvs_backend *be, chess_save *save,
                             bool compat) {
  slot_view views[2];
  chess_error err;
  int best = -1;
  bool saw_unknown = false;
  bool saw_broken = false;
  int s;

  if (be == NULL || save == NULL) {
    return CHESS_ERR_NULL;
  }
  err = scan_both(be, views);
  if (err != CHESS_OK) {
    return err;
  }
  for (s = 0; s < 2; s++) {
    if (views[s].decodes &&
        (best < 0 || views[s].seq > views[best].seq)) {
      best = (int)s;
    }
    if (views[s].unknown) {
      saw_unknown = true;
    } else if (views[s].present && !views[s].decodes) {
      saw_broken = true;
    }
  }
  if (best >= 0) {
    /* A newer unknown slot means the valid save may be stale: strict
     * loads stop for an explicit user choice, compat loads proceed. */
    if (saw_unknown && !compat) {
      unsigned other = (unsigned)(best == 0 ? 1 : 0);
      if (views[other].unknown && views[other].seq > views[best].seq) {
        return CHESS_ERR_UNKNOWN_VERSION;
      }
    }
    *save = views[best].save;
    return CHESS_OK;
  }
  if (saw_unknown && !compat) {
    return CHESS_ERR_UNKNOWN_VERSION;
  }
  if (saw_broken) {
    return CHESS_ERR_CORRUPT;
  }
  return CHESS_ERR_NO_SAVE;
}

chess_error chess_save_load(chess_nvs_backend *be, chess_save *save) {
  return load_impl(be, save, false);
}

chess_error chess_save_load_compat(chess_nvs_backend *be, chess_save *save) {
  return load_impl(be, save, true);
}

chess_error chess_save_store(chess_nvs_backend *be, const chess_save *save) {
  static uint8_t image[CHESS_SAVE_SLOT_MAX];
  static uint8_t check[CHESS_SAVE_SLOT_MAX];
  slot_view views[2];
  chess_error err;
  uint64_t best_seq = 0;
  bool have_seq = false;
  int target = -1;
  int oldest = -1;
  uint64_t seq;
  size_t len = 0;
  size_t check_len = 0;
  int s;
  chess_save staged;

  if (be == NULL || save == NULL) {
    return CHESS_ERR_NULL;
  }
  err = scan_both(be, views);
  if (err != CHESS_OK) {
    return err;
  }
  /* Newest sequence across every readable slot (unknown ones count:
   * their envelope seq is still fixed-offset). */
  for (s = 0; s < 2; s++) {
    if (views[s].present && (views[s].decodes || views[s].unknown)) {
      if (!have_seq || views[s].seq > best_seq) {
        best_seq = views[s].seq;
        have_seq = true;
      }
    }
  }
  if (have_seq && best_seq == 0xFFFFFFFFFFFFFFFFu) {
    return CHESS_ERR_NO_SEQ; /* maintenance prompt, never silent wrap */
  }
  /* Never overwrite a newer unknown-version slot; prefer empty, then
   * broken, then the older valid slot. */
  for (s = 0; s < 2; s++) {
    if (!views[s].present) {
      target = s;
      break;
    }
  }
  if (target < 0) {
    for (s = 0; s < 2; s++) {
      if (!views[s].decodes && !views[s].unknown) {
        target = s;
        break;
      }
    }
  }
  if (target < 0) {
    for (s = 0; s < 2; s++) {
      if (!views[s].unknown &&
          (oldest < 0 || views[s].seq < views[oldest].seq)) {
        oldest = s;
      }
    }
    target = oldest; /* -1 only when both slots are unknown */
  }
  if (target < 0) {
    return CHESS_ERR_UNKNOWN_VERSION;
  }

  seq = have_seq ? best_seq + 1u : 1u;
  if (save->seq > seq) {
    seq = save->seq;
  }
  staged = *save;
  staged.seq = seq;
  err = chess_save_encode(&staged, image, sizeof(image), &len);
  if (err != CHESS_OK) {
    return err;
  }
  err = be->write(be->ctx, target, image, len);
  if (err != CHESS_OK) {
    return err;
  }
  err = be->commit(be->ctx);
  if (err != CHESS_OK) {
    return err;
  }
  /* Read-back: only a verified slot reports success. */
  err = be->read(be->ctx, target, check, sizeof(check), &check_len);
  if (err != CHESS_OK || check_len != len) {
    return CHESS_ERR_CORRUPT;
  }
  {
    size_t i;
    for (i = 0; i < len; i++) {
      if (check[i] != image[i]) {
        return CHESS_ERR_CORRUPT;
      }
    }
  }
  {
    chess_save verify;
    if (chess_save_decode(check, check_len, &verify) != CHESS_OK ||
        verify.seq != seq) {
      return CHESS_ERR_CORRUPT;
    }
  }
  return CHESS_OK;
}
