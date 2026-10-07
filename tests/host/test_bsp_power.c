/* Execute the real BSP transition with an ADC-configured wake pad. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "runtime.h"
#include "../../components/bsp/src/bsp_power.c"
static jmp_buf slept;
static bool input_enabled, held, locked, press_during_settle;
static int config_error, wake_error;
static unsigned checks, delay_calls, shutdown_calls, sleep_calls;
esp_err_t gpio_config(const gpio_config_t *c) {
    assert(c->pin_bit_mask == 1 && c->mode == GPIO_MODE_INPUT);
    assert(!c->pull_up_en && !c->pull_down_en);
    if (config_error) return config_error;
    input_enabled = true; return ESP_OK;
}
int gpio_get_level(int pin) { assert(pin == 0); checks++; return input_enabled && !held; }
esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t mask, int mode) {
    assert(mask == 1 && mode == ESP_GPIO_WAKEUP_GPIO_LOW);
    assert(input_enabled && checks >= 2); return wake_error;
}
int esp_sleep_get_wakeup_cause(void) { return 0; }
void esp_deep_sleep_start(void) { sleep_calls++; longjmp(slept, 1); }
void esp_restart(void) { assert(0); }
void vTaskDelay(unsigned ticks) {
    assert(ticks > 0); delay_calls++;
    if (press_during_settle) held = true;
}
bool bsp_lvgl_lock(int timeout) { assert(timeout > 0); return locked; }
esp_err_t bsp_battery_sleep(void) { shutdown_calls++; return ESP_OK; }
esp_err_t bsp_i2c_prepare_deep_sleep(void) { shutdown_calls++; return ESP_OK; }
esp_err_t bsp_display_prepare_deep_sleep(void) { shutdown_calls++; return ESP_OK; }
static void reset(void) {
    input_enabled = false; held = false; locked = true; press_during_settle = false;
    config_error = wake_error = 0;
    checks = delay_calls = shutdown_calls = sleep_calls = 0;
}
int main(void) {
    reset(); /* ADC init leaves the digital input disabled. */
    if (!setjmp(slept)) { bsp_power_enter_deep_sleep(); assert(0); }
    assert(input_enabled && delay_calls && sleep_calls == 1 && shutdown_calls == 3);
    reset(); held = true;
    assert(bsp_power_enter_deep_sleep() == ESP_ERR_INVALID_STATE);
    assert(shutdown_calls == 0 && sleep_calls == 0);
    reset(); press_during_settle = true;
    assert(bsp_power_enter_deep_sleep() == ESP_ERR_INVALID_STATE);
    assert(shutdown_calls == 0 && sleep_calls == 0);
    reset(); config_error = ESP_FAIL;
    assert(bsp_power_enter_deep_sleep() == ESP_FAIL);
    assert(shutdown_calls == 0);
    reset(); wake_error = ESP_FAIL;
    assert(bsp_power_enter_deep_sleep() == ESP_FAIL);
    assert(shutdown_calls == 0);
    reset(); locked = false;
    assert(bsp_power_enter_deep_sleep() == ESP_ERR_TIMEOUT);
    assert(shutdown_calls == 0);
    puts("BSP power transition tests passed");
}
