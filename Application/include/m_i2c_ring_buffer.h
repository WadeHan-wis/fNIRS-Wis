/*
 * Module   : m_i2c_ring_buffer
 * Task     : I2C Task(writer)와 BLE Task(reader) 모두에서 호출됨
 * Priority : N/A (자료구조, mutex로 보호)
 *
 * I2C Task와 BLE Task 간의 유일한 통로 (codingstandard.md §4).
 * 서로 직접 함수 호출로 데이터를 넘기지 않는다.
 */
#ifndef M_I2C_RING_BUFFER_H_
#define M_I2C_RING_BUFFER_H_

#include <stdbool.h>
#include "module_err.h"
#include "nirs_sample.h"

#ifdef __cplusplus
extern "C" {
#endif

void m_i2c_ring_buffer_init(void);

/* I2C Task에서 호출. 버퍼가 가득 차면 overflow로 처리하고
 * silent하게 버리지 않는다 — dropped count를 증가시킨다 (codingstandard.md §6).
 */
module_err_t m_i2c_ring_buffer_push(const nirs_sample_t *sample);

/* BLE Task에서 호출. 비어있으면 false 반환. */
bool m_i2c_ring_buffer_pop(nirs_sample_t *sample_out);

uint32_t m_i2c_ring_buffer_get_dropped_count(void);
uint32_t m_i2c_ring_buffer_get_max_usage(void);

#ifdef __cplusplus
}
#endif

#endif /* M_I2C_RING_BUFFER_H_ */
