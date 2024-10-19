#include <string.h>
#include <esp_log.h>

#include "ble_runner.h"
#include "hrm.h"

#define TAG "HRM"

static void hrm_receive(uint8_t *data, size_t datalen)
{
}

static const periph_t hrm_desc = {
	.callback = hrm_receive,
	.srv_uuid = 0x180D,
	.nchar_uuid = 0x2A37,
};

const periph_t *hrm_init(void)
{
	ESP_LOGI(TAG, "Initialising");
	return &hrm_desc;
}
