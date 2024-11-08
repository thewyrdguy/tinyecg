#ifndef _DISPLAY_H
#define _DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

void display_init(lv_display_t* lvgl_display);
void display_update(lv_display_t* disp);

#ifdef __cplusplus
}
#endif

#endif /* _DISPLAY_H */
