idf_component_register(
	SRCS esp_lcd_panel_rm67162.c
	INCLUDE_DIRS "."
	REQUIRES "driver" "esp_driver_gpio" "esp_driver_spi" "esp_lcd"
	PRIV_REQUIRES "esp_mm" "esp_psram" "esp_pm"
)
