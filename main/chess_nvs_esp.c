/* NVS-backed storage backend (firmware): namespace chess_v1, blobs
 * save_a/save_b. Failures surface as errors; this layer never erases.
 */
#include "chess_storage.h"

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "chess-store";
static const char *NS = "chess_v1";

static nvs_handle_t s_handle;
static bool s_open;

static esp_err_t ensure_open(void) {
    esp_err_t err;
    if (s_open) {
        return ESP_OK;
    }
    err = nvs_open(NS, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open chess_v1 失败: %s", esp_err_to_name(err));
        return err;
    }
    s_open = true;
    return ESP_OK;
}

static const char *slot_key(int slot) {
    return slot == 0 ? "save_a" : "save_b";
}

static chess_error esp_read(void *ctx, int slot, uint8_t *out, size_t cap,
                            size_t *len) {
    size_t need = 0;
    esp_err_t err;
    (void)ctx;
    if (slot < 0 || slot > 1 || out == NULL || len == NULL) {
        return CHESS_ERR_NULL;
    }
    if (ensure_open() != ESP_OK) {
        return CHESS_ERR_CORRUPT;
    }
    err = nvs_get_blob(s_handle, slot_key(slot), NULL, &need);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return CHESS_ERR_NO_SAVE;
    }
    if (err != ESP_OK || need == 0 || need > CHESS_SAVE_SLOT_MAX) {
        return CHESS_ERR_CORRUPT;
    }
    if (cap < need) {
        return CHESS_ERR_BUFFER_TOO_SMALL;
    }
    err = nvs_get_blob(s_handle, slot_key(slot), out, &need);
    if (err != ESP_OK) {
        return CHESS_ERR_CORRUPT;
    }
    *len = need;
    return CHESS_OK;
}

static chess_error esp_write(void *ctx, int slot, const uint8_t *data,
                             size_t len) {
    (void)ctx;
    if (slot < 0 || slot > 1 || (len > 0 && data == NULL)) {
        return CHESS_ERR_NULL;
    }
    if (len > CHESS_SAVE_SLOT_MAX) {
        return CHESS_ERR_BUFFER_TOO_SMALL;
    }
    if (ensure_open() != ESP_OK) {
        return CHESS_ERR_CORRUPT;
    }
    if (nvs_set_blob(s_handle, slot_key(slot), data, len) != ESP_OK) {
        return CHESS_ERR_CORRUPT;
    }
    return CHESS_OK;
}

static chess_error esp_commit(void *ctx) {
    (void)ctx;
    if (ensure_open() != ESP_OK) {
        return CHESS_ERR_CORRUPT;
    }
    if (nvs_commit(s_handle) != ESP_OK) {
        return CHESS_ERR_CORRUPT;
    }
    return CHESS_OK;
}

chess_nvs_backend chess_nvs_esp_backend(void) {
    chess_nvs_backend be;
    be.read = esp_read;
    be.write = esp_write;
    be.commit = esp_commit;
    be.ctx = NULL;
    return be;
}
