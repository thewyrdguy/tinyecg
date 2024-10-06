set(srcs
	"tinyecg.c"
	"lvgl_display.c"
	"ble_scanner.c"
	"display.c"
	"data.c"
)

idf_component_register(
	SRCS ${srcs}
	INCLUDE_DIRS "."
)
