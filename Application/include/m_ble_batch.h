/*
 * Module   : m_ble_batch
 * Task     : BLE Task 컨텍스트에서만 호출
 *
 * architecture.md §2.4: 협상된 MTU 페이로드 안에 들어가는 최대 샘플 수를 역산해
 * batch size를 결정한다. 정확한 실측치는 Rev2 착수 시 확정 (현재는 보수적 고정값).
 */
#ifndef M_BLE_BATCH_H_
#define M_BLE_BATCH_H_

#include <stdint.h>
#include <stdbool.h>
#include "module_err.h"
#include "nirs_sample.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TODO(open-item): MTU 협상 실측 후 재계산 (architecture.md §2.4) */
#define BLE_BATCH_MAX_SAMPLES 12

typedef struct {
	uint8_t count;
	nirs_sample_t samples[BLE_BATCH_MAX_SAMPLES];
} ble_batch_t;

void m_ble_batch_reset(ble_batch_t *batch);
module_err_t m_ble_batch_add(ble_batch_t *batch, const nirs_sample_t *sample);
bool m_ble_batch_is_full(const ble_batch_t *batch);

#ifdef __cplusplus
}
#endif

#endif /* M_BLE_BATCH_H_ */
