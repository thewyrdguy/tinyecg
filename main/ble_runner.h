#ifndef _BLE_RUNNER_H
#define _BLE_RUNNER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	NOTIFY,
	WRITE,
} char_type_t;

typedef struct {
	uint16_t uuid;
	char_type_t type;
	void (*callback)(uint8_t *data, size_t datalen);
} characteristic_t;

typedef struct {
	uint16_t uuid;
	const characteristic_t *chars;
} service_t;

typedef struct {
	const service_t *srvlist;
	const char *name;
	uint16_t uuid;
	uint16_t delay;
	void (*init)(void);
	void (*start)(void);
	void (*stop)(void);
} periph_t;

void ble_write(uint16_t handle, uint8_t *data, size_t datalen);
void ble_runner(const periph_t *periphs[]);

#ifdef __cplusplus
}
#endif

#endif /* _BLE_RUNNER_H */
