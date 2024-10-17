#ifndef _DATA_H
#define _DATA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	_state_uninitialized = 0,
	state_scanning,
	state_receiving,
	state_goingdown,
} state_t;

typedef struct {
	state_t state;
	char name[16];
} data_stash_t;

void report_state(state_t state);
void report_periph(char const *name, size_t len);
void get_stash(data_stash_t *newstash);
void data_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _DATA_H */
