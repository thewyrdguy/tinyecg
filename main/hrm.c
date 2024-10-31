#include <string.h>
#include <esp_log.h>

#include "ble_runner.h"
#include "hrm.h"
#include "data.h"
#include "sampling.h"

#define TAG "HRM"

#define F_HR16 0x01
#define F_HAS_ENERGY 0x08
#define F_HAS_RRI 0x10

static int8_t beat[] = {
  0, 0, 1, 1, 2, 3, 2, 2, 3, 4, 5, 4, 1, -1, -2, -2, -2, -1, -2, -3, -4, -2,
  0, -1, -6, -6, 13, 55, 99, 114, 90, 39, -5, -21, -14, -2, 3, 2, 0, 0, 0, 0,
  0, 1, 2, 3, 3, 4, 4, 5, 6, 7, 9, 10, 11, 12, 14, 15, 17, 19, 21, 24, 27, 29,
  32, 35, 38, 41, 44, 46, 49, 51, 50, 47, 42, 35, 27, 19, 12, 5, 4, 3, 2, 0,
  -2, -3, -5, -5, -5, -4, -3, -2, -2, -1, -1, 0
};

static void makeSamples(int rris, uint16_t rri[], int *nump, int8_t sampp[]) {
	int filled = 0;
	int avail = *nump;

	for (int i = 0; i < rris; i++) {
		// RR Interval comes in 1/1024 of a second.
		// We want SPS (150 / sec) samples.
		int num = rri[i] * SPS / 1024;
		if (num > avail) num = avail;
		int to_copy = sizeof(beat);
		if (to_copy > num) to_copy = num;
		int remains = num - to_copy;
		memcpy(sampp + filled, beat, to_copy);
		if (remains) memset(sampp + filled + to_copy, 0, remains);
		filled += num;
		avail -= num;
	}
	(*nump) = filled;
}

#define SBUFSIZE 1024
static int8_t samples[SBUFSIZE];
uint8_t missed = 0;

static void hrm_receive(uint8_t *data, size_t datalen)
{
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, datalen, ESP_LOG_DEBUG);
	uint8_t const *end = data + datalen;
	uint16_t hr;
	uint16_t energy;
	uint16_t rri[10];
	int rris;
	uint8_t flags = *(data++);
	if (flags & F_HR16) {
		hr = data[0] + (data[1] << 8);
		data += 2;
	} else {
		hr = data[0];
		data++;
	}
	if (flags & F_HAS_ENERGY) {
		energy = data[0] + (data[1] << 8);
		data += 2;
	} else {
		energy = 0;
	}
	if (flags & F_HAS_RRI) {
		int i;
		rris = (end - data) / 2;
		missed = 0;
		if (rris > sizeof(rri) / sizeof(rri[0]))
			rris = sizeof(rri) / sizeof(rri[0]);
		for (i = 0; i < rris; i++) {
			rri[i] = data[0] + (data[1] << 8);
			data += 2;
		}
	} else {
		rris = 0;
		missed++;
	}
	ESP_LOGI(TAG, "HR %hu Energy %hu RRIs %d", hr, energy, rris);
	for (int i = 0; i < rris; i++)
		ESP_LOGI(TAG, "    RRI %hu", rri[i]);

	int num = SBUFSIZE;
	makeSamples(rris, rri, &num, samples);
	ESP_LOGI(TAG, "Synthesised %d samples", num);
	report_jumbo(&(data_stash_t){.heartrate = hr}, num, samples);
}

static void bat_receive(uint8_t *data, size_t datalen)
{
	ESP_LOGI(TAG, "bat_receive (%zu):", datalen);
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, datalen, ESP_LOG_INFO);
	if (datalen == 1) report_rbatt(data[0]);
}

static const characteristic_t main_chars[] = {
	{.uuid = 0x2A37, .type = NOTIFY, .callback = hrm_receive},
	{0},
};

static const characteristic_t batt_chars[] = {
	{.uuid = 0x2A19, .type = NOTIFY, .callback = bat_receive},
	{0},
};

static const service_t services[] = {
	{.uuid = 0x180D, .chars = main_chars},
	{.uuid = 0x180F, .chars = batt_chars},
	{0},
};

const periph_t hrm_desc = {
	.srvlist = services,
	.uuid = 0x180D,
};
