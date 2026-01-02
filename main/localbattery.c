#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali_scheme.h>
#include <driver/gpio.h>

#include "sdkconfig.h"
#include "localbattery.h"
#include "data.h"

#define TAG "LBAT"

void localBatteryTask(void *pvParameter)
{
	int pads[] = {CONFIG_HWE_BATTERY_ADC_A, CONFIG_HWE_BATTERY_ADC_B};
	adc_channel_t channel;
	adc_unit_t unit;
	adc_oneshot_unit_handle_t handle;
	adc_cali_handle_t cali;
	int value = 0;
	bool not_present = true;
	int i, pad;

	for (i = 0; i < sizeof(pads) / sizeof(int); i++) {
		pad = pads[i];
		ESP_ERROR_CHECK(adc_oneshot_io_to_channel(
				pad, &unit, &channel));
		ESP_ERROR_CHECK(adc_oneshot_new_unit(
				&(adc_oneshot_unit_init_cfg_t){
					.unit_id = unit,
				},
				&handle));
		ESP_ERROR_CHECK(adc_oneshot_config_channel(handle,
				channel,
				&(adc_oneshot_chan_cfg_t){
					.bitwidth = ADC_BITWIDTH_DEFAULT,
					.atten = ADC_ATTEN_DB_12,
				}));
		ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(
				&(adc_cali_curve_fitting_config_t){
					.unit_id = unit,
					.bitwidth = ADC_BITWIDTH_DEFAULT,
					.atten = ADC_ATTEN_DB_12,
				},
				&cali));
		ESP_ERROR_CHECK(gpio_set_pull_mode(pad, GPIO_PULLDOWN_ONLY));
		ESP_ERROR_CHECK(adc_oneshot_get_calibrated_result(
				handle, cali, channel, &value));
		ESP_ERROR_CHECK(gpio_set_pull_mode(pad, GPIO_FLOATING));
		if (value > 0) {
			not_present = false;
			break;  // Leave the current unit configured
		}
		ESP_ERROR_CHECK(adc_oneshot_del_unit(handle));
	}
	if (not_present) {
		ESP_LOGE(TAG,
			"Neither of confugured pins seem to measure voltage");
		vTaskSuspend(NULL);
	} else {
		ESP_LOGI(TAG, "Using battery ADC on pin %d", pad);
	}

	const TickType_t xFrequency = configTICK_RATE_HZ * 15;
	TickType_t xLastWakeTime = xTaskGetTickCount();
	while (1) {
		ESP_ERROR_CHECK(adc_oneshot_get_calibrated_result(
				handle, cali, channel, &value));
		ESP_LOGI(TAG, "Calibrated result: %d", value);
		/*
		 * The board mesures 1/2 of the battery voltage on the adc
		 * Assuming that it'a a LiPo battery, with the voltage
		 * range from 3.0 to 4.0, we have 1500 mV as empty, and
		 * 2000 as full. In percent, it will be:
		 */
		report_lbatt(value > 1500 ? (value - 1500) / 5 : 0);
		vTaskDelayUntil(&xLastWakeTime, xFrequency);
	}
}
