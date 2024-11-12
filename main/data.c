#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "sampling.h"
#include "data.h"

#define TAG "data"

SemaphoreHandle_t dataSemaphore;

data_stash_t stash = {};

// Ring buffer is based on read pointer + amount, as we expect
// 25 times more reads than writes
#define BUFSIZE 384  // 2.7 seconds worth of data at 150 SPS
static int8_t samples[BUFSIZE] = {};
static uint16_t rdp = 0;
static uint16_t amount = 0;

void report_jumbo(data_stash_t *p_ds, int p_num, int8_t *p_samples)
{
	int wrp, avail, buf_left;

	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		memcpy(&stash, p_ds, DYNSIZE);
		wrp = (rdp + amount) % BUFSIZE;
		avail = BUFSIZE - amount;
		buf_left = BUFSIZE - wrp;
		if (buf_left >= p_num) {
			memcpy(samples + wrp, p_samples, p_num);
		} else {
			memcpy(samples + wrp, p_samples, buf_left);
			memcpy(samples, p_samples + buf_left,
					p_num - buf_left);
		}
		if (p_num <= avail) {
			amount += p_num;
			stash.overrun = false;
		} else {
			amount = BUFSIZE;
			rdp = (wrp + p_num) % BUFSIZE;
			stash.overrun = true;
		}
		xSemaphoreGive(dataSemaphore);
	}
}

void report_rssi(uint8_t rssi)
{
	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		stash.rssi = rssi;
		xSemaphoreGive(dataSemaphore);
	}
}

void report_rbatt(uint8_t rbatt)
{
	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		stash.rbatt = rbatt;
		xSemaphoreGive(dataSemaphore);
	}
}

void report_lbatt(uint8_t lbatt)
{
	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		stash.lbatt = lbatt;
		xSemaphoreGive(dataSemaphore);
	}
}

void report_state(enum state_e st)
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

void report_found(bool found)
{
	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		stash.found = found;
		xSemaphoreGive(dataSemaphore);
	}
}

static int repeated_underrun = 0;  // To minimise noise in the log

void get_stash(data_stash_t *newstash, size_t num, int8_t *samples_p)
{
	size_t to_copy, to_repeat, buf_left;

	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		if (num < amount) {
			to_copy = num;
			to_repeat = 0;
			if (repeated_underrun) {
				ESP_LOGI(TAG, "Underrun happened %d times",
						repeated_underrun);
			}
			repeated_underrun = 0;
			stash.underrun = false;
		} else {
			to_copy = amount;
			to_repeat = num - amount;
			if (!repeated_underrun) {
				ESP_LOGI(TAG, "Underrun, have %d samples",
						to_copy);
			}
			repeated_underrun++;
			stash.underrun = true;
		}
		buf_left = BUFSIZE - rdp;
		if (buf_left >= to_copy) {
			memcpy(samples_p, samples + rdp, to_copy);
		} else {
			memcpy(samples_p, samples + rdp, buf_left);
			memcpy(samples_p + buf_left, samples,
					to_copy - buf_left);
		}
		if (to_repeat) {
			memset(samples_p + to_copy,
					samples[(rdp + to_copy - 1) % num],
					to_repeat);
		}
		rdp = (rdp + to_copy) % BUFSIZE;
		amount -= to_copy;

		// Do this after maybe updating underrun field
		memcpy(newstash, &stash, sizeof(stash));
		xSemaphoreGive(dataSemaphore);
	}
}

void data_init()
{
	dataSemaphore = xSemaphoreCreateMutex();
	memset(&stash, 0, sizeof(stash));
}
