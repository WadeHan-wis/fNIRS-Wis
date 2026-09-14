/*
 * Module   : m_i2c_rtc
 * Task     : ISR 컨텍스트 (nrfx RTC CC interrupt) + init은 I2C Task 시작 전 1회
 * Priority : N/A (ISR)
 *
 * architecture.md §2.3: Zephyr k_timer를 거치지 않고 nrfx RTC를 직접 사용한다.
 * ISR에서는 timestamp capture + semaphore give만 수행한다 (codingstandard.md §3).
 * 절대 금지: BLE/로그/flash/I2C/malloc.
 */
#ifndef M_I2C_RTC_H_
#define M_I2C_RTC_H_

#include "module_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* sem_i2c_ready: RTC ISR -> I2C Task 트리거용 세마포어 */
extern struct k_sem sem_i2c_ready;

/* RTC 드라이버 초기화 + 100ms(5-frame Bresenham 보정) 주기 시작 */
module_err_t m_i2c_rtc_init(void);
module_err_t m_i2c_rtc_start(void);
module_err_t m_i2c_rtc_stop(void);

/* I2C Task에서 샘플 타임스탬프로 쓸 32bit us 타임스탬프 조회 (S2 관례) */
uint32_t m_i2c_rtc_get_timestamp_us(void);

#ifdef __cplusplus
}
#endif

#endif /* M_I2C_RTC_H_ */
