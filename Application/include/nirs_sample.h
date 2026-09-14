/*
 * Rev0 | Raw sample packet structure
 * architecture.md §4 필드 정의를 그대로 따른다. MCU는 가공 없이 raw만 보존한다.
 */
#ifndef NIRS_SAMPLE_H_
#define NIRS_SAMPLE_H_

#include <stdint.h>
#include "config_app.h"
#include "module_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t timestamp_us;     /* S2 관례: 32bit us 단위 */
	uint32_t seq_num;          /* monotonic sequence number, 누락 검출용 */

	uint16_t raw[NIRS_WAVELENGTH_COUNT]; /* 640/680/950nm raw intensity */

	uint16_t gain;             /* AS7341 gain 설정값 */
	uint16_t integration_time; /* AS7341 integration time 설정값 */
	uint16_t led_duty[NIRS_WAVELENGTH_COUNT]; /* LED PWM duty (파장별) */

	uint8_t battery_pct;       /* TODO(open-item): Battery 모듈 미구현 — 현재 0 고정 */
	module_err_t status;       /* 포화/저신호/센서 오류 flag 등 */

	uint16_t fw_version;
} nirs_sample_t;

#ifdef __cplusplus
}
#endif

#endif /* NIRS_SAMPLE_H_ */
