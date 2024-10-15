#include <lvgl.h>
#include "data.h"

/* Create a pseudo lv_color_t that will produce byte-swapped r5g6b5 */
static inline lv_color_t c_swap(lv_color_t o)
#if 1
{
	return o;
}
#else
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
#endif

/*
 * In case it is needed:
 *  https://forum.lvgl.io/t/get-string-width-in-pixels/3414/4
 * lv_point_t p;
 * _lv_txt_ap_proc(my_str, ctx_text);
 * _lv_txt_get_size(&p, ctxi_text, my_font, 0, 0,
 * 			 LV_COORD_MAX, LV_TXT_FLAG_EXPAND);
 */

lv_obj_t *welcome_label, *update_label, *batt_label, *hr_label;

lv_obj_t *mkframe(lv_obj_t *parent, lv_align_t align,
		int32_t w, int32_t h, lv_color_t bcolour)
{
	lv_obj_t *frame = lv_obj_create(parent);
	lv_obj_set_align(frame, align);
	lv_obj_set_size(frame, w, h);
	lv_obj_set_style_bg_opa(frame, LV_OPA_0, LV_PART_MAIN);
	lv_obj_set_style_border_color(frame, bcolour, LV_PART_MAIN);
	lv_obj_set_style_border_opa(frame, LV_OPA_100, LV_PART_MAIN);
	lv_obj_set_style_border_width(frame, 2, LV_PART_MAIN);
	lv_obj_set_style_radius(frame, 5, LV_PART_MAIN);
	lv_obj_set_style_pad_all(frame, 5, LV_PART_MAIN);
	return frame;
}

lv_obj_t *mklabel(lv_obj_t *parent, lv_obj_t *after)
{
	lv_obj_t *label = lv_label_create(parent);
	lv_obj_set_size(label, lv_pct(100), 30);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(label,
			c_swap(lv_color_hex(0xffffff)), LV_PART_MAIN);
	lv_obj_set_style_bg_color(label, c_swap(lv_color_hex(0x007700)),
				LV_PART_MAIN);
	lv_obj_set_style_bg_opa(label, LV_OPA_100, LV_PART_MAIN);
	lv_obj_set_style_text_align(label, LV_ALIGN_CENTER, LV_PART_MAIN);
	if (after) {
		lv_obj_align_to(label, after, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);
	} else {
		lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
	}
	return label;
}

void display_welcome(lv_display_t* disp)
{
	lv_obj_t *scr = lv_display_get_screen_active(disp);
	lv_obj_set_style_bg_color(scr, c_swap(lv_color_hex(0x000000)),
				LV_PART_MAIN);

	lv_obj_t *lframe = mkframe(scr, LV_ALIGN_LEFT_MID, 460, lv_pct(100),
			c_swap(lv_color_hex(0x770000)));
	lv_obj_t *rframe = mkframe(scr, LV_ALIGN_RIGHT_MID, 70, lv_pct(100),
			c_swap(lv_color_hex(0x007700)));

	welcome_label = lv_label_create(lframe);
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

	update_label = lv_label_create(lframe);
	lv_obj_set_style_text_font(update_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(update_label,
			c_swap(lv_color_hex(0xffffff)), LV_PART_MAIN);
	lv_obj_set_width(update_label, lv_pct(75));
	lv_obj_set_height(update_label, lv_pct(15));
	lv_obj_set_style_bg_color(update_label, c_swap(lv_color_hex(0x007700)),
				LV_PART_MAIN);
	lv_obj_set_style_bg_opa(update_label, LV_OPA_100, LV_PART_MAIN);
	lv_obj_set_style_text_align(update_label, LV_ALIGN_LEFT_MID,
				LV_PART_MAIN);
	lv_obj_align(update_label, LV_ALIGN_TOP_RIGHT, 0, 0);

	batt_label = mklabel(rframe, NULL);
	hr_label = mklabel(rframe, batt_label);
	lv_obj_t *label_2 = mklabel(rframe, hr_label);
	lv_obj_t *label_3 = mklabel(rframe, label_2);
	lv_obj_t *label_4 = mklabel(rframe, label_3);
	lv_obj_t *label_5 = mklabel(rframe, label_4);
	lv_obj_t *label_6 = mklabel(rframe, label_5);

	lv_label_set_text_static(batt_label, "55%");
	lv_label_set_text_static(hr_label, "255");
	lv_label_set_text_static(label_2, "2");
	lv_label_set_text_static(label_3, "3");
	lv_label_set_text_static(label_4, "4");
	lv_label_set_text_static(label_5, "5");
	lv_label_set_text_static(label_6, "6");

}

void display_update(int samples)
{
	data_stash_t stash;

	get_stash(&stash);

	lv_obj_clean(update_label);
	lv_label_set_text(update_label, stash.name);
}
