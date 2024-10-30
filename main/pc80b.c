#include <string.h>
#include <esp_log.h>

#include "ble_runner.h"
#include "hrm.h"

#define TAG "PC80B"

/*
	.wchar_uuid = 0xfff2,

		uint8_t write_char_data[35];
		for (int i = 0; i < sizeof(write_char_data); ++i) {
			write_char_data[i] = i % 256;
		}
		esp_ble_gattc_write_char(
			gattc_if,
			gattc_profile.conn_id,
			gattc_profile.char_handle,
			sizeof(write_char_data),
			write_char_data,
			ESP_GATT_WRITE_TYPE_RSP,
			ESP_GATT_AUTH_REQ_NONE);
*/

static uint16_t write_handle;

static void receive(uint8_t *data, size_t datalen)
{
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, datalen, ESP_LOG_INFO);
}

static void get_write_handle(uint8_t *data, size_t datalen)
{
	assert(datalen == sizeof(uint16_t));
	write_handle = *(uint16_t*)data;
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
};
