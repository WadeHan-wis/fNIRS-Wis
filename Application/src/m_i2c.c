#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <stdbool.h>
#include "m_i2c.h"
#include "m_ble.h"
#include "m_i2c_rtc.h"
#include "m_i2c_ring_buffer.h"
#include "m_i2c_led.h"
#include "m_i2c_as7341.h"
#include "m_ctrl.h"
#include "nirs_sample.h"
#include "config_app.h"

LOG_MODULE_REGISTER(m_i2c, LOG_LEVEL_INF);

static uint32_t s_seq_num;
static uint16_t s_fixed_gain;
static uint16_t s_fixed_integration_time;

/* NIR1(&i2c1)/NIR2(&i2c0) — pinmap.md §3, 각각 독립 I2C 버스에 실장된 AS7341 2개. */
static m_i2c_as7341_dev_t s_as7341[NIR_SENSOR_COUNT];

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
	/* Safety 인증 대응(2026-09-17, architecture.md §11 항목6-[3]): 장시간 BLE 미연결
	 * 저전력 대기모드. ACQUISITION 중 연결이 끊겨도 BLE_DISCONNECT_STANDBY_TIMEOUT_TICKS
	 * 동안은 계속 측정+버퍼링하다가(로컬 버퍼링 지속), 그 이상 끊겨 있으면 이 모드로
	 * 전환해서 측정을 멈추고(발열/전력 절감) 짧은 점멸로 대기 상태를 표시한다.
	 */
	I2C_LED_MODE_STANDBY,
} i2c_led_mode_t;

static i2c_led_mode_t s_led_mode;
static uint8_t s_blink_toggle_count;
static bool s_blink_led_on;

/* ACQUISITION 중 연속 미연결 tick 카운트 (연결되면 0으로 리셋). STANDBY 점멸용 카운터. */
static uint32_t s_disconnect_tick_count;
static uint32_t s_standby_blink_tick_count;

static uint8_t s_device_on_led_index;
static uint8_t s_device_on_tick_count;

/* BLE AS7341_CONFIG로 설정 가능한 파장별 LED duty (permille). 기본값은 기존
 * placeholder(500‰=50%)를 유지 — CONFIG write가 들어오면 apply_pending_ble_config()가
 * 갱신한다 (2026-09-15, 테스트 APK 프로토콜 반영). */
static uint16_t s_led_duty_permille[NIRS_WAVELENGTH_COUNT] = {500, 500, 500};

/* cycle/active window(offset 6-9) 게이팅 — architecture.md §11 항목4(BLE 실 스택+APK
 * 연동) 완료를 위해 2026-09-18 추가. RTC 100ms tick 주기 자체는 그대로 유지하고
 * (m_ble_proto.h 상단 TODO 참고, §2.3 Bresenham 보정 대상과 분리), ACQUISITION 모드에서
 * "이번 tick에 실제로 측정할지"만 tick 카운터로 게이팅한다. 기본값은 cycle=1/active=1
 * tick(=항상 active, 게이팅 없음) — BLE가 CONFIG를 한 번도 안 보낸 상태(TEMP_AS7341_READ_TEST
 * 등)에서 기존 검증된 매 tick 측정 동작을 그대로 보존하기 위함.
 */
static uint16_t s_gate_cycle_ticks = 1;
static uint16_t s_gate_active_ticks = 1;
static uint32_t s_gate_tick_count;

/* ms를 RTC tick(100ms, SAMPLE_RATE_HZ 고정) 단위로 반올림 변환, 최소 1 tick 클램프
 * (0이면 아래 % 연산이 미정의 동작이 됨). */
static uint16_t ms_to_ticks_clamped(uint16_t ms)
{
	uint32_t tick_ms = 1000U / SAMPLE_RATE_HZ;
	uint32_t ticks = ((uint32_t)ms + tick_ms / 2U) / tick_ms;

	if (ticks < 1U) {
		ticks = 1U;
	}

	return (uint16_t)ticks;
}

/* 프로토콜의 integration time 필드(u8, 1 unit=20ms)를 AS7341 ATIME 레지스터로 변환.
 * ASTEP는 m_i2c_as7341_set_integration_time()이 999로 고정하므로 그 전제로 계산한다:
 * t_int_ms = (ATIME+1) * 1000 * 2.78us = (ATIME+1) * 2.78ms.
 */
static uint16_t integration_units_to_atime(uint8_t units)
{
	uint32_t t_int_us = (uint32_t)units * 20000U;
	uint32_t atime_plus1 = (t_int_us + 1390U) / 2780U; /* +denom/2 반올림 */

	if (atime_plus1 < 1U) {
		atime_plus1 = 1U;
	}
	if (atime_plus1 > 256U) {
		atime_plus1 = 256U;
	}

	return (uint16_t)(atime_plus1 - 1U);
}

/* RTC tick마다(모드 무관) 호출 — BLE로부터 새 AS7341_CONFIG가 도착했으면 적용한다.
 * Acquisition↔BLE 직접 결합 금지 원칙에 따라 m_ctrl을 경유해서만 받는다
 * (m_ctrl.h/m_ble_proto.h 상단 주석 참고, 2026-09-15).
 */
static void apply_pending_ble_config(void)
{
	uint8_t config[CTRL_AS7341_CONFIG_LEN];

	if (!m_ctrl_take_as7341_config(config)) {
		return;
	}

	/* LED1~4 = offset 2~5, %값. 이 보드는 LED가 3개(640/680/950nm)뿐이라 LED4는
	 * 무시한다. */
	for (int wl = 0; wl < NIRS_WAVELENGTH_COUNT; wl++) {
		uint8_t percent = config[2 + wl];

		if (percent > 100) {
			percent = 100;
		}
		s_led_duty_permille[wl] = (uint16_t)percent * 10U;
	}

	s_fixed_integration_time = integration_units_to_atime(config[0]);
	for (int i = 0; i < NIR_SENSOR_COUNT; i++) {
		m_i2c_as7341_set_integration_time(&s_as7341[i], s_fixed_integration_time);
	}

	/* cycle/active window(offset 6-9) — 위 s_gate_* 주석 참고. active>cycle 클램프는
	 * m_ble_proto.c에서 이미 처리됨. */
	uint16_t cycle_ms = sys_get_le16(&config[6]);
	uint16_t active_ms = sys_get_le16(&config[8]);

	s_gate_cycle_ticks = ms_to_ticks_clamped(cycle_ms);
	s_gate_active_ticks = ms_to_ticks_clamped(active_ms);
	if (s_gate_active_ticks > s_gate_cycle_ticks) {
		s_gate_active_ticks = s_gate_cycle_ticks;
	}
	s_gate_tick_count = 0;

	LOG_INF("BLE config 적용: LED duty(permille)=%u/%u/%u ATIME=%u gate=%u/%u tick",
		s_led_duty_permille[0], s_led_duty_permille[1], s_led_duty_permille[2],
		s_fixed_integration_time, s_gate_active_ticks, s_gate_cycle_ticks);
}

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

	err = m_i2c_as7341_init(&s_as7341[NIR_SENSOR_1], DEVICE_DT_GET(DT_NODELABEL(i2c1)));
	if (err != MODULE_ERR_OK) {
		return err;
	}

	err = m_i2c_as7341_init(&s_as7341[NIR_SENSOR_2], DEVICE_DT_GET(DT_NODELABEL(i2c0)));
	if (err != MODULE_ERR_OK) {
		return err;
	}

	/* architecture.md §4/§5: 초기 검증 단계에서는 gain/integration time 고정값 사용.
	 * gain=0(0.5x)/ATIME=0(2.78ms)으로는 실기에서 raw 값이 거의 0(노이즈 수준)이라
	 * 광학 반응 확인이 안 됨을 확인 — gain=9(256x)/ATIME=29(약 83ms)로 변경, 실기에서
	 * 6채널 모두 안정적인 비영(非零) 값을 확인했다 (2026-09-15, NIR1 기준).
	 * TODO(open-item): 채널별 saturation 임계 게인은 아직 실측/확정 전 — 이 값은 "동작 확인"
	 * 수준이며 최종 게인/적분시간은 모바일 연동 raw data 수집 후 재조정한다 (architecture.md §11).
	 */
	s_fixed_gain = 9;
	s_fixed_integration_time = 29;
	for (int i = 0; i < NIR_SENSOR_COUNT; i++) {
		m_i2c_as7341_set_gain(&s_as7341[i], s_fixed_gain);
		m_i2c_as7341_set_integration_time(&s_as7341[i], s_fixed_integration_time);
	}

	/* RTC 시작(ticking)은 여기서 하지 않는다 — BLE bt_enable()이 끝난 뒤
	 * m_i2c_task_entry()에서 시작한다 (아래 sem_ble_init_done 관련 주석 참고).
	 */

	/* 부팅 버전 점멸 표시(config_app.h 상단 주석 참고) — SWD/RTT 없이 육안으로
	 * OTA 적용 여부를 확인하기 위한 정식 기능. K_MSEC 사용은 여기가 RTC tick 루프
	 * 시작 전(부팅 시퀀스 중) 1회뿐이라 문제 없음 — ISR이 아니라 일반 태스크
	 * 컨텍스트(m_i2c_task_entry → i2c_init).
	 */
	for (int i = 0; i < (FW_VERSION_PATCH + 1); i++) {
		m_i2c_led_all_on(I2C_LED_INDICATOR_DUTY_PERMILLE);
		k_sleep(K_MSEC(FW_VERSION_BOOT_BLINK_MS));
		m_i2c_led_all_off();
		k_sleep(K_MSEC(FW_VERSION_BOOT_BLINK_MS));
	}

	/* 시나리오 1: 디바이스 On → LED 0번부터 1초씩 순차 점등 시작 */
	s_led_mode = I2C_LED_MODE_DEVICE_ON;
	s_device_on_led_index = 0;
	s_device_on_tick_count = 0;

#if TEMP_AS7341_READ_TEST
	/* TEMP(개발용): AS7341 채널 매핑 실측 검증 — BLE 연동 대기 없이 바로 측정 시퀀스로 진입 */
	s_led_mode = I2C_LED_MODE_ACQUISITION;
	m_i2c_led_all_off();
	return MODULE_ERR_OK;
#elif TEMP_LED_STATIC_ALL_ON_TEST
	/* TEMP(하드웨어 진단): 3개 전부 100% duty로 고정 — D3/D4 미점등 원인 확인용 */
	return m_i2c_led_all_on(1000);
#else
	m_i2c_led_all_off();
	return m_i2c_led_set_duty((nirs_wavelength_t)s_device_on_led_index,
				   I2C_LED_INDICATOR_DUTY_PERMILLE);
#endif
}

/* Safety 인증 대응(2026-09-17, architecture.md §11 항목6-[4]): module_err.h에 이미
 * 정의돼 있었지만 지금까지 아무도 판정하지 않던 SATURATION/LOW_SIGNAL을 실제로 채운다.
 * I2C 통신 에러가 이미 있으면(호출부에서 status가 OK일 때만 호출) 그쪽을 우선한다.
 * TODO(open-item): 채널별/게인별 정확한 풀스케일 계산은 architecture.md §11의 gain/ATIME
 * 실측 캘리브레이션과 함께 재확정 — 지금은 보수적 고정 임계값(config_app.h) 사용.
 */
static module_err_t check_sensor_sanity(const nirs_sample_t *sample)
{
	for (int i = 0; i < NIR_SENSOR_COUNT; i++) {
		for (int wl = 0; wl < NIRS_WAVELENGTH_COUNT; wl++) {
			if (sample->raw[i][wl] >= AS7341_SATURATION_THRESHOLD) {
				return MODULE_ERR_SENSOR_SATURATION;
			}
		}
	}

	for (int i = 0; i < NIR_SENSOR_COUNT; i++) {
		for (int wl = 0; wl < NIRS_WAVELENGTH_COUNT; wl++) {
			if (sample->raw[i][wl] < AS7341_LOW_SIGNAL_THRESHOLD) {
				return MODULE_ERR_SENSOR_LOW_SIGNAL;
			}
		}
	}

	return MODULE_ERR_OK;
}

/* 이번 tick이 cycle/active window의 active 구간인지 판정 — 호출부(ACQUISITION 분기)가
 * true일 때만 acquire_one_sample()을 호출한다. */
static bool is_gate_active_tick(void)
{
	return (s_gate_tick_count % s_gate_cycle_ticks) < s_gate_active_ticks;
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
		/* BLE AS7341_CONFIG로 설정된 duty 사용 (기본값 500‰) — apply_pending_ble_config() 참고. */
		sample->led_duty[wl] = s_led_duty_permille[wl];
		m_i2c_led_set_duty((nirs_wavelength_t)wl, sample->led_duty[wl]);
	}

	for (int i = 0; i < NIR_SENSOR_COUNT; i++) {
		module_err_t err = m_i2c_as7341_read_raw(&s_as7341[i], sample->raw[i]);

		if (err != MODULE_ERR_OK) {
			sample->status = err;
			m_ctrl_report_error(err);
		}
	}

	if (sample->status == MODULE_ERR_OK) {
		module_err_t sanity_err = check_sensor_sanity(sample);

		if (sanity_err != MODULE_ERR_OK) {
			sample->status = sanity_err;
			LOG_WRN("센서 sanity check 실패: %s (§11 항목6-[4] 실기 검증용 가시화, 2026-09-18)",
				sanity_err == MODULE_ERR_SENSOR_SATURATION ? "SATURATION" : "LOW_SIGNAL");
			m_ctrl_report_error(sanity_err);
		}
	}

	m_i2c_led_all_off();
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

/* BLE 연결이 끊기면 측정을 멈추고 "디바이스 On 순차점등" 상태로 되돌린다
 * (2026-09-16, R2-2 재연결 시나리오 구현 — 연결 해제 후에도 계속 센싱하던 문제 수정).
 * i2c_init()의 시나리오 1 진입 로직과 동일하게 LED 0번부터 다시 순차 점등을 시작한다.
 */
static void reset_to_device_on(void)
{
	m_i2c_led_all_off();
	s_led_mode = I2C_LED_MODE_DEVICE_ON;
	s_device_on_led_index = 0;
	s_device_on_tick_count = 0;
	/* Safety 인증 대응(2026-09-17, architecture.md §11 항목6-[4]): seq_num은 더 이상
	 * 여기서 리셋하지 않는다 — 부팅 세션 내내 단조증가로 유지해야 앱이 재연결 후
	 * seq_num 불연속을 "로컬 버퍼 오버플로우로 유실된 실측 구간"의 gap 마커로 식별할
	 * 수 있다(m_ble_proto.h 참고). */
	m_i2c_led_set_duty((nirs_wavelength_t)s_device_on_led_index, I2C_LED_INDICATOR_DUTY_PERMILLE);
}

/* Safety 인증 대응(2026-09-17, architecture.md §11 항목6-[3]): 장시간 BLE 미연결 저전력
 * 대기모드. LED1(640nm)만 짧게 점멸해서 "동작 중이나 연결 대기" 상태를 표시한다. */
static void handle_standby_tick(void)
{
	uint32_t phase = s_standby_blink_tick_count % BLE_STANDBY_BLINK_PERIOD_TICKS;

	if (phase == 0) {
		m_i2c_led_set_duty(NIRS_WAVELENGTH_640NM, I2C_LED_INDICATOR_DUTY_PERMILLE);
	} else if (phase == BLE_STANDBY_BLINK_ON_TICKS) {
		m_i2c_led_set_duty(NIRS_WAVELENGTH_640NM, 0);
	}

	s_standby_blink_tick_count++;
}

/* Safety 인증 대응(2026-09-17): ACQUISITION 중 장시간 미연결로 저전력 대기모드에 진입. */
static void enter_standby(void)
{
	LOG_INF("BLE 연결 끊김 %u tick(%ums) 초과 -> STANDBY 진입 (§11 항목6-[3] 실기 검증용 "
		"가시화, 2026-09-18)",
		s_disconnect_tick_count, s_disconnect_tick_count * (1000U / SAMPLE_RATE_HZ));
	m_i2c_led_all_off();
	s_led_mode = I2C_LED_MODE_STANDBY;
	s_standby_blink_tick_count = 0;
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
		s_gate_tick_count = 0; /* cycle/active window를 연결 시점부터 정렬해서 시작 */
		m_i2c_led_all_off(); /* 측정 시퀀스에서 파장별로 개별 점등하므로 우선 소등 */
	}
}

K_SEM_DEFINE(sem_i2c_init_done, 0, 1);

void m_i2c_task_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	module_err_t init_err = i2c_init();

	/* 성공/실패 무관하게 항상 1회 give — BLE Task가 이 신호를 무한정 기다리며
	 * 멈추지 않게 한다 (m_i2c.h 상단 주석 참고, 2026-09-15 bt_enable() 경합 수정). */
	k_sem_give(&sem_i2c_init_done);

	if (init_err != MODULE_ERR_OK) {
		m_ctrl_report_error(init_err);
		k_sleep(K_FOREVER); /* 획득 하드웨어 없이는 이 태스크가 할 일이 없다 */
	}

	/* BLE bt_enable()이 끝날 때까지 RTC를 시작하지 않는다 — RTC2가 100ms마다 계속
	 * 인터럽트를 발생시키는 상태에서 bt_enable()이 진행되면 SoftDevice Controller
	 * 초기화가 멈추는 문제를 실기에서 확인함(2026-09-15). BLE Task가 bt_enable()
	 * 완료(성공/실패 무관) 후 sem_ble_init_done을 준다.
	 */
	k_sem_take(&sem_ble_init_done, K_FOREVER);

	module_err_t rtc_err = m_i2c_rtc_start();

	if (rtc_err != MODULE_ERR_OK) {
		m_ctrl_report_error(rtc_err);
		k_sleep(K_FOREVER); /* RTC 없이는 샘플링 타이밍을 만들 수 없다 */
	}

	nirs_sample_t sample;

	while (1) {
		k_sem_take(&sem_i2c_ready, K_FOREVER);

		apply_pending_ble_config();

#if TEMP_RTC_TICK_LOG_TEST
		/* TEMP(개발용): RTC ISR이 준 세마포어로 깨어난 시점의 타임스탬프를 찍어
		 * tick 간격이 실제로 ~100ms인지 확인 (ISR 자체는 로그 금지이므로 소비
		 * 측 태스크에서 확인 — codingstandard.md §3). */
		LOG_INF("RTC tick, timestamp_us=%u", m_i2c_rtc_get_timestamp_us());
#endif

#if TEMP_WATCHDOG_FAULT_INJECT_TEST
		{
			static uint32_t s_fault_inject_tick_count;

			s_fault_inject_tick_count++;
			if (s_fault_inject_tick_count == TEMP_WATCHDOG_FAULT_INJECT_TICKS) {
				LOG_WRN("TEMP_WATCHDOG_FAULT_INJECT_TEST: 지금부터 m_i2c 태스크를 "
					"고의로 멈춥니다 (watchdog 강제 유발 테스트)");
			}
			if (s_fault_inject_tick_count >= TEMP_WATCHDOG_FAULT_INJECT_TICKS) {
				while (1) {
					k_sleep(K_MSEC(100));
				}
			}
		}
#endif

		/* Safety 인증 대응(2026-09-17, architecture.md §11 항목6-[1]): 단일고장으로
		 * CTRL이 안전상태(DEGRADED/FAULT)에 있으면 LED를 끄고 측정을 완전히 건너뛴다.
		 * 래치 상태라 재부팅 전까지 자동 복구하지 않는다(m_ctrl.h 참고). */
		if (m_ctrl_is_safe_state()) {
			m_i2c_led_all_off();
			m_ctrl_notify_alive(CTRL_ALIVE_I2C);
			continue;
		}

		switch (s_led_mode) {
		case I2C_LED_MODE_DEVICE_ON:
			handle_device_on_tick();
			break;

		case I2C_LED_MODE_BLE_BLINK:
			if (!m_ctrl_is_ble_connected()) {
				reset_to_device_on();
				break;
			}
			handle_ble_blink_tick();
			break;

		case I2C_LED_MODE_ACQUISITION: {
			if (!m_ctrl_is_ble_connected()) {
				/* Safety 인증 대응(2026-09-17, 항목6-[3]): 즉시 멈추지 않고
				 * grace 기간 동안 계속 측정+버퍼링(로컬 버퍼링 지속) —
				 * 아래로 흘러서(fallthrough) 평소처럼 acquire+push한다.
				 * TODO(open-item, architecture.md §11 항목7, 2026-09-18): 테스트 APK가
				 * disconnect 후 자동 재연결을 안 해서 이 grace period를 매번 끝까지
				 * 체감하게 됨 — 펌웨어 동작은 그대로 유지, 앱 쪽에 재연결 로직 추가 필요. */
				s_disconnect_tick_count++;
				if (s_disconnect_tick_count >= BLE_DISCONNECT_STANDBY_TIMEOUT_TICKS) {
					enter_standby();
					break;
				}
			} else {
				s_disconnect_tick_count = 0;
			}

			/* cycle/active window(§11 항목4) — active 구간이 아니면 acquire 자체를
			 * 건너뛴다(LED off 상태 유지, sanity check 오탐 방지, 위 is_gate_active_tick()
			 * 주석 참고). seq_num은 push된 샘플에만 증가하므로 gap 식별(SEQ notify)
			 * 의미는 그대로 유지된다. */
			if (is_gate_active_tick()) {
				acquire_one_sample(&sample);

				module_err_t err = m_i2c_ring_buffer_push(&sample);

				if (err == MODULE_ERR_RING_BUFFER_OVERFLOW) {
					LOG_WRN("Ring buffer overflow, dropped_count=%u (§11 항목6-[4] 실기 "
						"검증용 가시화, 2026-09-18)",
						m_i2c_ring_buffer_get_dropped_count());
					m_ctrl_report_error(err);
				}
			} else {
				m_i2c_led_all_off();
			}

			s_gate_tick_count++;
			break;
		}

		case I2C_LED_MODE_STANDBY:
			if (m_ctrl_is_ble_connected()) {
				/* 기존 재연결 흐름(10회 점멸) 재사용 후 측정 재개 */
				s_disconnect_tick_count = 0;
				s_led_mode = I2C_LED_MODE_BLE_BLINK;
				s_blink_toggle_count = 0;
				s_blink_led_on = false;
				m_i2c_led_all_off();
				break;
			}
			handle_standby_tick();
			break;
		}

		/* Watchdog(Rev3, R3-1) 생존 신호 — 이 tick의 작업(I2C 읽기 포함)이 끝까지
		 * 진행됐다는 뜻이므로 루프 맨 끝에서 호출한다 (m_ctrl.h 참고). */
		m_ctrl_notify_alive(CTRL_ALIVE_I2C);
	}
}

/* main()에서 생성하지 않는다 — 커널 시작 시 자동 등록/스케줄링된다. */
K_THREAD_DEFINE(m_i2c_tid, I2C_TASK_STACK_SIZE, m_i2c_task_entry, NULL, NULL, NULL,
		 I2C_TASK_PRIORITY, 0, 0);
