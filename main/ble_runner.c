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
#include <nvs.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <esp_log.h>

#include "ble_runner.h"
#include "data.h"

#define TAG "ble_runner"

#define SCAN_DURATION 50
#define LOCAL_MTU 512

SemaphoreHandle_t btSemaphore;

static const periph_t **pparr, *pp;

static uint16_t saved_gattc_if = ESP_GATT_IF_NONE;

uint16_t gattc_conn_id;
esp_bd_addr_t gattc_remote_bda;
esp_ble_addr_type_t gattc_ble_addr_type;

typedef struct _srv_profile {
	struct _srv_profile *next;
	const service_t *srvdesc;
	uint16_t start_handle;
	uint16_t end_handle;
} srv_profile_t;
static srv_profile_t *srvprofs = NULL;

typedef struct _handle {
	struct _handle *next;
	uint16_t handle;
	bool is_notify;
	void (*callback)(uint8_t *data, size_t datalen);
	srv_profile_t *sp;
} handle_t;
static handle_t *handles = NULL;

static TimerHandle_t read_rssi_timer;
static void readRssiCallback(TimerHandle_t xTimer)
{
	ESP_LOGD(TAG, "readRssiCallback running");
	esp_err_t err = esp_ble_gap_read_rssi(gattc_remote_bda);
	/* Read may happen after disconnect but before kill */
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Read RSSI: ignore error %d ", err);
	}
}
static TimerHandle_t connect_timer;
static void initiateConnectCallback(TimerHandle_t xTimer)
{
	ESP_LOGI(TAG, "Initiating connect after delay");
	esp_ble_gattc_open(saved_gattc_if, gattc_remote_bda,
			gattc_ble_addr_type, true);
}

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
		ESP_LOGI(TAG, "Initiate scanning");
		report_state(state_scanning);
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
		switch (param->scan_rst.search_evt) {
		case ESP_GAP_SEARCH_INQ_RES_EVT:
			ESP_LOG_BUFFER_HEX_LEVEL(TAG,
					param->scan_rst.bda, 6,
				       	ESP_LOG_DEBUG);
			ESP_LOGD(TAG, "Adv Data Len %d, Scan Response Len %d",
					param->scan_rst.adv_data_len,
					param->scan_rst.scan_rsp_len);
			adv_name = esp_ble_resolve_adv_data(
					param->scan_rst.ble_adv,
					ESP_BLE_AD_TYPE_NAME_CMPL,
					&adv_name_len);
			ESP_LOGD(TAG, "Device Name Len %d", adv_name_len);
			ESP_LOG_BUFFER_CHAR_LEVEL(TAG, adv_name, adv_name_len,
					ESP_LOG_DEBUG);

			if (adv_name_len) {
				report_periph((char*)adv_name, adv_name_len);
			} else {
				char buf[ESP_BD_ADDR_LEN * 3 + 1];
				for (int i = 0; i < ESP_BD_ADDR_LEN; i++)
					sprintf(buf+(i*3), "%02x:",
						param->scan_rst.bda[i]);
				buf[sizeof(buf) - 2] = '\0';
				report_periph(buf, sizeof(buf) - 1);
			}

			adv_srv = esp_ble_resolve_adv_data(
					param->scan_rst.ble_adv,
					ESP_BLE_AD_TYPE_16SRV_CMPL,
					&adv_srv_len);
			ESP_LOGD(TAG, "Device Srv Len %d", adv_srv_len);
			if (pp) {
				ESP_LOGI(TAG, "Ignoring because have already");
				break;
			}
			for (int i = 0; pparr[i]; i++) {
				if ((pparr[i])->name
				    && (strlen((pparr[i])->name) == adv_name_len)
				    && (strncmp((char *)adv_name, (pparr[i])->name,
						adv_name_len) == 0)) {
					pp = pparr[i];
					break;
				}
				if (adv_srv_len != sizeof(uint16_t)) {
					continue;
				}
				adv_srv_uuid = adv_srv[0] + (adv_srv[1] << 8);
				ESP_LOGD(TAG, "Device Srv uuid %04x",
						adv_srv_uuid);
				if (adv_srv_uuid == (pparr[i])->uuid) {
					pp = pparr[i];
					break;
				}
			}
			if (pp) {
				ESP_LOGI(TAG, "Found %s, stop scan & connect",
					adv_name_len ? (char*)adv_name
							: "noname");
				esp_ble_gap_stop_scanning();
				report_found(true);
				memcpy(&gattc_remote_bda, param->scan_rst.bda,
					sizeof(esp_bd_addr_t));
				gattc_ble_addr_type =
					param->scan_rst.ble_addr_type;
				xTimerChangePeriod(connect_timer,
					configTICK_RATE_HZ *
						((pp->delay) ? pp->delay : 1),
						0);
				xTimerStart(connect_timer, 0);
			}
			break;
		case ESP_GAP_SEARCH_INQ_CMPL_EVT:
			ESP_LOGI(TAG, "Scan completed");
			if (!pp) {
				ESP_LOGI(TAG, "Found nothing");
				xSemaphoreGive(btSemaphore);
			}
			break;
		default:
			ESP_LOGE(TAG, "Unhandled GAP search EVT %x",
					param->scan_rst.search_evt);
		}
		break;
	case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
		if (param->scan_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
			ESP_LOGI(TAG, "Scan stopped");
		} else {
			ESP_LOGE(TAG, "scan stop failed, error status = %x",
					param->scan_stop_cmpl.status);
		}
		break;
	case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
		if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
			ESP_LOGD(TAG, "Stop adv successfully");
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
		ESP_LOGD(TAG, "packet length updated: rx = %d, "
				"tx = %d, status = %d",
				param->pkt_data_length_cmpl.params.rx_len,
				param->pkt_data_length_cmpl.params.tx_len,
				param->pkt_data_length_cmpl.status);
		break;
	case ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT:
		ESP_LOGD(TAG, "rssi read: %d status %d",
				param->read_rssi_cmpl.rssi,
				param->read_rssi_cmpl.status);
		int8_t rssi = (90 + param->read_rssi_cmpl.rssi) / 10;
		if (rssi < 0) rssi = 0;
		if (rssi > 4) rssi = 4;
		report_rssi((uint8_t)rssi);
		break;
	default:
		ESP_LOGE(TAG, "Unhandled GAP EVT %x", event);
		break;
	}
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
		esp_ble_gattc_cb_param_t *p_data)
{
	handle_t *handle;  // used in a couple of case sections
	static esp_ble_scan_params_t scan_params = {
		.scan_type = BLE_SCAN_TYPE_PASSIVE,
		.own_addr_type = BLE_ADDR_TYPE_PUBLIC,
		.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
		.scan_interval = 0x50,
		.scan_window = 0x30,
		.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
	};
	// If multiple profiles, we would select the one and call its callback
	switch (event) {
	case ESP_GATTC_REG_EVT:
		if (p_data->reg.app_id != 0) {
			ESP_LOGE(TAG, "REG_EVT gave us wrong app_id %d",
					p_data->reg.app_id);
			return;
		}
		if (p_data->reg.status == ESP_GATT_OK) {
			ESP_LOGD(TAG, "ESP_GATTC_REG_EVT app_id %04x if %d",
					p_data->reg.app_id, gattc_if);
			saved_gattc_if = gattc_if;
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
		gattc_conn_id = p_data->connect.conn_id;
		memcpy(&gattc_remote_bda, p_data->connect.remote_bda,
				sizeof(esp_bd_addr_t));
		ESP_LOGI(TAG, "REMOTE BDA:");
		ESP_LOG_BUFFER_HEX_LEVEL(TAG, gattc_remote_bda,
				sizeof(esp_bd_addr_t), ESP_LOG_INFO);
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
					NULL);
		} else {
			ESP_LOGE(TAG, "discover service failed, status %d",
					p_data->dis_srvc_cmpl.status);
		}
		break;
	case ESP_GATTC_CFG_MTU_EVT:
		if (p_data->cfg_mtu.status == ESP_GATT_OK) {
			ESP_LOGI(TAG, "MTU after configuration: "
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
		switch (p_data->search_res.srvc_id.uuid.len) {
		case ESP_UUID_LEN_16:
			ESP_LOGI(TAG, "uuid16: %04x",
				p_data->search_res.srvc_id.uuid.uuid.uuid16);
			break;
		case ESP_UUID_LEN_32:
			ESP_LOGI(TAG, "uuid32: %08lx",
				p_data->search_res.srvc_id.uuid.uuid.uuid32);
			break;
		case ESP_UUID_LEN_128:
			char buf[ESP_UUID_LEN_128 * 2 + 1];
			for (int i = 0; i < ESP_UUID_LEN_128; i++) {
				sprintf(buf+(i*2), "%02x",
					p_data->search_res.srvc_id.uuid
						.uuid.uuid128[i]);
			}
			ESP_LOGI(TAG, "uuid128: %s", buf);
			break;
		default:
			break;
		}
		ESP_LOGI(TAG, "conn %x primary %d start hdl %d end hdl %d "
				"current handle value %d",
				p_data->search_res.conn_id,
				p_data->search_res.is_primary,
				p_data->search_res.start_handle,
				p_data->search_res.end_handle,
				p_data->search_res.srvc_id.inst_id);
		for (const service_t *srv = pp->srvlist; srv->uuid; srv++) {
			if (p_data->search_res.srvc_id.uuid.len
					== ESP_UUID_LEN_16 &&
			    p_data->search_res.srvc_id.uuid.uuid.uuid16
			    		== srv->uuid) {
				ESP_LOGI(TAG, "Service uuid %04x discoverd",
						srv->uuid);
				srv_profile_t *srvprof =
					malloc(sizeof(srv_profile_t));
				assert(srvprof != NULL);
				srvprof->next = srvprofs;
				srvprofs = srvprof;
				srvprof->srvdesc = srv;
				srvprof->start_handle =
					p_data->search_res.start_handle;
				srvprof->end_handle =
					p_data->search_res.end_handle;
			}
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
			ESP_LOGD(TAG, "Service information came from remote device");
		} else if (p_data->search_cmpl.searched_service_source
				== ESP_GATT_SERVICE_FROM_NVS_FLASH) {
			ESP_LOGD(TAG, "Service information came from NVS");
		} else {
			ESP_LOGW(TAG, "Unknown service information source");
		}
		if (!srvprofs) {
			ESP_LOGI(TAG, "Search complete w/o success, close");
			if (esp_ble_gattc_close(gattc_if,
				p_data->search_cmpl.conn_id) != ESP_GATT_OK) {
				ESP_LOGE(TAG, "gattc_close error");
			}
			break;
		}
		for (srv_profile_t *sp = srvprofs; sp; sp = sp->next) {
			uint16_t count = 0;
			if (esp_ble_gattc_get_attr_count(
					gattc_if,
					p_data->search_cmpl.conn_id,
					ESP_GATT_DB_CHARACTERISTIC,
					sp->start_handle,
					sp->end_handle,
					0,  // "Invalid handle"?
					&count) != ESP_GATT_OK) {
				ESP_LOGE(TAG,
					"esp_ble_gattc_get_attr_count error");
			}
			ESP_LOGI(TAG, "%hu characteristics found", count);

			if (!count) continue;

			esp_gattc_char_elem_t *char_elem_res =
				(esp_gattc_char_elem_t *)malloc(
					sizeof(esp_gattc_char_elem_t) * count);
			assert(char_elem_res != NULL);
			if (esp_ble_gattc_get_all_char(
					gattc_if,
					p_data->search_cmpl.conn_id,
					sp->start_handle,
					sp->end_handle,
					char_elem_res,
					&count,
					0) != ESP_GATT_OK) {
				ESP_LOGE(TAG, "get_all_char error");
				free(char_elem_res);
				continue;
			}
			for (int i = 0; i < count; i++) {
				ESP_LOGI(TAG,
					"%d: %04x hdl:%d r:%s, w:%s+%s, n:%s",
					i,
					char_elem_res[i].uuid.uuid.uuid16,
					char_elem_res[i].char_handle,
					(char_elem_res[i].properties
					& ESP_GATT_CHAR_PROP_BIT_READ)
						? "y" : "n",
					(char_elem_res[i].properties
					& ESP_GATT_CHAR_PROP_BIT_WRITE_NR)
						? "y" : "n",
					(char_elem_res[i].properties
					& ESP_GATT_CHAR_PROP_BIT_WRITE)
						? "y" : "n",
					(char_elem_res[i].properties
					& ESP_GATT_CHAR_PROP_BIT_NOTIFY)
						? "y" : "n"
				);
				for (const characteristic_t *chr =
						sp->srvdesc->chars;
					       	chr->uuid; chr++) {
					if (char_elem_res[i].uuid.uuid.uuid16
                                       	        	== chr->uuid) {
						handle_t *handle = malloc(
							sizeof(handle_t));
						assert(handle != NULL);
						handle->next = handles;
						handles = handle;
						handle->handle =
							char_elem_res[i]
							.char_handle;
						handle->is_notify =
							chr->type == NOTIFY;
						handle->callback =
							chr->callback;
						handle->sp = sp;
					}
				}
			}
			free(char_elem_res);
		}

		for (handle_t *handle = handles; handle;
					handle = handle->next) {
			if (handle->is_notify) {
				ESP_LOGI(TAG, "Registering for notify");
				esp_ble_gattc_register_for_notify(
					gattc_if,
					gattc_remote_bda,
					handle->handle);
			} else {
				ESP_LOGI(TAG, "Returning write hdl");
				handle->callback(
					(uint8_t*)&handle->handle,
					sizeof(uint16_t));
			}
		}
		if (xTimerStart(read_rssi_timer, 0) != pdPASS) {
			ESP_LOGE(TAG, "Failed to start read rssi timer");
		}
		if (pp->start) (pp->start)();
		break;
	case ESP_GATTC_REG_FOR_NOTIFY_EVT:
		ESP_LOGD(TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT");
		uint16_t count = 0;
		uint16_t notify_enable = 1;
		for (handle = handles; handle; handle = handle->next) {
			if (handle->handle == p_data->reg_for_notify.handle)
				break;
		}
		if (!handle) {
			ESP_LOGE(TAG, "Unexpected handle %04hx",
					p_data->reg_for_notify.handle);
			break;
		}
		if (!handle->is_notify) {
			ESP_LOGE(TAG, "Unexpected handle %04hx for notify",
					p_data->reg_for_notify.handle);
			break;
		}
		if (esp_ble_gattc_get_attr_count(
				gattc_if,
				gattc_conn_id,
				ESP_GATT_DB_DESCRIPTOR,
				handle->sp->start_handle,
				handle->sp->end_handle,
				handle->handle,
				&count) != ESP_GATT_OK) {
			ESP_LOGE(TAG, "esp_ble_gattc_get_attr_count error");
			break;
		}
		ESP_LOGI(TAG, "%hu descriptors found", count);
		if (count == 0) {
			ESP_LOGE(TAG, "zero descriptors found");
			break;
		}
		esp_gattc_descr_elem_t *descr_elem_result = malloc(
				sizeof(esp_gattc_descr_elem_t) * count);
		if (!descr_elem_result) {
			ESP_LOGE(TAG, "no memory for descriptors");
			break;
		}
		if (esp_ble_gattc_get_all_descr(
				gattc_if,
				gattc_conn_id,
				handle->handle,
				descr_elem_result,
				&count,
				0) != ESP_GATT_OK) {
			ESP_LOGE(TAG, "get_all_descr error");
			free(descr_elem_result);
			break;
		}
		uint16_t client_config_handle = 0;  // real handle cannot be 0?
		for (int i = 0; i < count; i++) {
			ESP_LOGI(TAG, "%d: %04x",
					i,
					descr_elem_result[i].uuid.uuid.uuid16
				);
			if (descr_elem_result[i].uuid.uuid.uuid16 ==
					ESP_GATT_UUID_CHAR_CLIENT_CONFIG) {
				client_config_handle =
					descr_elem_result[i].handle;
			}
		}
		free(descr_elem_result);
		if (!client_config_handle) {
			ESP_LOGE(TAG, "did not find clinet config descriptor");
			break;
		}
		if (esp_ble_gattc_write_char_descr(
				gattc_if,
				gattc_conn_id,
				client_config_handle,
				sizeof(notify_enable),
				(uint8_t*)&notify_enable,
				ESP_GATT_WRITE_TYPE_RSP,
				ESP_GATT_AUTH_REQ_NONE) != ESP_GATT_OK) {
			ESP_LOGE(TAG, "error esp_ble_gattc_write_char_descr");
		} else {
			ESP_LOGI(TAG, "Requested subscription");
			report_state(state_receiving);
		}
		break;
	case ESP_GATTC_NOTIFY_EVT:
		ESP_LOGD(TAG, "Receive %s (%d bytes) from handle %04hx",
			(p_data->notify.is_notify) ? "notify" : "indicate",
			p_data->notify.value_len,
			p_data->notify.handle);
		for (handle = handles; handle; handle = handle->next) {
			if (handle->handle == p_data->notify.handle)
				break;
		}
		if (!handle) {
			ESP_LOGE(TAG, "Unexpected handle %04hx",
					p_data->notify.handle);
			break;
		}
		if (!handle->is_notify) {
			ESP_LOGE(TAG, "Unexpected handle %04hx for notify",
					p_data->notify.handle);
			break;
		}
		handle->callback(p_data->notify.value,
					p_data->notify.value_len);
		break;
	case ESP_GATTC_WRITE_DESCR_EVT:
		if (p_data->write.status != ESP_GATT_OK) {
			ESP_LOGE(TAG, "write descr failed, error status = %x",
					p_data->write.status);
			break;
		}
		ESP_LOGI(TAG, "Write descr success");
		break;
	case ESP_GATTC_SRVC_CHG_EVT:
		ESP_LOGI(TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:");
		ESP_LOG_BUFFER_HEX_LEVEL(TAG, p_data->srvc_chg.remote_bda,
				sizeof(esp_bd_addr_t), ESP_LOG_INFO);
		break;
	case ESP_GATTC_WRITE_CHAR_EVT:
		ESP_LOGD(TAG, "write char status = %x", p_data->write.status);
		break;
	case ESP_GATTC_DISCONNECT_EVT:
		ESP_LOGI(TAG, "Disconnect, reason = %d",
				p_data->disconnect.reason);
		if (xTimerIsTimerActive(read_rssi_timer) != pdFALSE) {
			xTimerStop(read_rssi_timer, 0);
		}
		if (pp && (pp->stop)) (pp->stop)();
		pp = NULL;
		for (handle_t *handle = handles; handle;) {
			handle_t *old = handle;
			handle = handle->next;
			free(old);
		}
		handles = NULL;
		for (srv_profile_t *sp = srvprofs; sp;) {
			srv_profile_t *old = sp;
			sp = sp->next;
			free(old);
		}
		srvprofs = NULL;
		report_found(false);
		// This will send GAP indication that it can start scanning
		ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));
		break;
	case ESP_GATTC_CLOSE_EVT:
		ESP_LOGI(TAG, "close, reason %d", p_data->close.reason);
		break;
	case ESP_GATTC_UNREG_EVT:
		ESP_LOGI(TAG, "Unregister event");
		break;
	default:
		ESP_LOGE(TAG, "Unhandled gattc EVT %d on if %d", event, gattc_if);
		break;
	}
}

void ble_write(uint16_t handle, uint8_t *data, size_t datalen)
{
	ESP_LOGD(TAG, "ble_write handle 0x%04hx", handle);
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, datalen, ESP_LOG_DEBUG);
	ESP_ERROR_CHECK(esp_ble_gattc_write_char(
			saved_gattc_if,
			gattc_conn_id,
			handle,
			datalen,
			data,
			ESP_GATT_WRITE_TYPE_RSP,
			ESP_GATT_AUTH_REQ_NONE));
}

void ble_runner(const periph_t *periphs[])
{
	pparr = periphs;
	for (int i = 0; pparr[i]; i++) {
		if (pparr[i]->init) (pparr[i]->init)();
	}
	btSemaphore = xSemaphoreCreateBinary();
	ESP_LOGI(TAG, "Initializing, running on core %d", xPortGetCoreID());
	read_rssi_timer = xTimerCreate(
				"Read RSSI",
				configTICK_RATE_HZ * 5,
				pdTRUE,  // repeating timer
				NULL,
				readRssiCallback
			);
	connect_timer = xTimerCreate(
				"Connect",
				1,  // will be set before start
				pdFALSE,  // one shot timer
				NULL,
				initiateConnectCallback
			);
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
	ESP_ERROR_CHECK(esp_ble_gattc_app_register(0));
	ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(LOCAL_MTU));
	ESP_LOGI(TAG, "Initialization done");

	xSemaphoreTake(btSemaphore, portMAX_DELAY);

	ESP_LOGI(TAG, "Nothing found during scan, powering down");
	esp_bluedroid_disable();
	esp_bluedroid_deinit();
	esp_bt_controller_disable();
	esp_bt_controller_deinit();
	nvs_flash_deinit();
}
