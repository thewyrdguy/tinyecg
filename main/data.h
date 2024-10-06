#ifndef _DATA_H
#define _DATA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	char name[16];
} data_stash_t;

void report_periph(char const *name);
void get_stash(data_stash_t *newstash);
void data_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _DATA_H */
