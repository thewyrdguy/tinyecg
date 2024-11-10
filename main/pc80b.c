#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "ble_runner.h"
#include "hrm.h"

#define TAG "PC80B"

/*
 ***** crc8 for Maxim (Dallas Semiconductor) OneWire bus protocol *****
 https://github.com/PaulStoffregen/OneWire/blob/master/OneWire.cpp

 Dow-CRC using polynomial X^8 + X^5 + X^4 + X^0
 Tiny 2x16 entry CRC table created by Arjen Lentz
 See http://lentz.com.au/blog/calculating-crc-with-a-tiny-32-entry-lookup-table
 */

static const uint8_t dscrc2x16_table[] = {
	0x00, 0x5E, 0xBC, 0xE2, 0x61, 0x3F, 0xDD, 0x83,
	0xC2, 0x9C, 0x7E, 0x20, 0xA3, 0xFD, 0x1F, 0x41,
	0x00, 0x9D, 0x23, 0xBE, 0x46, 0xDB, 0x65, 0xF8,
	0x8C, 0x11, 0xAF, 0x32, 0xCA, 0x57, 0xE9, 0x74,
};

uint8_t crc8(const uint8_t *addr, uint8_t len)
{
	uint8_t crc = 0;

	while (len--) {
		crc = *addr++ ^ crc;  // just re-using crc as intermediate
		crc = dscrc2x16_table[crc & 0x0f] ^
			dscrc2x16_table[16 + ((crc >> 4) & 0x0f)];
	}
	return crc;
}

/* Application part */

static uint16_t write_handle;

static void send_cmd(uint8_t opcode, uint8_t *data, uint8_t len)
{
	uint8_t buf[64];
	assert(len + 4 < sizeof(buf));
	buf[0] = 0xa5;
	buf[1] = opcode;
	buf[2] = len;
	memcpy(buf + 3, data, len);
	buf[3 + len] = crc8(buf, 3 + len);
	ble_write(write_handle, buf, len + 4);
}

static TaskHandle_t read_batt_task;
static void readBattTask(void *pvParameter)
{
	const TickType_t xFrequency = configTICK_RATE_HZ * 5;
	TickType_t xLastWakeTime = xTaskGetTickCount();
	while (1) {
		send_cmd(0x11, (uint8_t *)"\0", 1);
		vTaskDelayUntil(&xLastWakeTime, xFrequency);
	}
}

#define BLE_MAX 512  // 512 is the max size of BLE characteristic value.
static uint8_t frame[BLE_MAX] = {};
static size_t wptr = 0;

static void cmd_devinfo(uint8_t *payload, uint8_t len)
{
	ESP_LOGI(TAG, "cmd_devinfo");
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_INFO);
}

static void cmd_time(uint8_t *payload, uint8_t len)
{
	ESP_LOGD(TAG, "cmd_time");
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_DEBUG);
	if (len != 8) {
		ESP_LOGE(TAG, "cmd_time bad length %hhu, must be 8", len);
		ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_ERROR);
		return;
	}
	struct {
		uint8_t sec;
		uint8_t min;
		uint8_t hrs;
		uint8_t day;
		uint8_t mon;
		uint8_t yr1;
		uint8_t yr2;
		uint8_t lng;
	} *td = payload;
	uint16_t year = (td->yr2 << 8) | td->yr1;
	ESP_LOGI(TAG, "Time: %04hu-%02hhu-%02hhu %02hhu:%02hhu:%02hhu",
			year, td->mon, td->day, td->hrs, td->min, td->sec);
}

static void cmd_transmode(uint8_t *payload, uint8_t len)
{
	ESP_LOGI(TAG, "cmd_transmode");
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_INFO);
}

static void cmd_contdata(uint8_t *payload, uint8_t len)
{
	ESP_LOGI(TAG, "cmd_contdata");
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_INFO);
}

static void cmd_fastdata(uint8_t *payload, uint8_t len)
{
	ESP_LOGI(TAG, "cmd_fastdata");
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_INFO);
}

static void cmd_heartbeat(uint8_t *payload, uint8_t len)
{
	ESP_LOGI(TAG, "cmd_heartbeat");
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_INFO);
}

static void (* const cmdfunc[16])(uint8_t *payload, uint8_t len) = {
	[0x1] = cmd_devinfo,
	[0x3] = cmd_time,
	[0x5] = cmd_transmode,
	[0xa] = cmd_contdata,
	[0xd] = cmd_fastdata,
	[0xf] = cmd_heartbeat,
};

static void receive(uint8_t *data, size_t datalen)
{
	if (datalen + wptr > BLE_MAX) {
		ESP_LOGE(TAG, "Too much data: len = %zu, wptr=%zu",
				datalen, wptr);
		wptr = 0;
	}
	memcpy(frame + wptr, data, datalen);
	wptr += datalen;
	size_t rptr;
	for (rptr = 0; rptr < wptr; ) {
		uint8_t flen = frame[rptr + 2] + 4;
		if (rptr + flen > wptr) {  // Frame incomplete
			break;
		}
		uint8_t crc = crc8(frame + rptr, flen - 1);
		uint8_t fidx;
		if ((frame[rptr] == 0xa5) &&
				(crc == frame[rptr + flen - 1]) &&
				((fidx = (frame[rptr + 1] & 0xf)) ==
					(frame[rptr + 1] >> 4))) {
			if (cmdfunc[fidx]) {
				(*cmdfunc[fidx])(frame + rptr + 3, flen - 4);
			} else {
				ESP_LOGE(TAG, "Unhandled opcode 0x%01hhx",
					frame[rptr + 1]);
				ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, datalen,
					ESP_LOG_ERROR);
			}
		} else {
			ESP_LOGE(TAG, "Tag 0x%01hhx, opcode 0x%01hhx, crc"
				" calculated 0x%01hhx, provided 0x%01hhx",
					frame[rptr],
					frame[rptr + 1],
					crc,
					frame[rptr + flen - 1]);
			ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, datalen,
					ESP_LOG_ERROR);
		}
		rptr += flen;
	}
	if (wptr - rptr) {
		memmove(frame, frame + rptr, wptr - rptr);
	}
	wptr -= rptr;
}

static void get_write_handle(uint8_t *data, size_t datalen)
{
	assert(datalen == sizeof(uint16_t));
	write_handle = *(uint16_t*)data;
	send_cmd(0x11, (uint8_t *)"\0\0\0\0\0\0", 6);
}

static void start(void)
{
	wptr = 0;
	xTaskCreate(readBattTask, "read RSSI", 4096*2, NULL, 0,
			&read_batt_task);
}

static void stop(void)
{
	wptr = 0;
	vTaskDelete(read_batt_task);
}

static const characteristic_t main_chars[] = {
	{.uuid = 0xfff1, .type = NOTIFY, .callback = receive},
	{.uuid = 0xfff2, .type = WRITE, .callback = get_write_handle},
	{0},
};

static const service_t services[] = {
	{.uuid = 0xfff0, .chars = main_chars},
	{0},
};

const periph_t pc80b_desc = {
	.srvlist = services,
	.name = "PC80B-BLE",
	.start = start,
	.stop = stop,
};
