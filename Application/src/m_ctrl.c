#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include "m_ctrl.h"
#include "config_app.h"

#define CTRL_MSGQ_MAX_MSGS 16

typedef struct {
	module_err_t err;
} ctrl_msg_t;

K_MSGQ_DEFINE(ctrl_msgq, sizeof(ctrl_msg_t), CTRL_MSGQ_MAX_MSGS, 4);

static ctrl_state_t g_ctrl_status = CTRL_STATE_NORMAL;
static ctrl_reset_reason_t s_last_reset_reason;

/* BLE 연결 상태 중계용 (m_ble → m_ctrl → m_i2c).
 * Acquisition(m_i2c)과 BLE는 ring buffer로만 통신해야 하므로(architecture.md §2.2,
 * codingstandard.md §4), 둘을 직접 잇지 않고 CTRL을 경유한다. 단조 플래그(false→true만,
 * 재연결/해제는 Rev2 범위)라 별도 mutex 없이 volatile bool로 충분하다.
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

void m_ctrl_notify_ble_connected(void)
{
	g_ble_connected = true;
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
		g_ctrl_status = CTRL_STATE_DEGRADED;
		break;

	case MODULE_ERR_WATCHDOG_TRIGGERED:
		m_ctrl_request_reset(CTRL_RESET_REASON_WATCHDOG);
		break;

	default:
		g_ctrl_status = CTRL_STATE_DEGRADED;
		break;
	}
}

void m_ctrl_task_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	ctrl_msg_t msg;

	while (1) {
		if (k_msgq_get(&ctrl_msgq, &msg, K_FOREVER) == 0) {
			handle_error(msg.err);
		}
	}
}

/* main()에서 생성하지 않는다 — 커널 시작 시 자동 등록/스케줄링된다. */
K_THREAD_DEFINE(m_ctrl_tid, CTRL_TASK_STACK_SIZE, m_ctrl_task_entry, NULL, NULL, NULL,
		 CTRL_TASK_PRIORITY, 0, 0);
