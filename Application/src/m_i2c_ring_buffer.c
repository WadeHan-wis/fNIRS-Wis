#include <zephyr/kernel.h>
#include "m_i2c_ring_buffer.h"
#include "config_app.h"

static nirs_sample_t s_ring_buffer[RING_BUFFER_CAPACITY];
static uint32_t s_head; /* next write index */
static uint32_t s_tail; /* next read index */
static uint32_t s_count;

static uint32_t g_dropped_sample_count;
static uint32_t g_max_ring_buffer_usage;

static struct k_mutex mtx_ring_buffer;

void m_i2c_ring_buffer_init(void)
{
	k_mutex_init(&mtx_ring_buffer);
	s_head = 0;
	s_tail = 0;
	s_count = 0;
	g_dropped_sample_count = 0;
	g_max_ring_buffer_usage = 0;
}

module_err_t m_i2c_ring_buffer_push(const nirs_sample_t *sample)
{
	if (sample == NULL) {
		return MODULE_ERR_INVALID_PARAM;
	}

	bool overflowed = false;

	k_mutex_lock(&mtx_ring_buffer, K_FOREVER);

	if (s_count >= RING_BUFFER_CAPACITY) {
		/* overflow: silent loss 금지 — counter만 증가시키고 가장 오래된 샘플을 덮어쓴다 */
		overflowed = true;
		g_dropped_sample_count++;
		s_tail = (s_tail + 1) % RING_BUFFER_CAPACITY;
		s_count--;
	}

	s_ring_buffer[s_head] = *sample;
	s_head = (s_head + 1) % RING_BUFFER_CAPACITY;
	s_count++;

	if (s_count > g_max_ring_buffer_usage) {
		g_max_ring_buffer_usage = s_count;
	}

	k_mutex_unlock(&mtx_ring_buffer);

	return overflowed ? MODULE_ERR_RING_BUFFER_OVERFLOW : MODULE_ERR_OK;
}

bool m_i2c_ring_buffer_pop(nirs_sample_t *sample_out)
{
	bool popped = false;

	if (sample_out == NULL) {
		return false;
	}

	k_mutex_lock(&mtx_ring_buffer, K_FOREVER);

	if (s_count > 0) {
		*sample_out = s_ring_buffer[s_tail];
		s_tail = (s_tail + 1) % RING_BUFFER_CAPACITY;
		s_count--;
		popped = true;
	}

	k_mutex_unlock(&mtx_ring_buffer);

	return popped;
}

/* gap 식별 한계 개선(2026-09-18, m_ble_gatt.c on_dropped_count_read() 참고)으로 BT 호스트
 * 스레드에서도 이 getter를 호출하게 됐다 — m_i2c 태스크(push 시 증가)와의 cross-task
 * 읽기이므로 mutex로 보호한다(codingstandard.md §8, "volatile만으로 충분하다고
 * 가정하지 않는다"). */
uint32_t m_i2c_ring_buffer_get_dropped_count(void)
{
	uint32_t count;

	k_mutex_lock(&mtx_ring_buffer, K_FOREVER);
	count = g_dropped_sample_count;
	k_mutex_unlock(&mtx_ring_buffer);

	return count;
}

uint32_t m_i2c_ring_buffer_get_max_usage(void)
{
	uint32_t usage;

	k_mutex_lock(&mtx_ring_buffer, K_FOREVER);
	usage = g_max_ring_buffer_usage;
	k_mutex_unlock(&mtx_ring_buffer);

	return usage;
}
