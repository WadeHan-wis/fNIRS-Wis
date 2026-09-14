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

	k_mutex_lock(&mtx_ring_buffer, K_FOREVER);

	if (s_count >= RING_BUFFER_CAPACITY) {
		/* overflow: silent loss 금지 — counter만 증가시키고 가장 오래된 샘플을 덮어쓴다 */
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

	return (g_dropped_sample_count > 0) ? MODULE_ERR_RING_BUFFER_OVERFLOW : MODULE_ERR_OK;
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

uint32_t m_i2c_ring_buffer_get_dropped_count(void)
{
	return g_dropped_sample_count;
}

uint32_t m_i2c_ring_buffer_get_max_usage(void)
{
	return g_max_ring_buffer_usage;
}
