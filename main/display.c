#include <string.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <misc/lv_style.h>
#include "sampling.h"
#include "data.h"

#if 0
/* Create a pseudo lv_color_t that will produce byte-swapped r5g6b5 */
/* We won't use this approach because it breaks antialiasing calculations */
static inline lv_color_t c_swap(lv_color_t o)
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

static void rssi_draw_cb(lv_event_t * e)
{
	lv_obj_t * obj = lv_event_get_target(e);
	int value = (intptr_t)lv_obj_get_user_data(obj);
	lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
	lv_draw_dsc_base_t * base_dsc = lv_draw_task_get_draw_dsc(draw_task);
	if (base_dsc->part != LV_PART_MAIN) return;

	lv_color_t colour = (value > 0) ? lv_color_make(0, 128, 0)
					: lv_color_make(128, 0, 0);
	lv_color_t dim = lv_color_make(32, 32, 32);
	lv_area_t obj_coords;
	lv_obj_get_coords(obj, &obj_coords);

	lv_draw_rect_dsc_t bar;
	lv_draw_rect_dsc_init(&bar);
	bar.border_width = 0;
	lv_area_t a;
	for (int i = 0; i < 5; i++) {
		bar.bg_color = (i <= value) ? colour : dim;
		a.x1 = 0;
		a.x2 = 3;
		a.y1 = 0;
		a.y2 = i * 5 + 3;
		lv_area_align(&obj_coords, &a, LV_ALIGN_BOTTOM_LEFT,
				i * 9 + 8, 0);
		lv_draw_rect(base_dsc->layer, &(bar), &a);
	}
}

static void batt_draw_cb(lv_event_t * e)
{
	lv_obj_t * obj = lv_event_get_target(e);
	int value = (intptr_t)lv_obj_get_user_data(obj);
	value = value * 36 / 100;
	if (value < 0) value = 0;
	if (value > 36) value = 36;
	lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
	lv_draw_dsc_base_t * base_dsc = lv_draw_task_get_draw_dsc(draw_task);
	if (base_dsc->part != LV_PART_MAIN) return;

	lv_color_t colour = (value > 0) ? lv_color_make(0, 128, 0)
					: lv_color_make(128, 0, 0);
	lv_color_t dim = lv_color_darken(colour, 50);
	lv_area_t obj_coords;
	lv_obj_get_coords(obj, &obj_coords);
	lv_area_t a = {
		.x1 = 0,
		.x2 = 40,
		.y1 = 0,
		.y2 = 18,
	};
	lv_area_align(&obj_coords, &a, LV_ALIGN_CENTER, 0, 0);

	lv_draw_rect_dsc_t box;
	lv_draw_rect_dsc_init(&box);
	box.border_width = 3;
	box.border_color = colour;
	box.bg_opa = LV_OPA_0;
	lv_draw_rect(base_dsc->layer, &box, &a);
	a.x1 += 2;
	a.x2 = a.x1 + value;
	a.y1 += 2;
	a.y2 -= 2;
	lv_draw_rect_dsc_t inside;
	lv_draw_rect_dsc_init(&inside);
	inside.border_width = 0;
	inside.bg_color = dim;
	lv_draw_rect(base_dsc->layer, &inside, &a);
}

static void lead_draw_cb(lv_event_t * e)
{
	lv_obj_t * obj = lv_event_get_target(e);
	bool leadoff = (intptr_t)lv_obj_get_user_data(obj);
	lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
	lv_draw_dsc_base_t * base_dsc = lv_draw_task_get_draw_dsc(draw_task);
	if (base_dsc->part != LV_PART_MAIN) return;

	lv_color_t colour = (leadoff) ? lv_color_make(128, 0, 0)
					: lv_color_make(0, 128, 0);
	lv_area_t obj_coords;
	lv_obj_get_coords(obj, &obj_coords);
	int ybase = obj_coords.y1 + (obj_coords.y2 - obj_coords.y1) / 2;
	lv_draw_arc_dsc_t arc;
	lv_draw_arc_dsc_init(&arc);
	arc.color = colour;
	arc.start_angle = 0;
	arc.end_angle = 360;
	arc.width = 8;
	arc.radius = 8;
	arc.center.y = ybase;
	for (int i = 0; i <= 1; i++) {
		if (i) arc.center.x = obj_coords.x2 - 12;
		else arc.center.x = obj_coords.x1 + 12;
		lv_draw_arc(base_dsc->layer, &arc);
	}
	lv_area_t a = {
		.x1 = obj_coords.x1 + 12,
		.x2 = obj_coords.x2 - 12,
		.y1 = ybase - 2,
		.y2 = ybase + 2,
	};
	lv_draw_rect_dsc_t connect;
	lv_draw_rect_dsc_init(&connect);
	connect.border_width = 0;
	connect.bg_color = colour;
	lv_draw_rect(base_dsc->layer, &connect, &a);
	if (leadoff) {  // make a gap in the middle
		int xbase = obj_coords.x1 +
			(obj_coords.x2 - obj_coords.x1) / 2;
		a.x1 = xbase - 4;
		a.x2 = xbase + 4;
		connect.bg_color = lv_color_black();
		lv_draw_rect(base_dsc->layer, &connect, &a);
	}
}

static void mode_draw_cb(lv_event_t * e)
{
	lv_obj_t * obj = lv_event_get_target(e);
	enum mmode_e mode = (enum mmode_e)lv_obj_get_user_data(obj);
	lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
	lv_draw_dsc_base_t * base_dsc = lv_draw_task_get_draw_dsc(draw_task);
	if (base_dsc->part != LV_PART_MAIN) return;

	lv_color_t colour = (mode == mm_detecting) ? lv_color_make(64, 64, 64)
					: lv_color_make(0, 0, 192);
	lv_area_t obj_coords;
	lv_obj_get_coords(obj, &obj_coords);
	int ybase = obj_coords.y1 + (obj_coords.y2 - obj_coords.y1) / 2;

	lv_draw_line_dsc_t line;
	lv_draw_line_dsc_init(&line);
	line.color = colour;
	line.width = 6;
	line.p1.x = obj_coords.x1 + 7;
	line.p1.y = ybase;
	line.p2.x = obj_coords.x2 - 10;
	line.p2.y = ybase;
	lv_draw_line(base_dsc->layer, &line);

	int stopper = (mode == mm_continuous) ? 0 : 6;
	lv_draw_triangle_dsc_t arrow;
	lv_draw_triangle_dsc_init(&arrow);
	arrow.bg_opa = LV_OPA_100;
	arrow.bg_color = colour;
	arrow.p[0].x = obj_coords.x2 - 5 - stopper;
	arrow.p[0].y = ybase;
	arrow.p[1].x = obj_coords.x2 - 17 - stopper;
	arrow.p[1].y = ybase - 10;
	arrow.p[2].x = obj_coords.x2 - 17 - stopper;
	arrow.p[2].y = ybase + 10;
	lv_draw_triangle(base_dsc->layer, &arrow);
	if (stopper) {
		line.width = 6;
		line.p1.y = ybase - 10;
		line.p2.y = ybase + 10;
		line.p1.x = obj_coords.x1 + 7;
		line.p2.x = obj_coords.x1 + 7;
		lv_draw_line(base_dsc->layer, &line);
		line.p1.x = obj_coords.x2 - 7;
		line.p2.x = obj_coords.x2 - 7;
		lv_draw_line(base_dsc->layer, &line);
	}
}

static void stage_draw_cb(lv_event_t * e)
{
	lv_obj_t * obj = lv_event_get_target(e);
	enum mstage_e stage = (enum mstage_e)lv_obj_get_user_data(obj);
	lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
	lv_draw_dsc_base_t * base_dsc = lv_draw_task_get_draw_dsc(draw_task);
	if (base_dsc->part != LV_PART_MAIN) return;

	lv_color_t colour = (stage == ms_stop) ? lv_color_make(192, 0, 0)
						: lv_color_make(0, 0, 192);
	lv_color_t dim = lv_color_make(64, 64, 64);
	lv_area_t obj_coords;
	lv_obj_get_coords(obj, &obj_coords);

	lv_draw_rect_dsc_t bar;
	lv_draw_rect_dsc_init(&bar);
	bar.border_width = 0;
	lv_area_t a;
	for (int i = 0; i < 5; i++) {
		bar.bg_color = (i <= (int)stage) ? colour : dim;
		a.x1 = 0;
		a.x2 = 5;
		a.y1 = 0;
		a.y2 = 5;
		lv_area_align(&obj_coords, &a, LV_ALIGN_LEFT_MID,
				i * 9 + 8, 0);
		lv_draw_rect(base_dsc->layer, &(bar), &a);
	}
}

enum {
	RSSI = 0,
	RBATT,
	HR,
	LEADOFF,
	MMODE,
	MSTAGE,
	LBATT,
	INDICS
};
static void (* const indic_cb[INDICS])(lv_event_t *e) = {
	rssi_draw_cb,
	batt_draw_cb,
	NULL,  // HR
	lead_draw_cb,
	mode_draw_cb,
	stage_draw_cb,
	batt_draw_cb,
};
static lv_obj_t *indic[INDICS] = {};

static lv_obj_t *update_label;

static data_stash_t old_stash = {};

static LV_STYLE_CONST_INIT(frame_style,
	((static lv_style_const_prop_t []){
		LV_STYLE_CONST_PAD_TOP(3),
		LV_STYLE_CONST_PAD_BOTTOM(3),
		LV_STYLE_CONST_PAD_LEFT(3),
		LV_STYLE_CONST_PAD_RIGHT(3),
		LV_STYLE_CONST_RADIUS(5),
		LV_STYLE_CONST_BORDER_WIDTH(2),
		LV_STYLE_CONST_BORDER_COLOR(LV_COLOR_MAKE(64, 64, 64)),
		LV_STYLE_CONST_BORDER_OPA(LV_OPA_100),
		LV_STYLE_CONST_BG_OPA(LV_OPA_0),
		LV_STYLE_CONST_PROPS_END
	}));

static lv_obj_t *mkframe(lv_obj_t *parent, lv_align_t align,
		int32_t w, int32_t h)
{
	lv_obj_t *frame = lv_obj_create(parent);
	lv_obj_add_style(frame, &frame_style, LV_PART_MAIN);
	lv_obj_set_align(frame, align);
	lv_obj_set_size(frame, w, h);
	return frame;
}

static LV_STYLE_CONST_INIT(indic_style,
	((static lv_style_const_prop_t []){
		LV_STYLE_CONST_PAD_TOP(1),
		LV_STYLE_CONST_PAD_BOTTOM(1),
		LV_STYLE_CONST_PAD_LEFT(2),
		LV_STYLE_CONST_PAD_RIGHT(2),
		LV_STYLE_CONST_RADIUS(0),
		LV_STYLE_CONST_BORDER_WIDTH(0),
		LV_STYLE_CONST_TEXT_ALIGN(LV_ALIGN_CENTER),
		LV_STYLE_CONST_TEXT_COLOR(LV_COLOR_MAKE(192, 192, 192)),
		LV_STYLE_CONST_BG_OPA(LV_OPA_0),
		LV_STYLE_CONST_PROPS_END
	}));

static lv_obj_t *mkindic(lv_obj_t *parent, lv_obj_t *after,
		void (*event_cb)(lv_event_t *e))
{
	lv_obj_t *indic = lv_label_create(parent);
	lv_obj_add_style(indic, &indic_style, LV_PART_MAIN);
	lv_obj_set_size(indic, lv_pct(100), 29);
	lv_label_set_text_static(indic, "   ");  // Magic, else no drawing
	if (after) {
		lv_obj_align_to(indic, after, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);
	} else {
		lv_obj_align(indic, LV_ALIGN_TOP_MID, 0, 0);
	}
	if (event_cb) {
		lv_obj_add_event_cb(indic, event_cb, LV_EVENT_DRAW_TASK_ADDED,
				NULL);
		lv_obj_add_flag(indic, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
	}
	return indic;
}

static void display_grid(lv_obj_t *scr)
{
	lv_obj_t *sframe;

	lv_obj_clean(scr);
	lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);

	(void)mkframe(scr, LV_ALIGN_LEFT_MID, MFWIDTH, HEIGHT);
	sframe = mkframe(scr, LV_ALIGN_RIGHT_MID, SFWIDTH, HEIGHT);

	for (int i = 0; i < INDICS; i++) {
		indic[i] = mkindic(sframe, i ? indic[i - 1] : NULL,
				indic_cb[i]);
	}

	memset(&old_stash, 0, sizeof(old_stash));
}

void display_init(lv_display_t* disp) {
	rawbuf = heap_caps_malloc(RAW_BUF_SIZE, MALLOC_CAP_DMA);
	clearbuf = heap_caps_malloc(RAW_BUF_SIZE, MALLOC_CAP_DMA);
	assert(rawbuf != NULL && clearbuf != NULL);
	memset(clearbuf, 0, RAW_BUF_SIZE);
	for (int y = 0; y < FHEIGHT; y++) {
		clearbuf[y * FWIDTH] =
			lv_color_to_u16(lv_color_make(16, 16, 16));
	}
}

static void display_welcome(lv_obj_t *scr)
{
	lv_obj_t *welcome_label;

	lv_obj_clean(scr);
	lv_obj_set_style_bg_color(scr, lv_color_make(0, 0, 64), LV_PART_MAIN);
	welcome_label = lv_label_create(scr);
	lv_obj_set_width(welcome_label, lv_pct(80));
	lv_obj_set_height(welcome_label, lv_pct(15));
	lv_obj_align(welcome_label, LV_ALIGN_CENTER, 0, 0);
	lv_label_set_long_mode(welcome_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_label_set_text_static(welcome_label,
			"TheWyrdThings https://github.com/thewyrdguy");
	//lv_obj_set_style_text_font(welcome_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(welcome_label, lv_color_white(),
			LV_PART_MAIN);
	update_label = lv_label_create(scr);
	lv_obj_set_width(update_label, lv_pct(80));
	lv_obj_set_height(update_label, lv_pct(15));
	lv_obj_align(update_label, LV_ALIGN_TOP_LEFT, 0, 0);
	//lv_obj_set_style_text_font(update_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(update_label, lv_color_make(128, 128, 128),
				LV_PART_MAIN);
	lv_obj_set_style_text_align(update_label, LV_ALIGN_LEFT_MID,
				LV_PART_MAIN);
}

static void display_stop(lv_obj_t *scr)
{
	lv_obj_t *goodbye_label;

	lv_obj_clean(scr);
	lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
	goodbye_label = lv_label_create(scr);
	lv_obj_set_width(goodbye_label, lv_pct(80));
	lv_obj_set_height(goodbye_label, lv_pct(15));
	lv_obj_align(goodbye_label, LV_ALIGN_CENTER, 0, 0);
	lv_label_set_long_mode(goodbye_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_label_set_text_static(goodbye_label,
			"Did not find anything, shutting down to save power.");
	//lv_obj_set_style_text_font(goodbye_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(goodbye_label, lv_color_make(255, 0, 0),
			LV_PART_MAIN);
}

static uint32_t pos = 0;
static int oldvpos = 127;

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
		if (new_stash.rssi != old_stash.rssi) {
			lv_obj_set_user_data(indic[RSSI],
					(void*)((int)new_stash.rssi));
			lv_obj_invalidate(indic[RSSI]);
		}
		if (new_stash.rbatt != old_stash.rbatt) {
			// lv_label_set_text_fmt(indic[RBATT], "%d%%",
			//		new_stash.rbatt);
			lv_obj_set_user_data(indic[RBATT],
					(void*)((int)new_stash.rbatt));
			lv_obj_invalidate(indic[RBATT]);
		}
		if (new_stash.heartrate != old_stash.heartrate) {
			if (new_stash.heartrate) {
				lv_label_set_text_fmt(indic[HR], "%d",
						new_stash.heartrate);
			} else {
				lv_label_set_text_static(indic[HR], " ");
			}
		}
		if (new_stash.mmode != old_stash.mmode) {
			lv_obj_set_user_data(indic[MMODE],
					(void*)((int)new_stash.mmode));
			lv_obj_invalidate(indic[MMODE]);
		}
		if (new_stash.mstage != old_stash.mstage) {
			lv_obj_set_user_data(indic[MSTAGE],
					(void*)((int)new_stash.mstage));
			lv_obj_invalidate(indic[MSTAGE]);
		}
		if (new_stash.leadoff != old_stash.leadoff) {
			//lv_label_set_text_fmt(indic[LEADOFF], "%c",
			//		new_stash.leadoff ? 'X' : 'O');
			lv_obj_set_user_data(indic[LEADOFF],
					(void*)((int)new_stash.leadoff));
			lv_obj_invalidate(indic[LEADOFF]);
		}
		if (new_stash.lbatt != old_stash.lbatt) {
			//lv_label_set_text_fmt(indic[LBATT], "%d%%",
			//		new_stash.lbatt);
			lv_obj_set_user_data(indic[LBATT],
					(void*)((int)new_stash.lbatt));
			lv_obj_invalidate(indic[LBATT]);
		}
		break;
	}
	if (new_stash.state == state_receiving) {
		int ltop, lbot;
		memset(rawbuf, 0, RAW_BUF_SIZE);
		for (int x = 0; x < FWIDTH; x++) {
			int vpos = 120 - samples[x];
			if (vpos > FHEIGHT - 1) vpos = FHEIGHT - 1;
			if (vpos < 0) vpos = 0;
			if (oldvpos < vpos) {  // old is on the top
				ltop = oldvpos;
				lbot = vpos;
			} else {
				ltop = vpos;
				lbot = oldvpos;
			}
			for (int y = ltop; y <= lbot; y++) {
				rawbuf[x + (y * FWIDTH)] = lv_color_to_u16(
						lv_color_make(0, 255, 0));
			}
			oldvpos = vpos;
		}
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
