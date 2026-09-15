/*
 * Rev0 | Application-wide configuration constants
 * Task priorities, RTC/timing constants, wavelength definitions.
 * architecture.md §2, §4, §5 참고.
 */
#ifndef CONFIG_APP_H_
#define CONFIG_APP_H_

#ifdef __cplusplus
extern "C" {
#endif

/* --- 보드(PoC v1) 펌웨어 버전 ---
 * v0.0.1부터 시작, 패치 적용마다 PATCH를 1씩 올린다 (v0.0.1 → v0.0.2 → ...).
 * 버전 이력은 CHANGELOG.md 참고.
 */
#define FW_VERSION_MAJOR 0
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 10

/* nirs_sample_t.fw_version(uint16_t)에 담기 위한 패킹: MAJOR(4bit)|MINOR(4bit)|PATCH(8bit) */
#define FW_VERSION_PACKED \
	(((FW_VERSION_MAJOR & 0xF) << 12) | ((FW_VERSION_MINOR & 0xF) << 8) | \
	 (FW_VERSION_PATCH & 0xFF))

/* --- Task priorities (Zephyr preemptive, lower number = higher priority) ---
 * architecture.md §2.2 / codingstandard.md §4: I2C(Acquisition) > BLE TX > Control
 * 현재 구현 대상은 3개 태스크(m_i2c/m_ble/m_ctrl)로 한정한다.
 * Battery/Temperature, Logging 태스크는 아직 구현하지 않는다.
 */
#define I2C_TASK_PRIORITY    2
#define BLE_TASK_PRIORITY 	 4
#define CTRL_TASK_PRIORITY   6

#define I2C_TASK_STACK_SIZE    2048
#define BLE_TASK_STACK_SIZE    2048	
#define CTRL_TASK_STACK_SIZE   1024

/* --- Sampling / RTC timing (architecture.md §2.3) ---
 * LFCLK 32.768kHz 기준 100ms = 3276.8 tick (비정수).
 * 5-frame 주기로 3277 tick 4회 + 3276 tick 1회 = 16384 tick = 정확히 500ms
 * (2026-09-15 코드 리뷰로 비율이 반대로 구현돼 있던 버그 발견/수정, m_i2c_rtc.c 참고).
 */
#define RTC_LFCLK_HZ                 32768
#define RTC_TICK_TARGET_100MS_LOW    3276
#define RTC_TICK_TARGET_100MS_HIGH   3277
#define RTC_BRESENHAM_FRAME_PERIOD   5   /* HIGH tick이 나오는 주기 (5프레임 중 1회) */

#define SAMPLE_RATE_HZ  10

/* --- Wavelengths (architecture.md §5) --- */
typedef enum {
	NIRS_WAVELENGTH_640NM = 0,
	NIRS_WAVELENGTH_680NM,
	NIRS_WAVELENGTH_950NM,
	NIRS_WAVELENGTH_COUNT,
} nirs_wavelength_t;

/* --- NIR 센서 (AS7341 x2, pinmap.md §3: 각각 독립 I2C 버스) --- */
typedef enum {
	NIR_SENSOR_1 = 0, /* &i2c1, SCL=P0.09/SDA=P0.10 */
	NIR_SENSOR_2,      /* &i2c0, SCL=P0.08/SDA=P0.07 */
	NIR_SENSOR_COUNT,
} nir_sensor_id_t;

/* --- Ring buffer (architecture.md §2.5: 100~200 샘플) --- */
#define RING_BUFFER_CAPACITY 150

/* --- LED 상태 시나리오 (m_i2c.c가 관리, pinmap.md §6) ---
 * SW1은 MAX16054를 통해 VBAT를 하드웨어적으로 on/off한다 — MCU에는 GPIO로 연결되어
 * 있지 않다. 즉 "디바이스 On"은 곧 "부팅됨"이고, "디바이스 Off"는 전원이 물리적으로
 * 끊긴 것이라 소프트웨어로 표현할 상태가 아니다 (LED도 전원과 함께 자동 소등).
 *
 * 시나리오: 부팅(디바이스 On) → LED 3개를 1개씩 1초 간격 순차 점등(3개 LED가 밀착
 * 실장돼 있고 하나는 적외선이라 육안 개별 확인용, 2026-09-14 실기 테스트 요청) →
 * BLE 연동 성공 시 10회 점멸 → 측정 시퀀스(파장별 PWM 스트로빙,
 * m_i2c_task_entry의 acquire_one_sample) 시작.
 */
#define I2C_LED_INDICATOR_DUTY_PERMILLE 200 /* 20% duty — 개발 단계 눈부심 방지 (2026-09-15 변경, 기존 50) */

/* 디바이스 On 상태에서 LED 1개당 점등 유지 시간. RTC 100ms tick을 그대로 세므로
 * SAMPLE_RATE_HZ(10)개 tick = 1초.
 */
#define I2C_LED_DEVICE_ON_STEP_TICKS SAMPLE_RATE_HZ

/* TEMP(하드웨어 진단용, 2026-09-14): D3/D4 미점등 원인 파악을 위해 1이면 순차 점등 대신
 * 3개 LED를 100% duty로 계속 켜둔다(타이밍 걱정 없이 멀티미터로 측정 가능).
 * 진단(R17/R20 2.7V 동일, D4 육안 확인) 완료 — 순차 점등 모드로 복귀.
 */
#define TEMP_LED_STATIC_ALL_ON_TEST 0

/* BLE 연동 성공 점멸 횟수. 점멸 토글은 RTC 100ms tick을 그대로 사용한다
 * (토글 1회=100ms, 점멸 1회=on+off=2 tick=200ms → 10회 점멸 = 2초).
 */
#define I2C_LED_BLE_CONNECT_BLINK_COUNT 10

/* TEMP(개발용, 2026-09-15): AS7341 채널-파장 매핑 실측 검증용. 1이면 BLE 연동 대기 없이
 * 부팅 즉시 측정 시퀀스(acquisition)로 진입해서 raw 채널 값을 RTT로 바로 확인할 수 있다.
 * 실제 BLE(Rev2)가 준비되기 전까지의 임시 우회이며, 검증 끝나면 0으로 되돌린다.
 */
#define TEMP_AS7341_READ_TEST 0

/* TEMP(개발용, 2026-09-15): RTC 100ms ISR → I2C Task wake 주기 실측 검증용.
 * 1이면 세마포어로 깨어날 때마다 RTC 타임스탬프를 로그로 찍어 tick 간격이 실제로
 * ~100ms인지 확인할 수 있다. 트래커 "Zephyr Task RTC Timer로 100ms ISR 구현" 항목의
 * 검증 방식(ISR 디버깅 로그 확인)에 대응. 검증 끝나면 0으로 되돌린다.
 */
#define TEMP_RTC_TICK_LOG_TEST 0

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_APP_H_ */
