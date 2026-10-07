#pragma once
#include "esp_err.h"
esp_err_t bsp_power_wait_for_wake_release(void);
esp_err_t bsp_power_enter_deep_sleep(void);
