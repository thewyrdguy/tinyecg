#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "data.h"

SemaphoreHandle_t dataSemaphore;

data_stash_t stash;

void report_hr(int hr)
{
	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		stash.hr = hr;
		xSemaphoreGive(dataSemaphore);
	}
}

void report_state(state_t st)
{
	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		stash.state = st;
		xSemaphoreGive(dataSemaphore);
	}
}

void report_periph(char const *name, size_t len)
{
	size_t to_copy = (len < sizeof(stash.name))
			? len : sizeof(stash.name) - 1;
	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		strncpy(stash.name, name, to_copy);
		stash.name[to_copy] = '\0';
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
