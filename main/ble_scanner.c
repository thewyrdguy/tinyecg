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
#include <freertos/FreeRTOS.h>
#include <esp_log.h>

#include "ble_scanner.h"

#define TAG "ble_scanner"

#define SCAN_DURATION 30
#define PROFILE_APP_ID 0
#define LOCAL_MTU 500

static void *data_handle = NULL;  // esp ble does not have "private data" pointer?

struct _endpoint {
	char *name;
	uint16_t srv_uuid;
	uint16_t nchar_uuid;
	uint16_t wchar_uuid;
};

struct _endpoint hrm_desc = {
	.srv_uuid = 0x180D,
	.nchar_uuid = 0x2A37,
};

struct _endpoint pc80b_desc = {
	.name = "PC80B-BLE",
	.srv_uuid = 0xfff0,
	.nchar_uuid = 0xfff1,
	.wchar_uuid = 0xfff2,
};

struct _endpoint *endpoint = NULL;

// We can have multiple profiles, with their individual callbacks. But we have only 1.

struct gattc_profile_inst {
	uint16_t gattc_if;
	uint16_t app_id;
	uint16_t conn_id;
	uint16_t service_start_handle;
	uint16_t service_end_handle;
	uint16_t char_handle;
	esp_bd_addr_t remote_bda;
} gattc_profile = {
	.gattc_if = ESP_GATT_IF_NONE,
};

bool connect = false;
bool get_server = false;

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
	uint8_t *adv_name = NULL;
	uint8_t adv_name_len = 0;
	uint8_t *adv_srv = NULL;
	uint8_t adv_srv_len = 0;
	uint16_t adv_srv_uuid = 0;
	ESP_LOGD(TAG, "esp_gap_cb(%x, ...) called", event);
	switch (event) {
	case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
		esp_ble_gap_start_scanning(SCAN_DURATION);
		break;
	case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
		if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
			ESP_LOGE(TAG, "Scan failed, status %x",
					 param->scan_start_cmpl.status);
		} else {
			ESP_LOGD(TAG, "Scan started");
		}
		break;
	case ESP_GAP_BLE_SCAN_RESULT_EVT:
		esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
		switch (scan_result->scan_rst.search_evt) {
		case ESP_GAP_SEARCH_INQ_RES_EVT:
			esp_log_buffer_hex(TAG, scan_result->scan_rst.bda, 6);
			ESP_LOGI(TAG, "Adv Data Len %d, Scan Response Len %d",
					scan_result->scan_rst.adv_data_len,
					scan_result->scan_rst.scan_rsp_len);
			adv_name = esp_ble_resolve_adv_data(
					scan_result->scan_rst.ble_adv,
					ESP_BLE_AD_TYPE_NAME_CMPL,
					&adv_name_len);
			ESP_LOGI(TAG, "Device Name Len %d", adv_name_len);
			esp_log_buffer_char(TAG, adv_name, adv_name_len);
			adv_srv = esp_ble_resolve_adv_data(
					scan_result->scan_rst.ble_adv,
					ESP_BLE_AD_TYPE_16SRV_CMPL,
					&adv_srv_len);
			ESP_LOGI(TAG, "Device Srv Len %d", adv_srv_len);
			if (adv_srv_len == sizeof(uint16_t)) {
				adv_srv_uuid = adv_srv[0] + (adv_srv[1] << 8);
				ESP_LOGI(TAG, "Device Srv uuid %04x", adv_srv_uuid);
			}
			if (adv_srv_uuid == hrm_desc.srv_uuid) {
				endpoint = &hrm_desc;
			}
			if ((strlen(pc80b_desc.name) == adv_name_len)
				&& (strncmp((char *)adv_name, pc80b_desc.name,
						adv_name_len) == 0)) {
				endpoint = &pc80b_desc;
			}
			if (endpoint) {
				ESP_LOGI(TAG, "Found %s",
					       	endpoint->name ? endpoint->name : "HRM");
				if (!connect) {
					connect = true;
					ESP_LOGI(TAG, "Connecting");
					esp_ble_gap_stop_scanning();
					esp_ble_gattc_open(gattc_profile.gattc_if,
						scan_result->scan_rst.bda,
						scan_result->scan_rst.ble_addr_type,
						true);
				}
			}
			break;
		case ESP_GAP_SEARCH_INQ_CMPL_EVT:
			ESP_LOGD(TAG, "ESP_GAP_SEARCH_INQ_CMPL_EVT");
			break;
		default:
			ESP_LOGE(TAG, "Unhandled GAP search EVT %x",
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
		ESP_LOGE(TAG, "Unhandled GAP EVT %x", event);
		break;
	}
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
		esp_ble_gattc_cb_param_t *p_data)
{
	static esp_ble_scan_params_t scan_params = {
		.scan_type = BLE_SCAN_TYPE_PASSIVE,
		.own_addr_type = BLE_ADDR_TYPE_PUBLIC,
		.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
		.scan_interval = 0x50,
		.scan_window = 0x30,
		.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
	};
	ESP_LOGI(TAG, "esp_gattc_cb event=%d if=%d app_id=%d", event, gattc_if,
			p_data->reg.app_id);
	// If multiple profiles, we would select the one and call its callback
	switch (event) {
	case ESP_GATTC_REG_EVT:
		if (p_data->reg.app_id != PROFILE_APP_ID) {
			ESP_LOGE(TAG, "REG_EVT gave us wrong app_id %d",
					p_data->reg.app_id);
			return;
		}
		if (p_data->reg.status == ESP_GATT_OK) {
			ESP_LOGD(TAG, "ESP_GATTC_REG_EVT app_id %04x if %d",
					p_data->reg.app_id, gattc_if);
			gattc_profile.gattc_if = gattc_if;
		} else {
			ESP_LOGI(TAG, "reg app failed, app_id %04x, status %d",
					p_data->reg.app_id, p_data->reg.status);
			return;
		}
		// This will send GAP indication that it can start scanning
		ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));
		break;
	case ESP_GATTC_CONNECT_EVT:
		ESP_LOGD(TAG, "ESP_GATTC_CONNECT_EVT conn_id %d, if %d",
				p_data->connect.conn_id, gattc_if);
		gattc_profile.conn_id = p_data->connect.conn_id;
		memcpy(gattc_profile.remote_bda, p_data->connect.remote_bda,
				sizeof(esp_bd_addr_t));
		ESP_LOGI(TAG, "REMOTE BDA:");
		esp_log_buffer_hex(TAG, gattc_profile.remote_bda,
				sizeof(esp_bd_addr_t));
		ESP_ERROR_CHECK(esp_ble_gattc_send_mtu_req(gattc_if,
				p_data->connect.conn_id));
		break;
	case ESP_GATTC_OPEN_EVT:
		if (p_data->open.status == ESP_GATT_OK) {
			ESP_LOGD(TAG, "open success");
		} else {
			ESP_LOGE(TAG, "open failed, status %d", p_data->open.status);
		}
		break;
	case ESP_GATTC_DIS_SRVC_CMPL_EVT:
		if (p_data->dis_srvc_cmpl.status == ESP_GATT_OK) {
			ESP_LOGI(TAG, "discover service complete conn_id %d",
					p_data->dis_srvc_cmpl.conn_id);
			esp_ble_gattc_search_service(gattc_if,
					p_data->dis_srvc_cmpl.conn_id,
					&(esp_bt_uuid_t){
						.len = ESP_UUID_LEN_16,
						.uuid = {.uuid16 = endpoint->srv_uuid,},
					});
		} else {
			ESP_LOGE(TAG, "discover service failed, status %d",
					p_data->dis_srvc_cmpl.status);
		}
		break;
	case ESP_GATTC_CFG_MTU_EVT:
		if (p_data->cfg_mtu.status == ESP_GATT_OK) {
			ESP_LOGD(TAG, "ESP_GATTC_CFG_MTU_EVT, "
					"Status %d, MTU %d, conn_id %d",
					p_data->cfg_mtu.status,
					p_data->cfg_mtu.mtu,
					p_data->cfg_mtu.conn_id);
		} else {
			ESP_LOGE(TAG,"config mtu failed, error status = %x",
					p_data->cfg_mtu.status);
		}
		break;
	case ESP_GATTC_SEARCH_RES_EVT:
		ESP_LOGI(TAG, "SEARCH RES: conn_id = %x is primary service %d",
				p_data->search_res.conn_id,
				p_data->search_res.is_primary);
		ESP_LOGI(TAG, "start handle %d end handle %d "
				"current handle value %d",
				p_data->search_res.start_handle,
				p_data->search_res.end_handle,
				p_data->search_res.srvc_id.inst_id);
		 if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16
		  && p_data->search_res.srvc_id.uuid.uuid.uuid16 == endpoint->srv_uuid) {
			ESP_LOGI(TAG, "Service uuid discoverd");
			gattc_profile.service_start_handle =
				p_data->search_res.start_handle;
			gattc_profile.service_end_handle =
				p_data->search_res.end_handle;
			get_server = true;
		}
		break;
	case ESP_GATTC_SEARCH_CMPL_EVT:
		if (p_data->search_cmpl.status != ESP_GATT_OK) {
			ESP_LOGE(TAG, "search service failed, error status = %x",
					p_data->search_cmpl.status);
			break;
		}
		if (p_data->search_cmpl.searched_service_source
				== ESP_GATT_SERVICE_FROM_REMOTE_DEVICE) {
			ESP_LOGI(TAG, "Service information came from remote device");
		} else if (p_data->search_cmpl.searched_service_source
				== ESP_GATT_SERVICE_FROM_NVS_FLASH) {
			ESP_LOGI(TAG, "Service information came from NVS");
		} else {
			ESP_LOGI(TAG, "Unknown service information source");
		}
		if (get_server) {
			uint16_t count = 0;
			if (esp_ble_gattc_get_attr_count(
						gattc_if,
						p_data->search_cmpl.conn_id,
						ESP_GATT_DB_CHARACTERISTIC,
						gattc_profile.service_start_handle,
						gattc_profile.service_end_handle,
						0,  // "Invalid handle"?
						&count) != ESP_GATT_OK) {
				ESP_LOGE(TAG, "esp_ble_gattc_get_attr_count error");
				break;
			}
			ESP_LOGI(TAG, "%hu characteristics found", count);
			if (count == 0) {
				ESP_LOGE(TAG, "%hu char found", count);
				break;
			}
			esp_gattc_char_elem_t *char_elem_result =
				(esp_gattc_char_elem_t *)malloc(
						sizeof(esp_gattc_char_elem_t) * count);
			if (!char_elem_result) {
				ESP_LOGE(TAG, "gattc no mem");
				break;
			}
			if (esp_ble_gattc_get_char_by_uuid(
						gattc_if,
						p_data->search_cmpl.conn_id,
						gattc_profile.service_start_handle,
						gattc_profile.service_end_handle,
						(esp_bt_uuid_t){
							.len = ESP_UUID_LEN_16,
							.uuid = {.uuid16 =
								endpoint->nchar_uuid,},
						},
						char_elem_result,
						&count) != ESP_GATT_OK) {
				ESP_LOGE(TAG, "esp_ble_gattc_get_char_by_uuid");
				free(char_elem_result);
				break;
			}
			ESP_LOGI(TAG, "%hu characteristics match uuid", count);
			for (int i = 0; i < count; i++) {
				ESP_LOGI(TAG, "char %d handle %d notifiable %d",
						i,
						char_elem_result[i].char_handle,
						char_elem_result[i].properties
					       	  & ESP_GATT_CHAR_PROP_BIT_NOTIFY);
			}
			free(char_elem_result);
		}
		break;
	case ESP_GATTC_REG_FOR_NOTIFY_EVT:
		ESP_LOGD(TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT");
		// esp_ble_gattc_get_attr_count();
		// esp_ble_gattc_get_descr_by_char_handle();
		// esp_ble_gattc_write_char_descr();
		// ...
		break;
	case ESP_GATTC_NOTIFY_EVT:
		if (p_data->notify.is_notify) {
			ESP_LOGD(TAG, "ESP_GATTC_NOTIFY_EVT, receive notify value:");
		} else {
			ESP_LOGD(TAG, "ESP_GATTC_NOTIFY_EVT, receive indicate value:");
		}
		esp_log_buffer_hex(TAG, p_data->notify.value, p_data->notify.value_len);
		break;
	case ESP_GATTC_WRITE_DESCR_EVT:
		if (p_data->write.status != ESP_GATT_OK) {
			ESP_LOGE(TAG, "write descr failed, error status = %x",
					p_data->write.status);
			break;
		}
		// esp_ble_gattc_write_char();
		break;
	case ESP_GATTC_SRVC_CHG_EVT:
		ESP_LOGI(TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:");
		esp_log_buffer_hex(TAG, p_data->srvc_chg.remote_bda,  // copy it first?
				sizeof(esp_bd_addr_t));
		break;
	case ESP_GATTC_WRITE_CHAR_EVT:
		ESP_LOGD(TAG, "write char status = %x", p_data->write.status);
		break;
	case ESP_GATTC_DISCONNECT_EVT:
		connect = false;
		endpoint = NULL;
		ESP_LOGI(TAG, "ESP_GATTC_DISCONNECT_EVT, reason = %d",
				p_data->disconnect.reason);
		// This will send GAP indication that it can start scanning
		ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));
		break;
	case ESP_GATTC_CLOSE_EVT:
		ESP_LOGI(TAG, "close, reason %d", p_data->close.reason);
		break;
	default:
		ESP_LOGE(TAG, "Unhandled gattc EVT %d on if %d", event, gattc_if);
		break;
	}
}

void ble_scanner_init(void *handle)
{
	data_handle = handle;
	ESP_LOGI(TAG, "Initializing, running on core %d", xPortGetCoreID());
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
	ESP_ERROR_CHECK(esp_ble_gattc_app_register(PROFILE_APP_ID));
	ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(LOCAL_MTU));
	ESP_LOGI(TAG, "Initialization done");
}
