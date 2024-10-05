#include <lvgl.h>

/* Create a pseudo lv_color_t that will produce byte-swapped r5g6b5 */
static lv_color_t c_swap(lv_color_t o)
{
    /*
       RRRrr... GGGggg.. BBbbb...
        ___  __ __  ___
       /   \/     \/   \
       RRRrrGGG gggBBbbb
               X
       gggBBbbb RRRrrGGG
       \___/\__ __/\___/
       r     g-     b---
       |       \        \
       gggBB000 bbbRRR00 rrGGG000
     */
    return (lv_color_t) {
	.red =   ((o.green << 3) & 0b11100000) | ((o.blue >> 3)  & 0b00011000),
	.green = ((o.blue << 2)  & 0b11100000) | ((o.red >> 3)   & 0b00011100),
	.blue =  ((o.red << 3)   & 0b11000000) | ((o.green >> 2) & 0b00111000)
    };
}

void display_welcome(lv_display_t* disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_t *label = lv_label_create(scr);

    lv_label_set_text(label, "TheWyrdThings: tinyecg initializing");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, c_swap(lv_color_hex(0xffffff)),
				LV_PART_MAIN);
    lv_obj_set_width(label, lv_pct(80));
    lv_obj_set_style_bg_color(scr, c_swap(lv_color_hex(0x000077)),
			      LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

void display_update(lv_display_t* disp, long int millis)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_t *label = lv_label_create(scr);

    lv_label_set_text_fmt(label, "TheWyrdThings: tinyecg update at %ld", millis);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, c_swap(lv_color_hex(0xffffff)),
				LV_PART_MAIN);
    lv_obj_set_width(label, lv_pct(80));
    lv_obj_set_style_bg_color(scr, c_swap(lv_color_hex(0x000077)),
			      LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}
