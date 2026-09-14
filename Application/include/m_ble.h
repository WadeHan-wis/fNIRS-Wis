/*
 * Module   : m_ble (BLE Task)
 * Rev      : Rev0(스켈레톤) / Rev2(실 BLE 스트리밍 + OTA)
 * Priority : BLE_TASK_PRIORITY (config_app.h)
 * Depends  : m_i2c_ring_buffer (reader), m_ble_batch
 *
 * I2C(Acquisition) Task와 독립적으로 동작해야 한다 — BLE 지연/재연결/혼잡이
 * optical sampling timing에 영향을 주면 안 된다 (architecture.md §2.2).
 */
#ifndef M_BLE_H_
#define M_BLE_H_

#include "module_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BLE Task 진입점. 이 파일(m_ble.c) 안에서 K_THREAD_DEFINE으로 등록된다.
 * BLE stack 초기화는 main()이 아니라 이 태스크 진입 직후 내부에서 수행한다
 * (S2 task_i2c 관례 계승).
 */
void m_ble_task_entry(void *p1, void *p2, void *p3);

#ifdef __cplusplus
}
#endif

#endif /* M_BLE_H_ */
