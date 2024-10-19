set(srcs
	"tinyecg.c"
	"lvgl_display.c"
	"ble_runner.c"
	"display.c"
	"data.c"
	"hrm.c"
	"pc80b.c"
)

idf_component_register(
	SRCS ${srcs}
	INCLUDE_DIRS "."
)
