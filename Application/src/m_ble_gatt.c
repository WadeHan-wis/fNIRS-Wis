#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "m_ble_gatt.h"
#include "m_ble_proto.h"

LOG_MODULE_REGISTER(m_ble_gatt, LOG_LEVEL_INF);

static void data0_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	LOG_INF("DATA0(NIR1) notify %s", value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

static void data1_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	LOG_INF("DATA1(NIR2) notify %s", value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

/* attrs[] 인덱스 (Zephyr BT_GATT_SERVICE_DEFINE 관례 — 아래 배열 순서와 반드시 일치):
 *   0: Primary Service
 *   1: CONFIG characteristic decl   2: CONFIG value (write)
 *   3: DATA0 characteristic decl    4: DATA0 value (notify)   5: DATA0 CCC
 *   6: DATA1 characteristic decl    7: DATA1 value (notify)   8: DATA1 CCC
 * m_ble_gatt_notify_data0/1()이 이 인덱스에 직접 의존한다 — 배열 순서를 바꾸면 함께 수정.
 */
/* 프로토콜 확정(2026-09-15, 사용자가 APK 소스 직접 분석): CONFIG(0x1525)만
 * read+write, DATA0/DATA1(0x1526/0x1527)은 notify-only. m_ble_proto.h 상단 주석
 * 참고. (이전에 UUID 매핑을 리버싱하려고 셋 다에 write를 걸어뒀던 TEMP 코드는
 * 매핑이 확정되어 제거함.)
 */
BT_GATT_SERVICE_DEFINE(as7341_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_AS7341_SERVICE),

	BT_GATT_CHARACTERISTIC(BT_UUID_AS7341_CONFIG,
				BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
					BT_GATT_CHRC_WRITE_WITHOUT_RESP,
				BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
				m_ble_proto_on_config_read, m_ble_proto_on_config_write, NULL),

	BT_GATT_CHARACTERISTIC(BT_UUID_AS7341_DATA0,
				BT_GATT_CHRC_NOTIFY,
				BT_GATT_PERM_NONE,
				NULL, NULL, NULL),
	BT_GATT_CCC(data0_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	BT_GATT_CHARACTERISTIC(BT_UUID_AS7341_DATA1,
				BT_GATT_CHRC_NOTIFY,
				BT_GATT_PERM_NONE,
				NULL, NULL, NULL),
	BT_GATT_CCC(data1_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

#define ATTR_IDX_DATA0_VALUE 4
#define ATTR_IDX_DATA1_VALUE 7

void m_ble_gatt_init(void)
{
	/* BT_GATT_SERVICE_DEFINE은 정적 매크로라 bt_enable() 시점에 자동 등록된다 —
	 * 이 함수는 향후 동적 등록/재등록이 필요해질 경우를 위한 확장 지점으로 남겨둔다.
	 */
	LOG_INF("AS7341 GATT service ready (service/config/data0/data1)");
}

int m_ble_gatt_notify_data0(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
	if (conn == NULL) {
		return -ENOTCONN;
	}

	return bt_gatt_notify(conn, &as7341_svc.attrs[ATTR_IDX_DATA0_VALUE], data, len);
}

int m_ble_gatt_notify_data1(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
	if (conn == NULL) {
		return -ENOTCONN;
	}

	return bt_gatt_notify(conn, &as7341_svc.attrs[ATTR_IDX_DATA1_VALUE], data, len);
}
