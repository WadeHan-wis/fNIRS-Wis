/*
 * Module   : m_i2c (I2C Task — 광 획득)
 * Rev      : Rev0(스켈레톤) / Rev1(실 데이터 획득)
 * Priority : I2C_TASK_PRIORITY (전체 태스크 중 최우선, config_app.h)
 * Depends  : m_i2c_rtc (세마포어), m_i2c_led, m_i2c_as7341, m_i2c_ring_buffer
 *
 * RTC ISR이 sem_i2c_ready를 주면 깨어나 LED 시퀀싱 + AS7341 read를 수행하고
 * 결과를 ring buffer에 push한다. BLE Task와는 ring buffer로만 통신한다.
 */
#ifndef M_I2C_H_
#define M_I2C_H_

#include <zephyr/kernel.h>
#include "module_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 2026-09-15 실기 확인: bt_enable()이 AS7341(NIR1) SMUX 폴링 도중(k_sleep(1ms) 구간)
 * 끼어들면 I2C1 트랜잭션이 완료되지 않고 멈추는 초기화 순서 경합이 있다 — I2C Task
 * 우선순위(2)가 BLE Task(4)보다 높아도, sleep 구간에는 스케줄러가 낮은 우선순위
 * 태스크로 넘어갈 수 있어 발생. m_ble.c의 ble_init()은 bt_enable() 호출 전에 이
 * 세마포어를 기다려서, AS7341 I2C 초기화가 (성공/실패 무관하게) 끝난 뒤에만 BLE
 * 스택을 켠다. m_i2c_task_entry()가 i2c_init() 결과와 무관하게 항상 1회 give한다.
 */
extern struct k_sem sem_i2c_init_done;

/* I2C Task 진입점. 이 파일(m_i2c.c) 안에서 K_THREAD_DEFINE으로 등록된다.
 * 드라이버/RTC 초기화는 main()이 아니라 이 태스크 진입 직후 내부에서 수행한다
 * (S2 task_i2c 관례 계승).
 */
void m_i2c_task_entry(void *p1, void *p2, void *p3);

#ifdef __cplusplus
}
#endif

#endif /* M_I2C_H_ */
