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
#include <stdint.h>
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
 * Acquisition↔BLE는 ring buffer로만 통신). m_ble가 연결 콜백에서
 * m_ctrl_notify_ble_connected()를, 연결 해제 콜백에서 m_ctrl_notify_ble_disconnected()를
 * 호출하면, m_i2c는 m_ctrl_is_ble_connected()를 polling해서 "디바이스 On 순차점등 →
 * BLE 10회 점멸 → 측정" 전환과, 연결 해제 시 다시 처음 상태로 되돌리는 시점을 판단한다
 * (2026-09-16, R2-2 재연결 시나리오 구현).
 */
void m_ctrl_notify_ble_connected(void);
void m_ctrl_notify_ble_disconnected(void);
bool m_ctrl_is_ble_connected(void);

/* AS7341_CONFIG(0x1525) write 중계 (m_ble → m_ctrl → m_i2c), 위와 동일한 이유로
 * CTRL을 거친다. 바이트 포맷 자체는 이 모듈이 해석하지 않는다(불투명 10바이트) —
 * 인코드/디코드는 m_ble_proto(수신 시)와 m_i2c(적용 시) 양쪽이 테스트 APK 프로토콜
 * 스펙을 각자 알고 처리한다 (2026-09-15, 사용자 제공 APK 리버싱 결과 반영).
 * m_ctrl_notify_as7341_config()는 여러 태스크에서 호출될 수 있어 mutex로 보호한다.
 */
#define CTRL_AS7341_CONFIG_LEN 10

void m_ctrl_notify_as7341_config(const uint8_t config[CTRL_AS7341_CONFIG_LEN]);

/* out에 최신 설정을 복사하고 true 반환 — 새 설정이 이전에 소비된 뒤 다시 갱신되지
 * 않았으면 false (m_i2c가 매 tick 이걸 확인해서 "새 설정이 있을 때만" 적용한다). */
bool m_ctrl_take_as7341_config(uint8_t out[CTRL_AS7341_CONFIG_LEN]);

/* Watchdog 생존 신호 (Rev3, R3-1, 2026-09-16). m_i2c/m_ble Task가 각자 메인 루프
 * 반복마다 1회 호출해서 "이번 루프까지 정상 진행했다"를 알린다. m_ctrl은 등록된
 * 소스 전부가 최근에 응답했을 때만 하드웨어 watchdog을 feed하므로, 한쪽 태스크가
 * (예: I2C 버스 hang으로) 멈추면 feed가 끊겨 SoC 전체가 자동 리셋된다.
 * ISR에서는 호출하지 않는다 (다른 m_ctrl_notify_*류와 동일한 제약).
 */
typedef enum {
	CTRL_ALIVE_I2C = 0,
	CTRL_ALIVE_BLE,
	CTRL_ALIVE_SOURCE_COUNT,
} ctrl_alive_source_t;

void m_ctrl_notify_alive(ctrl_alive_source_t src);

#ifdef __cplusplus
}
#endif

#endif /* M_CTRL_H_ */
