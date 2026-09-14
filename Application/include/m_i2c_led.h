/*
 * Rev1 구현 (PoC v1, nRF52832 PWM0 채널 0/1/2 — app.overlay, pinmap.md §2)
 * Module   : m_i2c_led
 * Task     : I2C Task 컨텍스트에서만 호출 (blocking 가능, ISR 호출 금지).
 *            AS7341 측정 정확도를 위해 LED는 오직 m_i2c 태스크만 구동한다 —
 *            다른 태스크(CTRL 등)에서 직접 호출하지 않는다.
 * Depends  : app.overlay의 &pwm0 + zephyr,user pwms 프로퍼티
 *
 * 640/680/950nm(PoC v1 LED3 실장은 980nm, architecture.md §11) LED를 PWM duty로
 * 개별 구동한다 (연속점등 금지, architecture.md §5).
 * LED 상태 시나리오(디바이스 On/BLE 연동 점멸/측정 시퀀스)는 m_i2c.c가 관리한다.
 */
#ifndef M_I2C_LED_H_
#define M_I2C_LED_H_

#include <stdint.h>
#include "module_err.h"
#include "config_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 드라이버 초기화. I2C Task 시작 전 1회 호출. */
module_err_t m_i2c_led_init(void);

/* 지정한 파장 LED를 duty_permille(0~1000, 1000=100%)로 구동한다.
 * I2C Task 컨텍스트에서만 호출 — blocking 가능.
 */
module_err_t m_i2c_led_set_duty(nirs_wavelength_t wavelength, uint16_t duty_permille);

/* 모든 LED를 끈다. */
module_err_t m_i2c_led_all_off(void);

/* 3개 LED를 모두 같은 duty_permille로 켠다.
 * m_i2c.c의 "디바이스 On" 인디케이터 및 "BLE 연동 성공" 점멸 시퀀스에 사용한다.
 */
module_err_t m_i2c_led_all_on(uint16_t duty_permille);

#ifdef __cplusplus
}
#endif

#endif /* M_I2C_LED_H_ */
