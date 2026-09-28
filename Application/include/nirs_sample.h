/*
 * Rev0 | Raw sample packet structure
 * architecture.md §4 필드 정의를 그대로 따른다. MCU는 가공 없이 raw만 보존한다.
 */
#ifndef NIRS_SAMPLE_H_
#define NIRS_SAMPLE_H_

#include <stdint.h>
#include <stdbool.h>
#include "config_app.h"
#include "module_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t timestamp_us;     /* S2 관례: 32bit us 단위 */
	uint32_t seq_num;          /* monotonic sequence number, 누락 검출용 */

	/* raw[NIR_SENSOR_1/2][640/680/950nm] — 보드에 AS7341이 2개(pinmap.md §3, 각각
	 * 독립 I2C) 실장돼 있어 센서별로 raw intensity를 분리 보존한다. */
	uint16_t raw[NIR_SENSOR_COUNT][NIRS_WAVELENGTH_COUNT];

	uint16_t gain;             /* AS7341 gain 설정값 (NIR1/NIR2 공용 고정값) */
	uint16_t integration_time; /* AS7341 integration time 설정값 (NIR1/NIR2 공용 고정값) */
	uint16_t led_duty[NIRS_WAVELENGTH_COUNT]; /* LED PWM duty (파장별) */

	uint8_t battery_pct;       /* TODO(open-item): Battery 모듈 미구현 — 현재 0 고정 */
	module_err_t status;       /* 포화/저신호/센서 오류 flag 등 */

	uint16_t fw_version;

	/* Ambient light 제거(dark-frame subtraction, 2026-09-28) — true면 LED 전부 OFF 상태로
	 * 측정한 다크 프레임(주변광+dark current만 포함, led_duty는 전부 0). 앱/PC가
	 * lit_raw - dark_raw로 순수 LED 신호를 구한다(§4 RAW 보존 원칙 — 펌웨어는 빼지 않는다).
	 * BLE 배칭 레코드에는 이 필드 대신 seq_num의 MSB로 인코드된다(m_ble_proto.c 참고,
	 * BLE_PROTOCOL_VERSION 4). */
	bool is_dark;
} nirs_sample_t;

#ifdef __cplusplus
}
#endif

#endif /* NIRS_SAMPLE_H_ */
