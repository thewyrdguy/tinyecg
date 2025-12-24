#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <lvgl.h>
#include "lvgl_display.h"
#include "ble_runner.h"
#include "display.h"
#include "data.h"
#include "sampling.h"

#include "localbattery.h"
#include "hrm.h"
#include "pc80b.h"

#define TAG "tinyecg"

#define LV_TICK_PERIOD_MS 1

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
	display_init(disp);

	esp_timer_handle_t periodic_timer;
	ESP_ERROR_CHECK(esp_timer_create(
		&(esp_timer_create_args_t) {
			.callback = &lv_tick_task,
			.name = "periodic_display",
		},
		&periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer,
		LV_TICK_PERIOD_MS * 1000));

	ESP_ERROR_CHECK(gpio_config(&(gpio_config_t) {
				.intr_type = GPIO_INTR_NEGEDGE,
				.mode = GPIO_MODE_INPUT,
				.pin_bit_mask = 1ULL<<CONFIG_HWE_BUTTON_1,
				.pull_down_en = GPIO_PULLDOWN_DISABLE,
				.pull_up_en = GPIO_PULLUP_ENABLE,
			}));

	const TickType_t xFrequency = configTICK_RATE_HZ / FPS;
	ESP_LOGI(TAG, "FPS=%d SPS=%d ticks per frame: %lu",
			FPS, SPS, xFrequency);
	TickType_t xLastWakeTime = xTaskGetTickCount();
	lv_area_t where, clear;
	uint16_t *rawbuf = NULL;
	uint16_t *clearbuf;
	while (run_display) {
		vTaskDelayUntil(&xLastWakeTime, xFrequency);
		if (xSemaphoreTake(displaySemaphore,
					portMAX_DELAY) == pdTRUE) {
			display_update(disp, &where, &clear,
					&rawbuf, &clearbuf);
			lv_task_handler();
			if (rawbuf) {
				lvgl_display_push(disp, &where,
						(uint8_t *)rawbuf);
				lvgl_display_push(disp, &clear,
						(uint8_t *)clearbuf);
			}
			xSemaphoreGive(displaySemaphore);
		}
		int lvl = gpio_get_level(CONFIG_HWE_BUTTON_1);
		if (!lvl) ble_stop();
	}
	lvgl_display_shut(disp);
	xSemaphoreGive(taskSemaphore);
	vTaskDelete(NULL);
}

void app_main(void)
{
	bool pwrdown;
	TaskHandle_t lbatt_task;
	taskSemaphore = xSemaphoreCreateBinary();
	xSemaphoreGive(taskSemaphore);
	ESP_LOGI(TAG, "Initializing data stash");
	data_init();
	ESP_LOGI(TAG, "Initializing display and local battery tasks");
	// Run graphic interface task on core 1.
	// Core 0 will be running bluetooth.
	xTaskCreatePinnedToCore(displayTask, "display", 4096*2,
			NULL, 0, NULL, 1);
	xTaskCreate(localBatteryTask, "display", 4096*2,
		       	NULL, 0, &lbatt_task);
	ESP_LOGI(TAG, "Running BLE scanner");
	pwrdown = ble_runner(
			(const periph_t*[]){&hrm_desc, &pc80b_desc, NULL});
	ESP_LOGI(TAG, "BLE scanner returned, signal display to shut");
	report_state(pwrdown ? state_offbutton : state_notfound);
	vTaskDelete(lbatt_task);
	vTaskDelay(pdMS_TO_TICKS(5000));
	run_display = false;
	xSemaphoreTake(taskSemaphore, portMAX_DELAY);
	ESP_LOGI(TAG, "Display task completed, shut down");
	esp_deep_sleep_start();
}
