set(srcs
	"tinyecg.c"
	"lvgl_display.c"
	"lvgl_demo_ui.c"
)

idf_component_register(
	SRCS ${srcs}
	INCLUDE_DIRS "."
)
