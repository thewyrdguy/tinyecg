#include <lvgl.h>
#include "data.h"

/* Create a pseudo lv_color_t that will produce byte-swapped r5g6b5 */
static lv_color_t c_swap(lv_color_t o)
{
	/*
	   RRRrr... GGGggg.. BBbbb...
		___  __ __  ___
	   /   \/	 \/   \
	   RRRrrGGG gggBBbbb
			   X
	   gggBBbbb RRRrrGGG
	   \___/\__ __/\___/
	   r	 g-	 b---
	   |	   \		\
	   gggBB000 bbbRRR00 rrGGG000
	 */
	return (lv_color_t) {
	.red =   ((o.green << 3) & 0b11100000) | ((o.blue >> 3)  & 0b00011000),
	.green = ((o.blue << 2)  & 0b11100000) | ((o.red >> 3)   & 0b00011100),
	.blue =  ((o.red << 3)   & 0b11000000) | ((o.green >> 2) & 0b00111000)
	};
}

lv_obj_t *welcome_label, *update_label;

void display_welcome(lv_display_t* disp)
{
	lv_obj_t *scr = lv_display_get_screen_active(disp);
	lv_obj_set_style_bg_color(scr, c_swap(lv_color_hex(0x000000)),
				LV_PART_MAIN);

	lv_obj_t *frame = lv_obj_create(scr);
	lv_obj_set_align(frame, LV_ALIGN_LEFT_MID);
	lv_obj_set_size(frame, 460, 240);
	lv_obj_set_style_bg_color(frame, c_swap(lv_color_hex(0x070707)),
				LV_PART_MAIN);
	lv_obj_set_style_bg_opa(frame, LV_OPA_0, LV_PART_MAIN);
	lv_obj_set_style_border_color(frame, c_swap(lv_color_hex(0x770000)),
			LV_PART_MAIN);
	lv_obj_set_style_border_opa(frame, LV_OPA_100, LV_PART_MAIN);
	lv_obj_set_style_border_width(frame, 2, LV_PART_MAIN);
	lv_obj_set_style_radius(frame, 5, LV_PART_MAIN);
	// lv_obj_add_event_cb(frame, frame_event_cb, LV_EVENT_DRAW_MAIN, NULL);

	welcome_label = lv_label_create(scr);
	lv_obj_set_style_bg_color(welcome_label, c_swap(lv_color_hex(0x000077)),
				LV_PART_MAIN);
	lv_obj_set_style_bg_opa(welcome_label, LV_OPA_100, LV_PART_MAIN);
	lv_label_set_long_mode(welcome_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_label_set_text_static(welcome_label,
			"TheWyrdThings https://github.com/thewyrdguy");
	lv_obj_set_style_text_font(welcome_label, &lv_font_montserrat_28, 0);
	lv_obj_set_width(welcome_label, lv_pct(70));
	lv_obj_set_height(welcome_label, lv_pct(15));
	lv_obj_set_style_text_color(welcome_label,
			c_swap(lv_color_hex(0xffffff)), LV_PART_MAIN);
	lv_obj_align(welcome_label, LV_ALIGN_CENTER, 0, 0);

	update_label = lv_label_create(scr);
	lv_obj_set_style_text_font(update_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(update_label,
			c_swap(lv_color_hex(0xffffff)), LV_PART_MAIN);
	lv_obj_set_width(update_label, lv_pct(45));
	lv_obj_set_height(update_label, lv_pct(15));
	lv_obj_set_style_bg_color(update_label, c_swap(lv_color_hex(0x007700)),
				LV_PART_MAIN);
	lv_obj_set_style_bg_opa(update_label, LV_OPA_100, LV_PART_MAIN);
	lv_obj_set_style_text_align(update_label, LV_ALIGN_LEFT_MID,
				LV_PART_MAIN);
	lv_obj_align(update_label, LV_ALIGN_TOP_RIGHT, 0, 0);
}

void display_update(int samples)
{
	data_stash_t stash;

	get_stash(&stash);

	lv_obj_clean(update_label);
	lv_label_set_text(update_label, stash.name);
}
