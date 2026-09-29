#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/logging/log.h>
#include "m_ctrl.h"
#include "config_app.h"

LOG_MODULE_REGISTER(m_ctrl, LOG_LEVEL_INF);

#define CTRL_MSGQ_MAX_MSGS 16

typedef struct {
	module_err_t err;
} ctrl_msg_t;

K_MSGQ_DEFINE(ctrl_msgq, sizeof(ctrl_msg_t), CTRL_MSGQ_MAX_MSGS, 4);

static ctrl_state_t g_ctrl_status = CTRL_STATE_NORMAL;
static ctrl_reset_reason_t s_last_reset_reason;

/* BLE 연결 상태 중계용 (m_ble → m_ctrl → m_i2c).
 * Acquisition(m_i2c)과 BLE는 ring buffer로만 통신해야 하므로(architecture.md §2.2,
 * codingstandard.md §4), 둘을 직접 잇지 않고 CTRL을 경유한다. bool 값 하나를 단순
 * 대입/읽기만 하므로(복합 연산 없음) 별도 mutex 없이 volatile bool로 충분하다.
 */
static volatile bool g_ble_connected;

/* AS7341_CONFIG 중계용 (m_ble → m_ctrl → m_i2c). 다중 바이트라 mutex로 보호한다
 * (BLE Task가 쓰고 I2C Task가 tick마다 읽는 cross-task 공유 데이터). */
K_MUTEX_DEFINE(mtx_as7341_config);
static uint8_t s_as7341_config[CTRL_AS7341_CONFIG_LEN];
static bool s_as7341_config_pending;

void m_ctrl_report_error(module_err_t err)
{
	ctrl_msg_t msg = { .err = err };

	/* 큐가 가득 차도 blocking하지 않는다 (호출부가 I2C Task일 수 있음) */
	(void)k_msgq_put(&ctrl_msgq, &msg, K_NO_WAIT);
}

void m_ctrl_request_reset(ctrl_reset_reason_t reason)
{
	s_last_reset_reason = reason;
	g_ctrl_status = CTRL_STATE_FAULT;

	/* CTRL 상태머신을 경유하는 유일한 리셋 호출 지점 (architecture.md §3) */
	sys_reboot(SYS_REBOOT_WARM);
}

ctrl_state_t m_ctrl_get_state(void)
{
	return g_ctrl_status;
}

bool m_ctrl_is_safe_state(void)
{
	return g_ctrl_status == CTRL_STATE_DEGRADED || g_ctrl_status == CTRL_STATE_FAULT;
}

void m_ctrl_notify_ble_connected(void)
{
	g_ble_connected = true;
}

void m_ctrl_notify_ble_disconnected(void)
{
	g_ble_connected = false;
}

bool m_ctrl_is_ble_connected(void)
{
	return g_ble_connected;
}

void m_ctrl_notify_as7341_config(const uint8_t config[CTRL_AS7341_CONFIG_LEN])
{
	k_mutex_lock(&mtx_as7341_config, K_FOREVER);
	memcpy(s_as7341_config, config, CTRL_AS7341_CONFIG_LEN);
	s_as7341_config_pending = true;
	k_mutex_unlock(&mtx_as7341_config);
}

bool m_ctrl_take_as7341_config(uint8_t out[CTRL_AS7341_CONFIG_LEN])
{
	bool had_pending;

	k_mutex_lock(&mtx_as7341_config, K_FOREVER);
	had_pending = s_as7341_config_pending;
	if (had_pending) {
		memcpy(out, s_as7341_config, CTRL_AS7341_CONFIG_LEN);
		s_as7341_config_pending = false;
	}
	k_mutex_unlock(&mtx_as7341_config);

	return had_pending;
}

/* Watchdog (Rev3, R3-1) — config_app.h WATCHDOG_* 매크로 상단 주석 참고.
 * s_last_alive_ms는 여러 태스크가 쓰고 CTRL 태스크만 읽는다. 단순 대입/읽기뿐이고
 * (증감 연산 없음) ARM에서 정렬된 32bit 접근은 원자적이라 별도 mutex 없이
 * volatile로 충분하다 (g_ble_connected와 동일한 근거, codingstandard.md §1).
 */
static const struct device *s_wdt_dev = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int s_wdt_channel_id = -1;
static volatile uint32_t s_last_alive_ms[CTRL_ALIVE_SOURCE_COUNT];

/* 모듈 오류 재부팅 에스컬레이션(2026-09-29, config_app.h CTRL_MODULE_FAULT_REBOOT_MAX
 * 참고) — 재부팅 횟수를 GPREGRET(POR 전에는 지워지지 않음)에 저장한다. gpregret1은
 * nRF52832 devicetree에 이미 status="okay"로 정의돼 있어 별도 overlay 불필요
 * (prj.conf CONFIG_RETAINED_MEM=y만 추가). */
static const struct device *s_gpregret_dev = DEVICE_DT_GET(DT_NODELABEL(gpregret1));

/* GPREGRET 1바이트를 재부팅 횟수 카운터로 사용 — read/write 실패(디바이스 not-ready 등)
 * 시에는 보수적으로 "이미 상한에 도달한 것"으로 간주해 곧장 영구 안전상태로 보낸다
 * (카운터를 못 믿는 상태에서 재부팅을 계속 반복하는 것보다 안전). */
static uint8_t read_fault_reboot_count(void)
{
	uint8_t count = CTRL_MODULE_FAULT_REBOOT_MAX;

	if (!device_is_ready(s_gpregret_dev)) {
		return count;
	}

	if (retained_mem_read(s_gpregret_dev, 0, &count, sizeof(count)) != 0) {
		return CTRL_MODULE_FAULT_REBOOT_MAX;
	}

	return count;
}

static void write_fault_reboot_count(uint8_t count)
{
	if (!device_is_ready(s_gpregret_dev)) {
		return;
	}

	(void)retained_mem_write(s_gpregret_dev, 0, &count, sizeof(count));
}

void m_ctrl_notify_alive(ctrl_alive_source_t src)
{
	if (src < CTRL_ALIVE_SOURCE_COUNT) {
		s_last_alive_ms[src] = k_uptime_get_32();
	}
}

/* CTRL Task 진입 직후 1회 수행 (S2 task_i2c 관례: 드라이버 init은 생성된 task 안에서).
 * 실패해도 CTRL 태스크 자체는 계속 동작해야 하므로(에러 큐 처리는 watchdog과 무관) 에러만
 * 보고하고 계속 진행 — s_wdt_channel_id가 음수로 남아 feed_watchdog_if_alive()가 아무것도
 * 하지 않는다(watchdog 보호가 없는 상태로 동작, 완전히 멈추지는 않음).
 */
/* Safety 인증 대응(2026-09-17): 이전 부팅이 watchdog 리셋이었는지 RESETREAS 레지스터로
 * 확인해서 사후 분석용으로 남긴다. 이 프로젝트는 리셋 원인과 무관하게 항상 안전한 기본
 * 상태(I2C_LED_MODE_DEVICE_ON, m_i2c.c i2c_init() 참고)에서 부팅하므로 "이전 상태 복구"
 * 로직 자체는 필요 없다 — 이 함수는 "왜 리셋됐는지"를 로그로 남기는 것이 목적이다
 * (architecture.md §11 항목6 참고). ISR이 아니라 태스크 컨텍스트에서 1회만 호출.
 */
static void log_reset_cause(void)
{
	uint32_t cause = 0;
	int err = hwinfo_get_reset_cause(&cause);

	if (err != 0) {
		LOG_WRN("hwinfo_get_reset_cause 실패 (err=%d) — 리셋 원인 확인 불가", err);
		return;
	}

	if (cause & RESET_WATCHDOG) {
		LOG_WRN("직전 부팅 리셋 원인: Watchdog (RESETREAS=0x%08X) — "
			"안전한 기본 상태(DEVICE_ON)로 재시작", cause);
	} else {
		LOG_INF("직전 부팅 리셋 원인 플래그: 0x%08X", cause);
	}

	(void)hwinfo_clear_reset_cause();

	/* [2026-09-29] 모듈 오류 재부팅 에스컬레이션(escalate_or_reboot()) 진단용 —
	 * 이 카운트는 물리 전원 재투입 전까지 리셋 사이 계속 유지된다(GPREGRET). */
	uint8_t fault_reboot_count = read_fault_reboot_count();

	if (fault_reboot_count > 0) {
		LOG_WRN("모듈 오류 재부팅 카운트: %u/%u (물리 전원 재투입 전까지 유지)",
			fault_reboot_count, CTRL_MODULE_FAULT_REBOOT_MAX);
	}
}

static void watchdog_init(void)
{
	if (!device_is_ready(s_wdt_dev)) {
		LOG_ERR("Watchdog device not ready — watchdog 보호 없이 동작");
		return;
	}

	struct wdt_timeout_cfg cfg = {
		.window = { .min = 0, .max = WATCHDOG_TIMEOUT_MS },
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
	};

	int channel_id = wdt_install_timeout(s_wdt_dev, &cfg);

	if (channel_id < 0) {
		LOG_ERR("wdt_install_timeout failed (err=%d)", channel_id);
		return;
	}

	/* [버그 수정, 2026-09-28] WDT_OPT_PAUSE_HALTED_BY_DBG 없이 wdt_setup(dev, 0)을
	 * 쓰면 nRF WDT 드라이버가 NRF_WDT_BEHAVIOUR_RUN_HALT_MASK를 세팅한다(wdt_nrfx.c) —
	 * 즉 SWD 디버거가 코어를 halt시켜도 워치독 카운터가 계속 돈다. RTT/디버그 세션을
	 * BLE 연동과 동시에 켜두면(사용자가 실기로 재현: RTT 연동 시에만 Watchdog 리셋
	 * 발생, RTT 없이 BLE만 쓰면 재현 안 됨) 디버거의 짧은 halt들이 누적돼 4초를
	 * 넘기면서 애플리케이션은 실제로 멈춘 적이 없는데도 워치독이 리셋시킨다. 이
	 * 옵션을 켜면 디버거로 halt된 동안만 워치독 카운터가 같이 멈춘다 — 디버거가
	 * 안 붙어있는 정상/양산 동작에서는 이 옵션이 아무 영향도 없다(원래 목적인
	 * "진짜로 멈춘 태스크 감지"는 그대로 유지).
	 */
	int err = wdt_setup(s_wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);

	if (err != 0) {
		LOG_ERR("wdt_setup failed (err=%d)", err);
		return;
	}

	s_wdt_channel_id = channel_id;
	LOG_INF("Watchdog armed (timeout=%ums)", WATCHDOG_TIMEOUT_MS);
}

/* m_i2c/m_ble Task 둘 다 최근 WATCHDOG_ALIVE_STALE_MS 이내에 살아있었을 때만 feed한다.
 * 한쪽이라도 stale하면 feed를 건너뛰어 하드웨어 watchdog이 자연히 만료되게 둔다 —
 * 이게 이 워치독의 유일한 목적(정상 진행 못 하는 태스크가 있으면 SoC 전체를 리셋).
 */
static void feed_watchdog_if_alive(void)
{
	if (s_wdt_channel_id < 0) {
		return;
	}

	uint32_t now = k_uptime_get_32();

	for (int i = 0; i < CTRL_ALIVE_SOURCE_COUNT; i++) {
		if ((now - s_last_alive_ms[i]) > WATCHDOG_ALIVE_STALE_MS) {
			return;
		}
	}

	wdt_feed(s_wdt_dev, s_wdt_channel_id);
}

/* 모듈 오류 재부팅 에스컬레이션(2026-09-29, config_app.h CTRL_MODULE_FAULT_REBOOT_MAX
 * 상단 주석 참고, TedreamS2 재시도 로직을 우리 구조에 맞게 변형 적용) — 예전에는(v0.1.31
 * 까지) 이 함수로 들어오는 오류가 확인 즉시 CTRL_STATE_DEGRADED로 영구 래치됐는데,
 * 이제는 GPREGRET에 저장된 재부팅 횟수가 상한(CTRL_MODULE_FAULT_REBOOT_MAX) 미만이면
 * 먼저 소프트웨어 재부팅으로 복구를 시도하고, 상한에 도달했을 때만 영구 안전상태로
 * 전환한다. GPREGRET는 POR(물리 전원 재투입) 전에는 지워지지 않으므로, 재부팅을
 * 반복해도 카운트가 유지된다 — "사용자가 물리적으로 전원을 껐다 켜야만" 다시 3번의
 * 재시도 기회가 생긴다. */
static void escalate_or_reboot(module_err_t err)
{
	uint8_t reboot_count = read_fault_reboot_count();

	if (reboot_count < CTRL_MODULE_FAULT_REBOOT_MAX) {
		write_fault_reboot_count(reboot_count + 1);
		LOG_ERR("모듈 오류(err=%d) 감지 — 재부팅으로 복구 시도 (%u/%u)", err,
			reboot_count + 1, CTRL_MODULE_FAULT_REBOOT_MAX);
		m_ctrl_request_reset(CTRL_RESET_REASON_FAULT); /* 이후로는 복귀하지 않음 */
		return;
	}

	LOG_ERR("모듈 오류(err=%d) — 재부팅 %u회 모두 실패, 영구 안전상태로 전환 "
		"(물리 전원 재투입 전까지 유지)", err, CTRL_MODULE_FAULT_REBOOT_MAX);
	g_ctrl_status = CTRL_STATE_DEGRADED;
}

static void handle_error(module_err_t err)
{
	switch (err) {
	case MODULE_ERR_OK:
		return;

	case MODULE_ERR_RING_BUFFER_OVERFLOW:
	case MODULE_ERR_SENSOR_SATURATION:
	case MODULE_ERR_SENSOR_LOW_SIGNAL:
	case MODULE_ERR_BLE_DISCONNECTED:
		if (g_ctrl_status == CTRL_STATE_NORMAL) {
			g_ctrl_status = CTRL_STATE_WARNING;
		}
		break;

	case MODULE_ERR_I2C_TIMEOUT:
	case MODULE_ERR_I2C_NACK:
	case MODULE_ERR_BLE_TX_FAILED:
	case MODULE_ERR_BLE_RETRY_EXCEEDED:
		escalate_or_reboot(err);
		break;

	case MODULE_ERR_WATCHDOG_TRIGGERED:
		m_ctrl_request_reset(CTRL_RESET_REASON_WATCHDOG);
		break;

	default:
		escalate_or_reboot(err);
		break;
	}
}

void m_ctrl_task_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	log_reset_cause();
	watchdog_init();

	ctrl_msg_t msg;

	/* K_FOREVER 대신 주기적으로 깨어나야 m_i2c/m_ble 생존 여부를 확인하고 watchdog을
	 * feed할 수 있다 — 에러 메시지 처리(msgq)와 watchdog feed를 같은 루프에서 함께 돈다. */
	while (1) {
		if (k_msgq_get(&ctrl_msgq, &msg, K_MSEC(WATCHDOG_CHECK_PERIOD_MS)) == 0) {
			handle_error(msg.err);
		}
		feed_watchdog_if_alive();
	}
}

/* main()에서 생성하지 않는다 — 커널 시작 시 자동 등록/스케줄링된다. */
K_THREAD_DEFINE(m_ctrl_tid, CTRL_TASK_STACK_SIZE, m_ctrl_task_entry, NULL, NULL, NULL,
		 CTRL_TASK_PRIORITY, 0, 0);
