#include <zephyr/kernel.h>
#include <stdbool.h>
#include "m_i2c.h"
#include "m_i2c_rtc.h"
#include "m_i2c_ring_buffer.h"
#include "m_i2c_led.h"
#include "m_i2c_as7341.h"
#include "m_ctrl.h"
#include "nirs_sample.h"
#include "config_app.h"

static uint32_t s_seq_num;
static uint16_t s_fixed_gain;
static uint16_t s_fixed_integration_time;

/*
 * LED 상태 시나리오 (요청사항 그대로):
 *   1) 디바이스 On  → LED 3개를 1개씩 1초 간격으로 순차 점등 (3개 LED가 밀착
 *      실장돼 있고 하나는 적외선이라 육안 개별 확인용). 디바이스 Off는 SW1이
 *      하드웨어적으로 전원을 끊어 LED도 자동 소등되므로 별도 코드 경로 없음
 *      (pinmap.md §6 / config_app.h 참고)
 *   2) BLE 연동 성공 → LED 10회 점멸
 *   3) 점멸 종료 후   → 정상 측정 시퀀스(파장별 PWM 스트로빙, source-detector 30mm
 *      기준 광학 설계, architecture.md §5)로 전환
 *
 * AS7341 측정 정확도를 위해 LED는 이 태스크(m_i2c)만 구동한다. BLE 연결 여부는
 * m_ble가 CTRL을 거쳐 전달한다(m_ctrl_is_ble_connected()) — Acquisition/BLE 태스크를
 * 직접 결합하지 않기 위함(architecture.md §2.2).
 */
typedef enum {
	I2C_LED_MODE_DEVICE_ON = 0, /* 디바이스 On, BLE 미연결 — 1개씩 1초 간격 순차 점등 */
	I2C_LED_MODE_BLE_BLINK,      /* BLE 연동 성공 — 10회 점멸 중 */
	I2C_LED_MODE_ACQUISITION,    /* 정상 측정 시퀀스 */
} i2c_led_mode_t;

static i2c_led_mode_t s_led_mode;
static uint8_t s_blink_toggle_count;
static bool s_blink_led_on;

static uint8_t s_device_on_led_index;
static uint8_t s_device_on_tick_count;

/* main()에서 호출하지 않는다 — m_i2c_task_entry() 진입 직후 태스크 컨텍스트에서
 * 1회 수행한다 (S2 task_i2c 관례: 드라이버 init은 생성된 task 안에서 진행).
 */
static module_err_t i2c_init(void)
{
	module_err_t err;

	s_seq_num = 0;

	m_i2c_ring_buffer_init();

	err = m_i2c_rtc_init();
	if (err != MODULE_ERR_OK) {
		return err;
	}

	err = m_i2c_led_init();
	if (err != MODULE_ERR_OK) {
		return err;
	}

	err = m_i2c_as7341_init();
	if (err != MODULE_ERR_OK) {
		return err;
	}

	/* architecture.md §4/§5: 초기 검증 단계에서는 gain/integration time 고정값 사용.
	 * TODO(open-item): 채널별 saturation 임계 게인 실측 후 고정값 확정 (Rev1).
	 */
	s_fixed_gain = 0;
	s_fixed_integration_time = 0;
	m_i2c_as7341_set_gain(s_fixed_gain);
	m_i2c_as7341_set_integration_time(s_fixed_integration_time);

	err = m_i2c_rtc_start();
	if (err != MODULE_ERR_OK) {
		return err;
	}

	/* 시나리오 1: 디바이스 On → LED 0번부터 1초씩 순차 점등 시작 */
	s_led_mode = I2C_LED_MODE_DEVICE_ON;
	s_device_on_led_index = 0;
	s_device_on_tick_count = 0;

#if TEMP_LED_STATIC_ALL_ON_TEST
	/* TEMP(하드웨어 진단): 3개 전부 100% duty로 고정 — D3/D4 미점등 원인 확인용 */
	return m_i2c_led_all_on(1000);
#else
	m_i2c_led_all_off();
	return m_i2c_led_set_duty((nirs_wavelength_t)s_device_on_led_index,
				   I2C_LED_INDICATOR_DUTY_PERMILLE);
#endif
}

static void acquire_one_sample(nirs_sample_t *sample)
{
	sample->timestamp_us = m_i2c_rtc_get_timestamp_us();
	sample->seq_num = s_seq_num++;
	sample->gain = s_fixed_gain;
	sample->integration_time = s_fixed_integration_time;
	sample->battery_pct = 0; /* TODO(open-item): Battery 모듈 미구현 */
	sample->fw_version = FW_VERSION_PACKED;
	sample->status = MODULE_ERR_OK;

	for (int wl = 0; wl < NIRS_WAVELENGTH_COUNT; wl++) {
		/* TODO(open-item): 실제 duty 값은 Rev1 LED 시퀀싱 정책에 따라 결정 */
		sample->led_duty[wl] = 500;
		m_i2c_led_set_duty((nirs_wavelength_t)wl, sample->led_duty[wl]);
	}

	module_err_t err = m_i2c_as7341_read_raw(sample->raw);

	m_i2c_led_all_off();

	if (err != MODULE_ERR_OK) {
		sample->status = err;
		m_ctrl_report_error(err);
	}
}

/* RTC 100ms tick마다 1회 호출. 시나리오 1(LED 1개씩 1초 순차 점등)을 진행하다가,
 * BLE 연동이 확인되면 시나리오 2(점멸)로 전환한다.
 */
static void handle_device_on_tick(void)
{
	if (m_ctrl_is_ble_connected()) {
		s_led_mode = I2C_LED_MODE_BLE_BLINK;
		s_blink_toggle_count = 0;
		s_blink_led_on = false;
		m_i2c_led_all_off();
		return;
	}

#if TEMP_LED_STATIC_ALL_ON_TEST
	return; /* TEMP(하드웨어 진단): 순차 점등 로직을 건너뛰고 3개 전부 계속 켜둔다 */
#endif

	s_device_on_tick_count++;
	if (s_device_on_tick_count < I2C_LED_DEVICE_ON_STEP_TICKS) {
		return;
	}

	s_device_on_tick_count = 0;
	m_i2c_led_set_duty((nirs_wavelength_t)s_device_on_led_index, 0);

	s_device_on_led_index++;
	if (s_device_on_led_index >= NIRS_WAVELENGTH_COUNT) {
		s_device_on_led_index = 0;
	}

	m_i2c_led_set_duty((nirs_wavelength_t)s_device_on_led_index,
			    I2C_LED_INDICATOR_DUTY_PERMILLE);
}

static void handle_ble_blink_tick(void)
{
	s_blink_led_on = !s_blink_led_on;

	if (s_blink_led_on) {
		m_i2c_led_all_on(I2C_LED_INDICATOR_DUTY_PERMILLE);
	} else {
		m_i2c_led_all_off();
	}

	s_blink_toggle_count++;

	/* 토글 1회=100ms, 점멸(on+off) 1회=2 toggle → N회 점멸=2N toggle */
	if (s_blink_toggle_count >= (I2C_LED_BLE_CONNECT_BLINK_COUNT * 2)) {
		s_led_mode = I2C_LED_MODE_ACQUISITION;
		m_i2c_led_all_off(); /* 측정 시퀀스에서 파장별로 개별 점등하므로 우선 소등 */
	}
}

void m_i2c_task_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	module_err_t init_err = i2c_init();

	if (init_err != MODULE_ERR_OK) {
		m_ctrl_report_error(init_err);
		k_sleep(K_FOREVER); /* 획득 하드웨어 없이는 이 태스크가 할 일이 없다 */
	}

	nirs_sample_t sample;

	while (1) {
		k_sem_take(&sem_i2c_ready, K_FOREVER);

		switch (s_led_mode) {
		case I2C_LED_MODE_DEVICE_ON:
			handle_device_on_tick();
			break;

		case I2C_LED_MODE_BLE_BLINK:
			handle_ble_blink_tick();
			break;

		case I2C_LED_MODE_ACQUISITION:
			acquire_one_sample(&sample);

			module_err_t err = m_i2c_ring_buffer_push(&sample);

			if (err == MODULE_ERR_RING_BUFFER_OVERFLOW) {
				m_ctrl_report_error(err);
			}
			break;
		}
	}
}

/* main()에서 생성하지 않는다 — 커널 시작 시 자동 등록/스케줄링된다. */
K_THREAD_DEFINE(m_i2c_tid, I2C_TASK_STACK_SIZE, m_i2c_task_entry, NULL, NULL, NULL,
		 I2C_TASK_PRIORITY, 0, 0);
