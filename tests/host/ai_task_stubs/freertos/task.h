#pragma once
#include "freertos/FreeRTOS.h"

int xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                void *arg, unsigned prio, TaskHandle_t *out);
void vTaskDelay(unsigned ticks);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task);
