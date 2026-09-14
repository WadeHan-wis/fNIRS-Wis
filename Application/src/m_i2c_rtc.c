/*
 * nrfx RTC 직접 제어 + 5-frame Bresenham drift 보정 (architecture.md §2.3).
 *
 * NCS v3.4.0 / nrfx API(>=4.1.0) 기준으로 컴파일 검증 완료 (2026-09-14, nrf52840dk 빌드).
 * PoC v1(nRF52832) 실기 디버깅(J-Link Commander로 MemManage/Spurious IRQ ESF 직접
 * 캡처, 2026-09-14)으로 아래 두 버그를 찾아 수정함 — 두 번째 수정 후 재부팅 검증 진행 중:
 *   1) NRFX_RTC_INSTANCE(2) → NRFX_RTC_INSTANCE(NRF_RTC2) (포인터 캐스팅 버그, 확인됨)
 *   2) RTC2 IRQ(36)를 IRQ_CONNECT하지 않아 실제 인터럽트 발생 시 Spurious IRQ로 크래시
 *      → 아래 IRQ_CONNECT/irq_enable 추가로 수정 (검증 대기 중).
 *
 * TODO(open-item): 실제 RTC 인스턴스 번호는 보드 확정 후 재확인한다.
 * RTC0/RTC1은 Zephyr 커널 틱과 충돌할 수 있어 RTC2를 기본값으로 둔다.
 * TODO(open-item): IRQ 우선순위(4, 아래)는 임시값 — Rev1 오실로스코프 지터 실측 후
 * 태스크 우선순위 체계와 함께 재검토한다.
 */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <nrfx_rtc.h>
#include "m_i2c_rtc.h"
#include "config_app.h"

/* RTC2 IRQ 번호. nrf52832.dtsi / nrf52840.dtsi 둘 다 36으로 확인됨.
 * devicetree의 rtc2 노드는 status="disabled"라 DT_IRQN을 쓰지 않고 리터럴로 둔다
 * (이 모듈은 Zephyr 드라이버 모델을 거치지 않고 nrfx를 직접 쓰는 설계, architecture.md §2.3).
 */
#define I2C_RTC2_IRQN         36
#define I2C_RTC2_IRQ_PRIORITY 4

K_SEM_DEFINE(sem_i2c_ready, 0, 1);

/* nrfx API >= 4.1.0: NRFX_RTC_INSTANCE()는 인스턴스 "번호"가 아니라 레지스터 베이스
 * 매크로(NRF_RTC2)를 받는다. 정수 2를 그대로 넘기면 (NRF_RTC_Type*)2로 캐스팅되어
 * 주소 0x2를 RTC 레지스터처럼 접근하게 되고, nrfx_rtc_init()의 PRESCALER 쓰기에서
 * MemManage 폴트가 난다 (2026-09-14 실기 디버깅으로 확인, k_sys_fatal_error_handler
 * ESF: pc=nrfy_rtc_periph_configure, 원인 reason=K_ERR_ARM_MEM 계열).
 */
static nrfx_rtc_t s_rtc = NRFX_RTC_INSTANCE(NRF_RTC2);

/* 5-frame 주기 중 몇 번째 프레임인지 (0..RTC_BRESENHAM_FRAME_PERIOD-1) */
static uint8_t s_bresenham_frame_index;

/* 마지막으로 설정한 CC 값 (하드웨어 readback API가 없어 소프트웨어로 추적) */
static uint32_t s_last_cc;

/* 32bit us 누적 타임스탬프. 매 인터럽트마다 정확히 100000us(=100ms) 더한다.
 * (tick 보정은 CC 재설정으로 처리하고, 타임스탬프는 항상 이론값 100ms 단위로 누적한다.)
 */
static volatile uint32_t s_timestamp_us;

static uint16_t next_cc_delta(void)
{
	uint16_t delta;

	/* 5프레임 중 1회만 HIGH(3277), 나머지 4회는 LOW(3276)
	 * 합계 = 3276*4 + 3277 = 13381 tick = 정확히 500ms (LFCLK 32.768kHz 기준)
	 */
	if (s_bresenham_frame_index == 0) {
		delta = RTC_TICK_TARGET_100MS_HIGH;
	} else {
		delta = RTC_TICK_TARGET_100MS_LOW;
	}

	s_bresenham_frame_index++;
	if (s_bresenham_frame_index >= RTC_BRESENHAM_FRAME_PERIOD) {
		s_bresenham_frame_index = 0;
	}

	return delta;
}

/* ISR context. 허용 항목만 수행: timestamp capture + semaphore give (codingstandard.md §3).
 * BLE notify/로그/flash/I2C/malloc 절대 금지.
 */
static void rtc_handler(nrf_rtc_event_t event_type, void *p_context)
{
	ARG_UNUSED(p_context);

	if (event_type != NRF_RTC_EVENT_COMPARE_0) {
		return;
	}

	/* 1) timestamp capture (다음 프레임 목표값을 이론적으로 누적) */
	s_timestamp_us += (1000000UL / SAMPLE_RATE_HZ);

	/* 2) 다음 CC 값 재설정 (Bresenham 보정, tick 단위) */
	s_last_cc = (s_last_cc + next_cc_delta()) & 0xFFFFFFUL; /* RTC counter는 24-bit */
	nrfx_rtc_cc_set(&s_rtc, 0, s_last_cc, true);

	/* 3) semaphore give만 — I2C Task를 깨운다. 그 이상은 절대 하지 않는다. */
	k_sem_give(&sem_i2c_ready);
}

/* nrfx는 Zephyr 정적 벡터테이블을 모르는 vendor HAL이라, RTC 활성화 시 CMSIS
 * NVIC_EnableIRQ()를 직접 호출한다 — 그런데 이 IRQ 번호에 Zephyr ISR이 연결돼
 * 있지 않으면(IRQ_CONNECT 누락) 실제 인터럽트 발생 시 기본 spurious 핸들러로
 * 빠져 k_fatal_error(K_ERR_SPURIOUS_IRQ)로 크래시한다. 그래서 nrfx_rtc_init()보다
 * 먼저 이 벡터를 등록해둬야 한다.
 */
static void rtc2_isr_wrapper(const void *arg)
{
	ARG_UNUSED(arg);
	nrfx_rtc_irq_handler(&s_rtc);
}

module_err_t m_i2c_rtc_init(void)
{
	int err;
	nrfx_rtc_config_t config = NRFX_RTC_DEFAULT_CONFIG;

	config.prescaler = 0; /* LFCLK 32.768kHz 그대로 사용 */

	s_bresenham_frame_index = 0;
	s_timestamp_us = 0;
	s_last_cc = RTC_TICK_TARGET_100MS_HIGH;

	IRQ_CONNECT(I2C_RTC2_IRQN, I2C_RTC2_IRQ_PRIORITY, rtc2_isr_wrapper, NULL, 0);
	irq_enable(I2C_RTC2_IRQN);

	err = nrfx_rtc_init(&s_rtc, &config, rtc_handler);
	if (err != 0) {
		return MODULE_ERR_NOT_INITIALIZED;
	}

	nrfx_rtc_cc_set(&s_rtc, 0, s_last_cc, true);

	return MODULE_ERR_OK;
}

module_err_t m_i2c_rtc_start(void)
{
	nrfx_rtc_counter_clear(&s_rtc);
	nrfx_rtc_enable(&s_rtc);
	return MODULE_ERR_OK;
}

module_err_t m_i2c_rtc_stop(void)
{
	nrfx_rtc_disable(&s_rtc);
	return MODULE_ERR_OK;
}

uint32_t m_i2c_rtc_get_timestamp_us(void)
{
	return s_timestamp_us;
}
