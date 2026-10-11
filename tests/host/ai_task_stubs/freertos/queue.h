#pragma once
#include "freertos/FreeRTOS.h"

QueueHandle_t xQueueCreate(unsigned n, size_t size);
void vQueueDelete(QueueHandle_t q);
int xQueueSend(QueueHandle_t q, const void *item, unsigned ticks);
int xQueueReceive(QueueHandle_t q, void *item, unsigned ticks);
