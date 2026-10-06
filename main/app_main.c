/* Firmware entry (Task 8): NVS + display + battery + buttons, then
 * the chess application owns the event loop. Init failures land on
 * an error screen — never a global NVS erase.
 */
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "chess_app.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs_flash.h"

static const char *TAG = "chess-main";

static void show_error(const char *why) {
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "error screen without LVGL lock: %s", why);
        return;
    }
    lv_obj_clean(lv_screen_active());
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_label_set_text_fmt(label, "Boot failed\n%s", why);
    lv_obj_center(label);
    bsp_lvgl_unlock();
}

void app_main(void) {
    esp_err_t err;

    ESP_LOGI(TAG, "ai-passport-chess 启动");

    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s (不做全局擦除)", esp_err_to_name(err));
        if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
            ESP_LOGE(TAG, "显示也失败,只能看串口日志");
            return;
        }
        show_error("NVS init failed");
        return;
    }

    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败");
        return;
    }
    bsp_display_backlight(80);

    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计初始化失败,显示 --");
    }
    if (bsp_button_init(chess_app_button, NULL) != ESP_OK) {
        show_error("Button init failed");
        return;
    }

    chess_app_start();
}
