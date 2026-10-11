#pragma once
#include <stddef.h>
#include <stdint.h>

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS pdTRUE
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void (*TaskFunction_t)(void *);
