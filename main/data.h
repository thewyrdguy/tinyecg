#ifndef _DATA_H
#define _DATA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum mstage_e {
	ms_detecting = 0,
	ms_preparing = 1,
	ms_measuring = 2,
	ms_analyzing = 3,
	ms_result = 4,
	ms_stop = 5,
	ms_last
};

enum mmode_e {
	mm_detecting = 0,
	mm_fast = 1,
	mm_continuous = 2,
	mm_last
};

enum channel_e {
	ch_detecting = 0,
	ch_internal = 1,
	ch_external = 2,
	ch_last
};

enum datatype_e {
	dt_ecgdata = 1,
	dt_ecgresult = 2,
	dt_last
};

enum state_e {
	_state_uninitialized = 0,
	state_scanning,
	state_receiving,
	state_goingdown,
};

typedef struct _ds {
	uint16_t energy;
	uint16_t volume;
	uint8_t gain;
	enum mstage_e mstage;
	enum mmode_e mmode;
	enum channel_e channel;
	enum datatype_e datatype;
	bool leadoff;
	uint8_t heartrate;
	uint8_t rssi;
#define DYNSIZE (offsetof(struct _ds, lbatt) - offsetof(struct _ds, energy))
	uint8_t lbatt;
	uint8_t rbatt;
	bool overrun;
	bool underrun;
	enum state_e state;
	char name[32];
} data_stash_t;

void report_state(enum state_e state);
void report_periph(char const *name, size_t len);
void report_jumbo(data_stash_t *ds, int num, int8_t *samples);
void report_rbatt(uint8_t rbatt);
void report_lbatt(uint8_t lbatt);
void get_stash(data_stash_t *newstash, size_t num, int8_t *samples);
void data_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _DATA_H */
