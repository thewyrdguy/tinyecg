#ifndef _BLE_SCANNER_H
#define _BLE_SCANNER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _periph {
	void (*callback)(uint8_t *data, size_t datalen);
	void (*start)(void);
	void (*stop)(void);
	const char *name;
	uint16_t srv_uuid;
	uint16_t nchar_uuid;
} periph_t;

typedef struct _periph_listelem {
	const struct _periph_listelem *next;
	const periph_t *periph;
} periph_listelem_t;

void ble_runner(periph_listelem_t *pptrs);

#ifdef __cplusplus
}
#endif

#endif /* _BLE_SCANNER_H */
