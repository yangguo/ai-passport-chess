/* M0 firmware baseline (Task 1): display + battery + three buttons +
 * a minimal status screen. No chess yet (Task 8), no radio, no audio.
 * Button callbacks only enqueue; an app task owns all LVGL access.
 * NVS failures land on an error screen — never a global erase.
 */
#include <stdio.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

static const char *TAG = "chess-base";

#define INPUT_QUEUE_DEPTH 16

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_queue;
static lv_obj_t *s_status;
static lv_obj_t *s_last;
static lv_obj_t *s_count;
static uint32_t s_events;

static const char *btn_name(bsp_btn_t b) {
    switch (b) {
    case BSP_BTN_UP:
        return "UP";
    case BSP_BTN_DOWN:
        return "DOWN";
    case BSP_BTN_OK:
        return "OK";
    default:
        return "?";
    }
}

static const char *ev_name(bsp_btn_ev_t e) {
    switch (e) {
    case BSP_BTN_PRESS:
        return "PRESS";
    case BSP_BTN_CLICK:
        return "CLICK";
    case BSP_BTN_DOUBLE:
        return "DOUBLE";
    case BSP_BTN_LONG:
        return "LONG";
    default:
        return "?";
    }
}

static void show_error(const char *why) {
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "error screen without LVGL lock: %s", why);
        return;
    }
    lv_obj_clean(lv_screen_active());
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_label_set_text_fmt(label, "启动失败\n%s", why);
    lv_obj_center(label);
    bsp_lvgl_unlock();
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (s_queue == NULL) {
        return;
    }
    const input_event_t in = { .btn = btn, .event = ev };
    (void)xQueueSend(s_queue, &in, 0);
}

static void refresh_status(bool battery_ok) {
    char line[96];
    int soc = battery_ok ? bsp_battery_soc() : -1;
    int mv = battery_ok ? bsp_battery_mv() : -1;
    if (soc < 0 || mv < 0) {
        snprintf(line, sizeof(line), "显示 OK · 按键 OK · 电量 --");
    } else {
        snprintf(line, sizeof(line), "显示 OK · 按键 OK · 电量 %d%% %dmV", soc, mv);
    }
    lv_label_set_text(s_status, line);
}

void app_main(void) {
    esp_err_t err;
    bool battery_ok;

    ESP_LOGI(TAG, "ai-passport-chess M0 baseline 启动");

    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s (不做全局擦除)", esp_err_to_name(err));
        if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
            ESP_LOGE(TAG, "显示也失败,只能看串口日志");
            return;
        }
        show_error("NVS 初始化失败");
        return;
    }

    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败");
        return;
    }
    bsp_display_backlight(100);

    battery_ok = (bsp_battery_init() == ESP_OK);
    if (!battery_ok) {
        ESP_LOGW(TAG, "电量计初始化失败,显示 --");
    }

    s_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (s_queue == NULL) {
        show_error("事件队列创建失败");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        show_error("按键初始化失败");
        return;
    }

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "LVGL 锁获取失败");
        return;
    }
    lv_obj_clean(lv_screen_active());
    lv_obj_t *title = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_label_set_text(title, "CHESS M0");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 32);
    s_status = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 76);
    s_last = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(s_last, &lv_font_montserrat_20, 0);
    lv_label_set_text(s_last, "-");
    lv_obj_align(s_last, LV_ALIGN_CENTER, 0, 20);
    s_count = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(s_count, &lv_font_montserrat_14, 0);
    lv_obj_align(s_count, LV_ALIGN_BOTTOM_MID, 0, -40);
    refresh_status(battery_ok);
    lv_label_set_text_fmt(s_count, "事件 0");
    bsp_lvgl_unlock();

    ESP_LOGI(TAG, "就绪:按任意键计数,长按 OK 也只记一次");

    for (;;) {
        input_event_t in;
        if (xQueueReceive(s_queue, &in, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_events++;
        ESP_LOGI(TAG, "key %s %s (#%lu)", btn_name(in.btn), ev_name(in.event),
                 (unsigned long)s_events);
        if (!bsp_lvgl_lock(250)) {
            continue;
        }
        lv_label_set_text_fmt(s_last, "%s %s", btn_name(in.btn), ev_name(in.event));
        lv_label_set_text_fmt(s_count, "事件 %lu", (unsigned long)s_events);
        if ((s_events % 16) == 0) {
            refresh_status(battery_ok);
        }
        bsp_lvgl_unlock();
    }
}
