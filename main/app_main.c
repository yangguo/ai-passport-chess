/* Firmware entry (Task 8): NVS + display + battery + buttons, then
 * the chess application owns the event loop. Init failures land on
 * an error screen — never a global NVS erase.
 */
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "chess_app.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

#define CHESS_APP_TASK_STACK 12288
#define CHESS_APP_TASK_PRIORITY (tskIDLE_PRIORITY + 1)

static const char *TAG = "chess-main";

#ifdef CONFIG_CHESS_DIAGNOSTIC_DISPLAY
static void run_display_diagnostic(void) {
    ESP_EARLY_LOGI(TAG, "DIAG reset_reason=%d", (int)esp_reset_reason());

    esp_err_t err = nvs_flash_init();
    ESP_EARLY_LOGI(TAG, "DIAG nvs_init=%s", esp_err_to_name(err));

    err = bsp_display_init();
    ESP_EARLY_LOGI(TAG, "DIAG display_init=%s", esp_err_to_name(err));
    if (err != ESP_OK) return;

    lv_display_t *display = bsp_lvgl_init();
    ESP_EARLY_LOGI(TAG, "DIAG lvgl_init=%s", display ? "ok" : "failed");
    if (display == NULL) return;

    bsp_display_backlight(80);
    ESP_EARLY_LOGI(TAG, "DIAG backlight=80%%");

    if (!bsp_lvgl_lock(1000)) {
        ESP_EARLY_LOGE(TAG, "DIAG lvgl_lock=failed");
        return;
    }
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "DISPLAY TEST\nLCD + LVGL OK");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    lv_screen_load(screen);
    bsp_lvgl_unlock();
    ESP_EARLY_LOGI(TAG, "DIAG screen_loaded=1; application intentionally stopped");
}
#endif

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

static void chess_app_task(void *arg) {
    (void)arg;
    chess_app_start();
    vTaskDelete(NULL);
}

void app_main(void) {
    esp_err_t err;

#ifdef CONFIG_CHESS_DIAGNOSTIC_DISPLAY
    run_display_diagnostic();
    for (;;) vTaskDelay(portMAX_DELAY);
#endif

    ESP_EARLY_LOGI(TAG, "ai-passport-chess 启动 reset_reason=%d",
                   (int)esp_reset_reason());

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
    ESP_EARLY_LOGI(TAG, "display and LVGL ready");
    bsp_display_backlight(80);
    ESP_EARLY_LOGI(TAG, "backlight enabled");

    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计初始化失败,显示 --");
    }
    if (bsp_button_init(chess_app_button, NULL) != ESP_OK) {
        show_error("Button init failed");
        return;
    }

    if (xTaskCreate(chess_app_task, "chess_app", CHESS_APP_TASK_STACK, NULL,
                    CHESS_APP_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "象棋应用任务创建失败");
        show_error("Chess task failed");
    }
}
