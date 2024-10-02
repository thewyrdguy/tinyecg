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

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
	uint8_t *adv_name = NULL;
	uint8_t adv_name_len = 0;
	//ESP_LOGD(TAG, "esp_gap_cb(%x, ...) called", event);
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
			ESP_LOGD(TAG, "ESP_GAP_SEARCH_INQ_CMPL_EVT");
			break;
		default:
			ESP_LOGD(TAG, "Unhandled GAP search EVT %x",
					scan_result->scan_rst.search_evt);
		}
		break;
	case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
		if (param->scan_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
			ESP_LOGD(TAG, "Scan completed");
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
		ESP_LOGD(TAG, "Unhandled GAP EVT %x", event);
		break;
	}
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
		esp_ble_gattc_cb_param_t *param)
{
	ESP_LOGD(TAG, "We cannot have this now, we are not connecting!");
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
	//ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
	ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
	ESP_ERROR_CHECK(esp_bluedroid_init());
	ESP_ERROR_CHECK(esp_bluedroid_enable());
	ESP_ERROR_CHECK(esp_ble_gap_register_callback(esp_gap_cb));
	ESP_ERROR_CHECK(esp_ble_gattc_register_callback(esp_gattc_cb));
	ESP_ERROR_CHECK(esp_ble_gattc_app_register(PROFILE_A_APP_ID));
	ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(LOCAL_MTU));
	ESP_LOGI(TAG, "Initialization done");
}
