#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "data.h"
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

static TaskHandle_t heartbeat_task = 0;
static void heartbeatTask(void *pvParameter)
{
	const TickType_t xFrequency = configTICK_RATE_HZ * 15;
	TickType_t xLastWakeTime = xTaskGetTickCount();
	while (1) {
		send_cmd(0xff, (uint8_t *)"\0", 1);
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
	struct _td {
		uint8_t sec;
		uint8_t min;
		uint8_t hrs;
		uint8_t day;
		uint8_t mon;
		uint8_t yr1;
		uint8_t yr2;
		uint8_t lng;
	} *td = (struct _td *)payload;
	uint16_t year = (td->yr2 << 8) | td->yr1;
	ESP_LOGI(TAG, "Time: %04hu-%02hhu-%02hhu %02hhu:%02hhu:%02hhu",
			year, td->mon, td->day, td->hrs, td->min, td->sec);
}

static void cmd_transmode(uint8_t *payload, uint8_t len)
{
	ESP_LOGD(TAG, "cmd_transmode");
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_DEBUG);
	struct _mframe {
		uint8_t devtyp;
		uint8_t transtype:1;
		uint8_t pad1:6;
		uint8_t filtermode:1;
		uint8_t sn[12];
	} *d = (struct _mframe *)payload;
	if (len != sizeof(struct _mframe)) {
		ESP_LOGE(TAG, "cmd_transmode bad length %hhu, must be %zu",
				len, sizeof(struct _mframe));
		ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_ERROR);
		return;
	}
	ESP_LOGI(TAG, "Filter mode %hhu, transtype %hhu",
			d->filtermode, d->transtype);
	uint8_t ack = (d->transtype ? 0x01 : 0x00);
	send_cmd(0x55, &ack, 1);
}

#define SAMPS 25

static uint8_t prevcseq = 0;

static void cmd_contdata(uint8_t *payload, uint8_t len)
{
	ESP_LOGD(TAG, "cmd_contdata");
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_DEBUG);
	struct _cdframe {
		uint8_t seq;
		uint8_t data[50];
		uint8_t hr;
		uint8_t vol_l;
		uint8_t vol_h:4;
		uint8_t gain:3;
		uint8_t leadoff:1;
	} __attribute__((packed)) *d = (struct _cdframe *)payload;
	if ((len != 1) && (len != sizeof(struct _cdframe))) {
		ESP_LOGE(TAG, "cmd_contdata bad length %hhu, must be 1 or %zu",
				len, sizeof(struct _cdframe));
		ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_ERROR);
		return;
	}
	if ((len == 1) || (d->seq % 64 == 0)) { // Stop data or /64, need ACK
		uint8_t ack[2] = {d->seq, 0x00};
		send_cmd(0xaa, ack, 2);
	}
	if (len == 1) {
		report_jumbo(&(data_stash_t){0}, 0, NULL);
		return;
	}

	if (d->seq != (prevcseq + 1)) {
		ESP_LOGE(TAG, "Cont wrong sequence: prev %hhu, new %hhu",
				prevcseq, d->seq);
	}
	prevcseq = d->seq;
	uint16_t vol = (d->vol_h << 8) + d->vol_l;
	int8_t samps[SAMPS];
	for (int i = 0; i < SAMPS; i++) {
		samps[i] =((d->data[i * 2] + (d->data[i * 2 + 1] << 8))
				- 2048) / 4;
	}
	report_jumbo(&(data_stash_t){
			.volume = vol,
			.gain = d->gain,
			.mstage = ms_measuring,
			.mmode = mm_continuous,
			.leadoff = d->leadoff,
			.heartrate = d->hr,
		}, SAMPS, samps);
}

static uint8_t prevfseq = 0;

static void cmd_fastdata(uint8_t *payload, uint8_t len)
{
	ESP_LOGD(TAG, "cmd_fastdata");
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_DEBUG);
	struct _fdframe {
		uint8_t seq;
		uint8_t _unk1;
		uint8_t pad1:4;
		uint8_t gain:3;
		uint8_t pad2:1;
		uint8_t mstage:4;
		uint8_t mmode:2;
		uint8_t channel:2;
		uint8_t hr;
		uint8_t datatype:3;
		uint8_t pad3:4;
		uint8_t leadoff:1;
		uint8_t data[50];
	} __attribute__((packed)) *d = (struct _fdframe *)payload;
	if (len == 6) {
		ESP_LOGI(TAG, "End frame");
		report_jumbo(&(data_stash_t){0}, 0, NULL);
		return;
	}
	if (len != sizeof(struct _fdframe)) {
		ESP_LOGE(TAG, "cmd_contdata bad length %hhu, must be %zu",
				len, sizeof(struct _fdframe));
		ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_ERROR);
		return;
	}
	if (d->seq != (prevfseq + 1)) {
		ESP_LOGE(TAG, "Fast wrong sequence: prev %hhu, new %hhu",
				prevfseq, d->seq);
	}
	prevfseq = d->seq;
	int8_t samps[SAMPS];
	for (int i = 0; i < SAMPS; i++) {
		samps[i] =((d->data[i * 2] + (d->data[i * 2 + 1] << 8))
				- 2048) / 4;
	}
	report_jumbo(&(data_stash_t){
			.gain = d->gain,
			.mstage = (enum mstage_e)d->mstage,
			.mmode = (enum mmode_e)d->mmode,
			.channel = (enum channel_e)d->channel,
			.datatype = (enum datatype_e)d->datatype,
			.leadoff = d->leadoff,
			.heartrate = d->hr,
		}, SAMPS, samps);
}

static void cmd_heartbeat(uint8_t *payload, uint8_t len)
{
	ESP_LOGD(TAG, "cmd_heartbeat");
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_DEBUG);
	if (len != 1) {
		ESP_LOGE(TAG, "cmd_heartbeat bad length %hhu, must be 8", len);
		ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, len, ESP_LOG_ERROR);
		return;
	}
	report_rbatt(payload[0] * 33);  // value is from 0 to 3
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
}

static void start(void)
{
	ESP_LOGI(TAG, "start()");
	wptr = 0;
	xTaskCreate(heartbeatTask, "Heartbeat", 4096*2, NULL, 0,
			&heartbeat_task);
	send_cmd(0x11, (uint8_t *)"\0\0\0\0\0\0", 6);
}

static void stop(void)
{
	ESP_LOGI(TAG, "stop()");
	wptr = 0;
	if (heartbeat_task) vTaskDelete(heartbeat_task);
	heartbeat_task = 0;
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
