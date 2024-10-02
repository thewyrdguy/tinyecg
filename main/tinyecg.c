#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <lvgl.h>
#include "lvgl_display.h"

#define TAG "tinylcd"

extern void example_lvgl_demo_ui(lv_display_t *disp);  // TODO remove this

#define LV_TICK_PERIOD_MS 1

static void lv_tick_task(void *arg) {
	lv_tick_inc(LV_TICK_PERIOD_MS);
}

SemaphoreHandle_t displaySemaphore;

static void displayTask(void *pvParameter)
{
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

	example_lvgl_demo_ui(disp);

	while (1) {
		vTaskDelay(pdMS_TO_TICKS(10));
		if (pdTRUE == xSemaphoreTake(displaySemaphore, portMAX_DELAY)) {
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
}
