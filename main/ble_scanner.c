#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <esp_gatt_defs.h>
#include <esp_bt_main.h>
#include <esp_gatt_common_api.h>
#include "nvs.h"
#include "nvs_flash.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include "ble_scanner.h"

#define TAG "ble_scanner"

#define PROFILE_NUM 1
#define PROFILE_A_APP_ID 0
#define LOCAL_MTU 500

static void *data_handle = NULL;  // esp ble does not have "private data" pointer?

static void gattc_profile_event_handler(esp_gattc_cb_event_t event,
		esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *p_data)
{
	switch (event) {
	case ESP_GATTC_REG_EVT:
		ESP_LOGI(TAG, "REG_EVT on if %d", gattc_if);
		ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(
			&(esp_ble_scan_params_t) {
				.scan_type = BLE_SCAN_TYPE_ACTIVE,
				.own_addr_type = BLE_ADDR_TYPE_PUBLIC,
				.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
				.scan_interval = 0x50,
				.scan_window = 0x30,
				.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
			}));
		break;
	default:
		ESP_LOGI(TAG, "EVT %d on if %d", event, gattc_if);
		break;
	}
}

struct gattc_profile_inst {
	esp_gattc_cb_t gattc_cb;
	uint16_t gattc_if;
	uint16_t app_id;
	uint16_t conn_id;
	uint16_t service_start_handle;
	uint16_t service_end_handle;
	uint16_t char_handle;
	esp_bd_addr_t remote_bda;
};

static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
	[PROFILE_A_APP_ID] = {
		.gattc_cb = gattc_profile_event_handler,
		.gattc_if = ESP_GATT_IF_NONE,
	},
};

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
	uint8_t *adv_name = NULL;
	uint8_t adv_name_len = 0;
	ESP_LOGI(TAG, "esp_gap_cb(%x, ...) called", event);
	switch (event) {
	case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
		uint32_t duration = 30;
		esp_ble_gap_start_scanning(duration);
		break;
	case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
		if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
			ESP_LOGE(TAG, "Scan failed, status %x",
					 param->scan_start_cmpl.status);
		} else {
			ESP_LOGI(TAG, "Stan started");
		}
		break;
	case ESP_GAP_BLE_SCAN_RESULT_EVT:
		esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
		switch (scan_result->scan_rst.search_evt) {
		case ESP_GAP_SEARCH_INQ_RES_EVT:
			esp_log_buffer_hex(TAG, scan_result->scan_rst.bda, 6);
			ESP_LOGI(TAG, "searched Adv Data Len %d, Scan Response Len %d",
					scan_result->scan_rst.adv_data_len,
					scan_result->scan_rst.scan_rsp_len);
			adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
			ESP_LOGI(TAG, "searched Device Name Len %d", adv_name_len);
			esp_log_buffer_char(TAG, adv_name, adv_name_len);
			// Initiate connection here
			// esp_ble_gap_stop_scanning();
			// esp_ble_gattc_open(...);
			break;
		case ESP_GAP_SEARCH_INQ_CMPL_EVT:
			ESP_LOGI(TAG, "ESP_GAP_SEARCH_INQ_CMPL_EVT");
			break;
		default:
			ESP_LOGI(TAG, "Unhandled GAP search EVT %x",
					scan_result->scan_rst.search_evt);
		}
		break;
	case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
		if (param->scan_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
			ESP_LOGI(TAG, "Scan completed");
		} else {
			ESP_LOGE(TAG, "scan stop failed, error status = %x",
					param->scan_stop_cmpl.status);
		}
		break;
	case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
		if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
			ESP_LOGI(TAG, "Stop adv successfully");
		} else {
			ESP_LOGE(TAG, "adv stop failed, error status = %x",
				       	param->adv_stop_cmpl.status);
		}
		break;
	case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
		ESP_LOGI(TAG, "update connection params status = %d, "
				"min_int = %d, max_int = %d, conn_int = %d, "
				"latency = %d, timeout = %d",
				param->update_conn_params.status,
				param->update_conn_params.min_int,
				param->update_conn_params.max_int,
				param->update_conn_params.conn_int,
				param->update_conn_params.latency,
				param->update_conn_params.timeout);
		break;
	case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:
		ESP_LOGI(TAG, "packet length updated: rx = %d, "
				"tx = %d, status = %d",
				param->pkt_data_length_cmpl.params.rx_len,
				param->pkt_data_length_cmpl.params.tx_len,
				param->pkt_data_length_cmpl.status);
		break;
	default:
		ESP_LOGI(TAG, "Unhandled GAP EVT %x", event);
		break;
	}
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
		esp_ble_gattc_cb_param_t *param)
{
	if (event == ESP_GATTC_REG_EVT) {
		if (param->reg.status == ESP_GATT_OK) {
			ESP_LOGI(TAG, "ESP_GATTC_REG_EVT app_id %04x if %d",
					param->reg.app_id, gattc_if);
			gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
		} else {
			ESP_LOGI(TAG, "reg app failed, app_id %04x, status %d",
					param->reg.app_id,
					param->reg.status);
			return;
		}
	}
	for (int i = 0; i < PROFILE_NUM; i++) {
		if ((gattc_if == ESP_GATT_IF_NONE
				|| gattc_if == gl_profile_tab[i].gattc_if)
			&& gl_profile_tab[i].gattc_cb) {
			gl_profile_tab[i].gattc_cb(event, gattc_if, param);
		}
	}
}

void ble_scanner_init(void *handle)
{
	data_handle = handle;
	ESP_LOGI(TAG, "Initializing");
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
	ESP_ERROR_CHECK(esp_bt_controller_init(
		&(esp_bt_controller_config_t)BT_CONTROLLER_INIT_CONFIG_DEFAULT()));
	ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
	ESP_ERROR_CHECK(esp_bluedroid_init());
	ESP_ERROR_CHECK(esp_bluedroid_enable());
	ESP_ERROR_CHECK(esp_ble_gap_register_callback(esp_gap_cb));
	ESP_ERROR_CHECK(esp_ble_gattc_register_callback(esp_gattc_cb));
	ESP_ERROR_CHECK(esp_ble_gattc_app_register(PROFILE_A_APP_ID));
	ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(LOCAL_MTU));
	ESP_LOGI(TAG, "Initialization done");
}
