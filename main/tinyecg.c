#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <lvgl.h>
#include "lvgl_display.h"
#include "ble_scanner.h"
#include "display.h"
#include "data.h"

#define TAG "tinylcd"

#define LV_TICK_PERIOD_MS 1

#if defined(CONFIG_TINYECG_FPS_25)
# define FPS 25
#elif defined(CONFIG_TINYECG_FPS_30)
# define FPS 30
#else
# error "Must define FPS, either 25 or 30"
#endif

#if (configTICK_RATE_HZ % FPS)
# error "TICK_RATE_HZ must be a multiple of FPS"
#endif
#if (SPS % FPS)
# error "SPS must be a multiple of FPS"
#endif

periph_t hrm_desc = {
	.next = NULL,
	.srv_uuid = 0x180D,
	.nchar_uuid = 0x2A37,
};

periph_t pc80b_desc = {
	.next = &hrm_desc,
	.name = "PC80B-BLE",
	.srv_uuid = 0xfff0,
	.nchar_uuid = 0xfff1,
	.wchar_uuid = 0xfff2,
};

periph_t *periphs = &pc80b_desc;

static void lv_tick_task(void *arg) {
	lv_tick_inc(LV_TICK_PERIOD_MS);
}

SemaphoreHandle_t displaySemaphore;
SemaphoreHandle_t taskSemaphore;
volatile bool run_display = true;

static void displayTask(void *pvParameter)
{
	ESP_LOGI(TAG, "Display task is running on core %d", xPortGetCoreID());
	assert(xSemaphoreTake(taskSemaphore, portMAX_DELAY) == pdTRUE);
	displaySemaphore = xSemaphoreCreateMutex();
	lv_display_t *disp = lvgl_display_init();
	assert(disp != NULL);

	esp_timer_handle_t periodic_timer;
	ESP_ERROR_CHECK(esp_timer_create(
		&(esp_timer_create_args_t) {
			.callback = &lv_tick_task,
			.name = "periodic_display",
		},
		&periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer,
		LV_TICK_PERIOD_MS * 1000));

	const TickType_t xFrequency = configTICK_RATE_HZ / FPS;
	ESP_LOGI(TAG, "FPS=%d SPS=%d ticks per frame: %lu",
			FPS, CONFIG_TINYECG_SPS, xFrequency);
	TickType_t xLastWakeTime = xTaskGetTickCount();
	while (run_display) {
		vTaskDelayUntil(&xLastWakeTime, xFrequency);
		if (xSemaphoreTake(displaySemaphore, portMAX_DELAY) == pdTRUE) {
			display_update(disp);
			lv_task_handler();
			xSemaphoreGive(displaySemaphore);
		}
	}
	lvgl_display_shut(disp);
	xSemaphoreGive(taskSemaphore);
	vTaskDelete(NULL);
}

void app_main(void)
{
	taskSemaphore = xSemaphoreCreateBinary();
	xSemaphoreGive(taskSemaphore);
	ESP_LOGI(TAG, "Initializing data stash");
	data_init();
	ESP_LOGI(TAG, "Initializing display task");
	// Run graphic interface task on core 1. Core 0 will be running bluetooth.
	xTaskCreatePinnedToCore(displayTask, "display", 4096*2, NULL, 0, NULL, 1);
	ESP_LOGI(TAG, "Running BLE scanner");
	ble_scanner_run(periphs);
	ESP_LOGI(TAG, "BLE scanner returned, signal display to shut");
	report_state(state_goingdown);
	vTaskDelay(pdMS_TO_TICKS(5000));
	run_display = false;
	xSemaphoreTake(taskSemaphore, portMAX_DELAY);
	ESP_LOGI(TAG, "Display task completed, shut down");
	esp_deep_sleep_start();
}
