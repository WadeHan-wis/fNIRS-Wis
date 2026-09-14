/*
 * Module   : m_ctrl (Control Task)
 * Priority : CTRL_TASK_PRIORITY (config_app.h)
 *
 * 모든 리셋/에러는 이 모듈을 경유한다 (architecture.md §3, codingstandard.md §5).
 * 다른 모듈에서 NVIC_SystemReset()/sys_reboot()를 직접 호출하는 것은 금지한다.
 */
#ifndef M_CTRL_H_
#define M_CTRL_H_

#include <stdbool.h>
#include "module_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	CTRL_STATE_NORMAL = 0,
	CTRL_STATE_WARNING,
	CTRL_STATE_DEGRADED,
	CTRL_STATE_FAULT,
} ctrl_state_t;

typedef enum {
	CTRL_RESET_REASON_WATCHDOG = 0,
	CTRL_RESET_REASON_SOFTWARE,
	CTRL_RESET_REASON_POWER,
	CTRL_RESET_REASON_FAULT,
} ctrl_reset_reason_t;

/* Ctrl Task 진입점. 이 파일(m_ctrl.c) 안에서 K_THREAD_DEFINE으로 등록된다.
 * 별도 init 함수가 없다 — 상태(g_ctrl_status)와 메시지 큐가 모두 정적 초기화되므로
 * main()이나 다른 태스크의 호출 순서에 의존하지 않는다.
 */
void m_ctrl_task_entry(void *p1, void *p2, void *p3);

/* 어떤 모듈/태스크 컨텍스트에서도 호출 가능 (내부적으로 메시지 큐에 넣고 즉시 반환).
 * ISR에서는 호출하지 않는다.
 */
void m_ctrl_report_error(module_err_t err);

/* CTRL 상태머신을 경유하는 유일한 리셋 요청 경로. */
void m_ctrl_request_reset(ctrl_reset_reason_t reason);

ctrl_state_t m_ctrl_get_state(void);

/* BLE 연결 상태 중계 (m_ble → m_ctrl → m_i2c). Acquisition(m_i2c)과 BLE는 서로 직접
 * 결합하지 않고 CTRL을 거친다 (architecture.md §2.2, codingstandard.md §4:
 * Acquisition↔BLE는 ring buffer로만 통신). m_ble가 실제 BLE 연결 콜백(Rev2)에서
 * m_ctrl_notify_ble_connected()를 호출하면, m_i2c는 m_ctrl_is_ble_connected()를
 * polling해서 LED 점멸→측정 전환 시점을 판단한다.
 */
void m_ctrl_notify_ble_connected(void);
bool m_ctrl_is_ble_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* M_CTRL_H_ */
