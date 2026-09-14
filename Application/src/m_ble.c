#include <zephyr/kernel.h>
#include "m_ble.h"
#include "m_ble_batch.h"
#include "m_i2c_ring_buffer.h"
#include "m_ctrl.h"
#include "config_app.h"

/* BLE 지연/재연결 대기 중에도 ring buffer를 계속 소비할 수 있도록 짧은 주기로 polling한다.
 * TODO(open-item): Rev2에서 실제 GATT notify 완료 콜백/큐 기반으로 event-driven 전환.
 */
#define BLE_POLL_INTERVAL_MS 20

static ble_batch_t s_tx_batch;

/* main()에서 호출하지 않는다 — m_ble_task_entry() 진입 직후 태스크 컨텍스트에서
 * 1회 수행한다 (S2 task_i2c 관례: 드라이버/스택 init은 생성된 task 안에서 진행).
 */
static module_err_t ble_init(void)
{
	m_ble_batch_reset(&s_tx_batch);

	/* TODO(open-item): BLE stack 초기화, advertising 시작, Tx power -8dBm 고정,
	 * MTU 협상(BLE 5.0 Extended Length 251byte 목표, 실패 시 23byte 폴백) (Rev2)
	 * TODO(open-item): 실제 BT 연결 콜백(connected callback)에서
	 * m_ctrl_notify_ble_connected()를 호출해야 한다 — m_i2c가 이 신호를 받아야
	 * "디바이스 연동 성공 → LED 10회 점멸 → 측정 시퀀스 시작"으로 전환한다.
	 */
	return MODULE_ERR_OK;
}

/* TODO(open-item): 실제 GATT notify 구현 (Rev2). 현재는 batch를 소비만 하고 버린다. */
static module_err_t send_batch(ble_batch_t *batch)
{
	if (batch->count == 0) {
		return MODULE_ERR_OK;
	}

	/* placeholder: 실제 전송 성공/실패에 따라 MODULE_ERR_BLE_TX_FAILED 등을 반환해야 한다 */
	m_ble_batch_reset(batch);

	return MODULE_ERR_OK;
}

void m_ble_task_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	module_err_t init_err = ble_init();

	if (init_err != MODULE_ERR_OK) {
		m_ctrl_report_error(init_err);
		k_sleep(K_FOREVER); /* BLE stack 없이는 이 태스크가 할 일이 없다 */
	}

	nirs_sample_t sample;

	while (1) {
		if (m_i2c_ring_buffer_pop(&sample)) {
			module_err_t err = m_ble_batch_add(&s_tx_batch, &sample);

			if (err != MODULE_ERR_OK || m_ble_batch_is_full(&s_tx_batch)) {
				err = send_batch(&s_tx_batch);
				if (err != MODULE_ERR_OK) {
					m_ctrl_report_error(err);
				}
			}
		} else {
			k_sleep(K_MSEC(BLE_POLL_INTERVAL_MS));
		}
	}
}

/* main()에서 생성하지 않는다 — 커널 시작 시 자동 등록/스케줄링된다. */
K_THREAD_DEFINE(m_ble_tid, BLE_TASK_STACK_SIZE, m_ble_task_entry, NULL, NULL, NULL,
		 BLE_TASK_PRIORITY, 0, 0);
