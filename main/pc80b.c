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

static void pc80b_receive(uint8_t *data, size_t datalen)
{
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, datalen, ESP_LOG_INFO);
}

static const periph_t pc80b_desc = {
	.callback = pc80b_receive,
	.name = "PC80B-BLE",
	.srv_uuid = 0xfff0,
	.nchar_uuid = 0xfff1,
};

const periph_t *pc80b_init(void)
{
	ESP_LOGI(TAG, "Initialising");
	return &pc80b_desc;
}
