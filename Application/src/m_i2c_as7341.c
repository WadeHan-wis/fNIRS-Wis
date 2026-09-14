#include <string.h>
#include "m_i2c_as7341.h"

static uint16_t s_gain;
static uint16_t s_integration_time;

module_err_t m_i2c_as7341_init(void)
{
	/* TODO(open-item): devicetree I2C 노드 바인딩, 센서 초기화 시퀀스 (Rev1) */
	s_gain = 0;
	s_integration_time = 0;
	return MODULE_ERR_OK;
}

module_err_t m_i2c_as7341_set_gain(uint16_t gain)
{
	/* TODO(open-item): 실제 AS7341 gain 레지스터 write (Rev1) */
	s_gain = gain;
	return MODULE_ERR_OK;
}

module_err_t m_i2c_as7341_set_integration_time(uint16_t integration_time)
{
	/* TODO(open-item): 실제 AS7341 integration time 레지스터 write (Rev1) */
	s_integration_time = integration_time;
	return MODULE_ERR_OK;
}

module_err_t m_i2c_as7341_read_raw(uint16_t raw_out[NIRS_WAVELENGTH_COUNT])
{
	if (raw_out == NULL) {
		return MODULE_ERR_INVALID_PARAM;
	}

	/* TODO(open-item): 실제 I2C 레지스터 read 시퀀스 구현 (Rev1, 현 최우선 목표) */
	memset(raw_out, 0, sizeof(uint16_t) * NIRS_WAVELENGTH_COUNT);
	return MODULE_ERR_OK;
}
