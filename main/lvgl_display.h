#ifndef _LVGL_DISPLAY_H
#define _LVGL_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

lv_display_t *lvgl_display_init(void);
void lvgl_display_shut(lv_display_t *disp);
void lvgl_display_push(lv_display_t *disp_drv, const lv_area_t *area,
		uint8_t *px_map);

#ifdef __cplusplus
}
#endif

#endif /* _LVGL_DISPLAY_H */
