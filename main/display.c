#include <string.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include "sampling.h"
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

// Frame outer sizes
#define HEIGHT 240
#define MFWIDTH 460
#define SFWIDTH 70
// Inner update window
#define FWIDTH (SPS / FPS)
#define FHEIGHT (HEIGHT - 10)
#define FMAX (MFWIDTH - 10)
#define RAW_BUF_SIZE (FWIDTH * FHEIGHT \
                * LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))

static uint16_t *rawbuf, *clearbuf;

static lv_obj_t *welcome_label, *update_label, *goodbye_label,
	*batt_label, *hr_label;
static lv_obj_t *mframe, *sframe;
static data_stash_t old_stash = {};

static lv_obj_t *mkframe(lv_obj_t *parent, lv_align_t align,
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

static lv_obj_t *mklabel(lv_obj_t *parent, lv_obj_t *after)
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

static void display_grid(lv_obj_t *scr)
{
	lv_obj_clean(scr);
	lv_obj_set_style_bg_color(scr, c_swap(lv_color_hex(0x000000)),
				LV_PART_MAIN);

	mframe = mkframe(scr, LV_ALIGN_LEFT_MID, MFWIDTH, HEIGHT,
			c_swap(lv_color_hex(0x770000)));
	sframe = mkframe(scr, LV_ALIGN_RIGHT_MID, SFWIDTH, HEIGHT,
			c_swap(lv_color_hex(0x007700)));

	batt_label = mklabel(sframe, NULL);
	hr_label = mklabel(sframe, batt_label);
	lv_obj_t *label_2 = mklabel(sframe, hr_label);
	lv_obj_t *label_3 = mklabel(sframe, label_2);
	lv_obj_t *label_4 = mklabel(sframe, label_3);
	lv_obj_t *label_5 = mklabel(sframe, label_4);
	lv_obj_t *label_6 = mklabel(sframe, label_5);

	uint8_t val = old_stash.lbatt;
	if (val > 99) val = 99;
	lv_label_set_text_fmt(batt_label, "%d%%", val);
	lv_label_set_text_static(hr_label, "---");
	lv_label_set_text_static(label_2, "2");
	lv_label_set_text_static(label_3, "3");
	lv_label_set_text_static(label_4, "4");
	lv_label_set_text_static(label_5, "5");
	lv_label_set_text_static(label_6, "6");
}

void display_init(lv_display_t* disp) {
	rawbuf = heap_caps_malloc(RAW_BUF_SIZE, MALLOC_CAP_DMA);
	clearbuf = heap_caps_malloc(RAW_BUF_SIZE, MALLOC_CAP_DMA);
	assert(rawbuf != NULL && clearbuf != NULL);
	memset(clearbuf, 0, RAW_BUF_SIZE);
	for (int y = 0; y < FHEIGHT; y++) {
		clearbuf[y * FWIDTH] = lv_color_to_u16(lv_color_hex(0x707070));
	}
}

static void display_welcome(lv_obj_t *scr)
{
	lv_obj_clean(scr);
	lv_obj_set_style_bg_color(scr, c_swap(lv_color_hex(0x00003F)),
				LV_PART_MAIN);
	welcome_label = lv_label_create(scr);
	lv_obj_set_width(welcome_label, lv_pct(80));
	lv_obj_set_height(welcome_label, lv_pct(15));
	lv_obj_align(welcome_label, LV_ALIGN_CENTER, 0, 0);
	lv_label_set_long_mode(welcome_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_label_set_text_static(welcome_label,
			"TheWyrdThings https://github.com/thewyrdguy");
	lv_obj_set_style_text_font(welcome_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(welcome_label,
			c_swap(lv_color_hex(0xffffff)), LV_PART_MAIN);
	update_label = lv_label_create(scr);
	lv_obj_set_width(update_label, lv_pct(80));
	lv_obj_set_height(update_label, lv_pct(15));
	lv_obj_align(update_label, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_set_style_text_font(update_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(update_label,
			c_swap(lv_color_hex(0x7F7F7F)), LV_PART_MAIN);
	lv_obj_set_style_text_align(update_label, LV_ALIGN_LEFT_MID,
				LV_PART_MAIN);
}

static void display_stop(lv_obj_t *scr)
{
	lv_obj_clean(scr);
	lv_obj_set_style_bg_color(scr, c_swap(lv_color_hex(0x000000)),
				LV_PART_MAIN);
	goodbye_label = lv_label_create(scr);
	lv_obj_set_width(goodbye_label, lv_pct(80));
	lv_obj_set_height(goodbye_label, lv_pct(15));
	lv_obj_align(goodbye_label, LV_ALIGN_CENTER, 0, 0);
	lv_label_set_long_mode(goodbye_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_label_set_text_static(goodbye_label,
			"Did not find anything, shutting down to save power.");
	lv_obj_set_style_text_font(goodbye_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(goodbye_label,
			c_swap(lv_color_hex(0xff0000)), LV_PART_MAIN);
}

static uint32_t pos = 0;

void display_update(lv_display_t* disp, lv_area_t *where, lv_area_t *clear,
		uint16_t **pbuf, uint16_t **cbuf)
{
	lv_obj_t *scr = lv_display_get_screen_active(disp);
	data_stash_t new_stash;
	int8_t samples[FWIDTH];

	get_stash(&new_stash, FWIDTH, samples);

	if (old_stash.state != new_stash.state) switch (new_stash.state) {
	case state_scanning:
		display_welcome(scr);
		break;
	case state_receiving:
		display_grid(scr);
		break;
	case state_goingdown:
		display_stop(scr);
		break;
	default:
		break;
	}

	switch (new_stash.state) {
	case state_scanning:
		lv_obj_clean(update_label);
		lv_label_set_text_fmt(update_label, "%s: %s",
				new_stash.found ? "Found" : "Scanning",
				new_stash.name);
		break;
	default:
		if (new_stash.heartrate != old_stash.heartrate)
			lv_label_set_text_fmt(hr_label, "%d",
					new_stash.heartrate);
		if (new_stash.lbatt != old_stash.lbatt) {
			uint8_t val = new_stash.lbatt;
			if (val > 99) val = 99;
			lv_label_set_text_fmt(batt_label, "%d%%", val);
		}
		break;
	}
	if (new_stash.state == state_receiving) {
		memset(rawbuf, 0, RAW_BUF_SIZE);
		where->x1 = 5 + pos;
		where->x2 = 4 + FWIDTH + pos;
		where->y1 = 5;
		where->y2 = 5 + FHEIGHT - 1;
		(*pbuf) = rawbuf;
		pos += FWIDTH;
		if (pos >= FMAX) pos = 0;
		clear->x1 = 5 + pos;
		clear->x2 = 4 + FWIDTH + pos;
		clear->y1 = 5;
		clear->y2 = 5 + FHEIGHT - 1;
		(*cbuf) = clearbuf;
	} else {
		pos = 0;
		(*pbuf) = NULL;
	}
	old_stash = new_stash;
}
