#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <lvgl.h>
#include "lvgl_display.h"
#include "ble_scanner.h"
#include "display.h"

#define TAG "tinylcd"

#define LV_TICK_PERIOD_MS 1

static void lv_tick_task(void *arg) {
	lv_tick_inc(LV_TICK_PERIOD_MS);
}

SemaphoreHandle_t displaySemaphore;

static void displayTask(void *pvParameter)
{
	ESP_LOGI(TAG, "Display task is running on core %d", xPortGetCoreID());
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

	display_welcome(disp);

	const TickType_t xFrequency = pdMS_TO_TICKS(2000);
	TickType_t xLastWakeTime = xTaskGetTickCount();
	while (1) {
		vTaskDelayUntil(&xLastWakeTime, xFrequency);
		if (xSemaphoreTake(displaySemaphore, portMAX_DELAY) == pdTRUE) {
			display_update(disp, pdTICKS_TO_MS(xTaskGetTickCount()));
			lv_task_handler();
			xSemaphoreGive(displaySemaphore);
		}
	}
}

void app_main(void)
{
	ESP_LOGI(TAG, "Initializing display task");
	// Run graphic interface task on core 1. Core 0 will be running bluetooth.
	xTaskCreatePinnedToCore(displayTask, "display", 4096*2, NULL, 0, NULL, 1);
	ESP_LOGI(TAG, "Initializing BLE scanner");
	ble_scanner_init(NULL);
}
