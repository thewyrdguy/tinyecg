#ifndef _LVGL_DISPLAY_H
#define _LVGL_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

lv_display_t *lvgl_display_init(void);
void lvgl_display_shut(lv_display_t *disp);

#ifdef __cplusplus
}
#endif

#endif /* _LVGL_DISPLAY_H */
