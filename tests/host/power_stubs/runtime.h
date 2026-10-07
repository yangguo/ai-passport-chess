#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_STATE 0x103
#define BSP_BTN_GPIO 0
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define ESP_SLEEP_WAKEUP_GPIO 7
#define ESP_GPIO_WAKEUP_GPIO_LOW 0
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
int gpio_get_level(int pin);
esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t mask, int mode);
int esp_sleep_get_wakeup_cause(void);
void esp_deep_sleep_start(void);
void esp_restart(void);
void vTaskDelay(unsigned ticks);
bool bsp_lvgl_lock(int timeout);
esp_err_t bsp_battery_sleep(void);
esp_err_t bsp_i2c_prepare_deep_sleep(void);
esp_err_t bsp_display_prepare_deep_sleep(void);
