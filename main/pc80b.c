#include <string.h>
#include <esp_log.h>

#include "ble_runner.h"
#include "hrm.h"

#define TAG "PC80B"

// .wchar_uuid = 0xfff2,

static void pc80b_receive(uint8_t *data, size_t datalen)
{
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
