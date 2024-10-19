#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "data.h"

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

void report_rbatt(uint8_t p_rbatt)
{
	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		stash.rbatt = p_rbatt;
		xSemaphoreGive(dataSemaphore);
	}
}

void report_lbatt(uint8_t p_lbatt)
{
	if (xSemaphoreTake(dataSemaphore, portMAX_DELAY) == pdTRUE) {
		stash.lbatt = p_lbatt;
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
