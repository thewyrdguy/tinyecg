#ifndef _BLE_SCANNER_H
#define _BLE_SCANNER_H

#ifdef __cplusplus
extern "C" {
#endif


typedef struct _periph {
	struct _periph *next;
	void (*callback)(uint8_t *data, size_t datalen,
			uint8_t *response, size_t *resplen);
	char *name;
	uint16_t srv_uuid;
	uint16_t nchar_uuid;
	uint16_t wchar_uuid;
	uint16_t nbatt_uuid;
} periph_t;

void ble_scanner_run(periph_t *periphs);

#ifdef __cplusplus
}
#endif

#endif /* _BLE_SCANNER_H */
