#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include "m_i2c_led.h"

/* app.overlay의 zephyr,user pwms 프로퍼티 인덱스 순서 = nirs_wavelength_t 순서
 * (640/680/950nm). PWM0 채널 0/1/2 = P0.04/P0.05/P0.06 (pinmap.md §2).
 */
static const struct pwm_dt_spec s_led_pwm[NIRS_WAVELENGTH_COUNT] = {
	PWM_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
	PWM_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
	PWM_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),
};

module_err_t m_i2c_led_init(void)
{
	for (int i = 0; i < NIRS_WAVELENGTH_COUNT; i++) {
		if (!pwm_is_ready_dt(&s_led_pwm[i])) {
			return MODULE_ERR_NOT_INITIALIZED;
		}
	}

	return MODULE_ERR_OK;
}

module_err_t m_i2c_led_set_duty(nirs_wavelength_t wavelength, uint16_t duty_permille)
{
	if (wavelength >= NIRS_WAVELENGTH_COUNT || duty_permille > 1000) {
		return MODULE_ERR_INVALID_PARAM;
	}

	const struct pwm_dt_spec *spec = &s_led_pwm[wavelength];
	uint32_t pulse_ns = (uint32_t)(((uint64_t)spec->period * duty_permille) / 1000);

	if (pwm_set_dt(spec, spec->period, pulse_ns) != 0) {
		return MODULE_ERR_NOT_INITIALIZED;
	}

	return MODULE_ERR_OK;
}

module_err_t m_i2c_led_all_off(void)
{
	for (int i = 0; i < NIRS_WAVELENGTH_COUNT; i++) {
		module_err_t err = m_i2c_led_set_duty((nirs_wavelength_t)i, 0);

		if (err != MODULE_ERR_OK) {
			return err;
		}
	}

	return MODULE_ERR_OK;
}

module_err_t m_i2c_led_all_on(uint16_t duty_permille)
{
	for (int i = 0; i < NIRS_WAVELENGTH_COUNT; i++) {
		module_err_t err = m_i2c_led_set_duty((nirs_wavelength_t)i, duty_permille);

		if (err != MODULE_ERR_OK) {
			return err;
		}
	}

	return MODULE_ERR_OK;
}
