#pragma once
#include <stddef.h>
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned n, size_t size);
int xQueueSend(QueueHandle_t q, const void *item, unsigned ticks);
int xQueueReceive(QueueHandle_t q, void *item, unsigned ticks);
