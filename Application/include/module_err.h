/*
 * Rev0 | Shared error type (S2 Rev22 패턴 계승)
 * 모든 모듈은 이 하나의 에러코드 타입만 사용한다. 모듈별 별도 enum 금지.
 * codingstandard.md §5 참고.
 */
#ifndef MODULE_ERR_H_
#define MODULE_ERR_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	MODULE_ERR_OK = 0,

	/* Generic */
	MODULE_ERR_INVALID_PARAM,
	MODULE_ERR_TIMEOUT,
	MODULE_ERR_NOT_INITIALIZED,
	MODULE_ERR_NO_MEMORY,

	/* I2C task (센서/LED 획득) */
	MODULE_ERR_I2C_TIMEOUT,
	MODULE_ERR_I2C_NACK,
	MODULE_ERR_SENSOR_SATURATION,
	MODULE_ERR_SENSOR_LOW_SIGNAL,
	MODULE_ERR_RING_BUFFER_OVERFLOW,

	/* BLE task */
	MODULE_ERR_BLE_DISCONNECTED,
	MODULE_ERR_BLE_TX_FAILED,
	MODULE_ERR_BLE_RETRY_EXCEEDED,

	/* Ctrl / system */
	MODULE_ERR_WATCHDOG_TRIGGERED,

	/* TODO(open-item): Battery/Thermal 관련 에러코드는 해당 모듈 구현 시 추가한다 */

	MODULE_ERR_UNKNOWN,
} module_err_t;

#ifdef __cplusplus
}
#endif

#endif /* MODULE_ERR_H_ */
