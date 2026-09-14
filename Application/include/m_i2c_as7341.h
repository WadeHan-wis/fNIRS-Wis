/*
 * Rev0 skeleton | Rev1 implementation target (현 최우선 목표: architecture.md §0)
 * Module   : m_i2c_as7341
 * Task     : I2C Task 컨텍스트에서만 호출 (I2C blocking, ISR 호출 금지)
 * Depends  : devicetree I2C 노드 (보드 확정 전까지 보류)
 *
 * AS7341 (ams OSRAM)로부터 파장별 raw intensity를 읽는다.
 * gain/integration time은 초기 검증 단계에서 고정값 사용 (architecture.md §4/§5).
 */
#ifndef M_I2C_AS7341_H_
#define M_I2C_AS7341_H_

#include <stdint.h>
#include "module_err.h"
#include "config_app.h"

#ifdef __cplusplus
extern "C" {
#endif

module_err_t m_i2c_as7341_init(void);

/* gain/integration time 설정. 런타임 변경 시 호출부에서 반드시 변경 시점을 로그/패킷에
 * 기록해야 한다 (codingstandard.md §6, 누락 시 리뷰 반려 사유).
 */
module_err_t m_i2c_as7341_set_gain(uint16_t gain);
module_err_t m_i2c_as7341_set_integration_time(uint16_t integration_time);

/* 채널별 raw intensity 1회 읽기 (3파장 raw[NIRS_WAVELENGTH_COUNT]에 채움).
 * 포화/저신호 감지 시 MODULE_ERR_SENSOR_SATURATION / MODULE_ERR_SENSOR_LOW_SIGNAL 반환.
 * TODO(open-item): 실제 I2C 레지스터 시퀀스 구현 (Rev1, 채널별 saturation 임계 게인 실측 포함).
 */
module_err_t m_i2c_as7341_read_raw(uint16_t raw_out[NIRS_WAVELENGTH_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* M_I2C_AS7341_H_ */
