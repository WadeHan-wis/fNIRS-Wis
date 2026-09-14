#include "m_ble_batch.h"

void m_ble_batch_reset(ble_batch_t *batch)
{
	batch->count = 0;
}

module_err_t m_ble_batch_add(ble_batch_t *batch, const nirs_sample_t *sample)
{
	if (batch->count >= BLE_BATCH_MAX_SAMPLES) {
		return MODULE_ERR_NO_MEMORY;
	}

	batch->samples[batch->count] = *sample;
	batch->count++;

	return MODULE_ERR_OK;
}

bool m_ble_batch_is_full(const ble_batch_t *batch)
{
	return batch->count >= BLE_BATCH_MAX_SAMPLES;
}
