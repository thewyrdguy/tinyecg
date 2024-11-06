#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali_scheme.h>

#include "sdkconfig.h"
#include "localbattery.h"
#include "data.h"

#define TAG "LBAT"

void localBatteryTask(void *pvParameter)
{
	adc_channel_t channel;
	adc_unit_t unit;
	adc_oneshot_unit_handle_t handle;
	adc_cali_handle_t cali;
	int value = 0;

	ESP_ERROR_CHECK(adc_oneshot_io_to_channel(CONFIG_HWE_BATTERY_ADC,
			       	&unit, &channel));
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&(adc_oneshot_unit_init_cfg_t){
					.unit_id = unit,
					.ulp_mode = ADC_ULP_MODE_DISABLE,
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
		report_lbatt((value - 1500) / 5);
		vTaskDelayUntil(&xLastWakeTime, xFrequency);
	}
}
