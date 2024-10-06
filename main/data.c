#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "data.h"

SemaphoreHandle_t dataSemaphore;

data_stash_t stash;

void report_periph(char const *name)
{
	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		strncpy(stash.name, name, sizeof(stash.name));
		xSemaphoreGive(dataSemaphore);
	}
}

void get_stash(data_stash_t *newstash)
{
	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		memcpy(newstash, &stash, sizeof(stash));
		xSemaphoreGive(dataSemaphore);
	}
}

void data_init()
{
	dataSemaphore = xSemaphoreCreateMutex();
	memset(&stash, 0, sizeof(stash));
}
