#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include "m_ble_gatt.h"
#include "m_ble_proto.h"
#include "config_app.h"

LOG_MODULE_REGISTER(m_ble_gatt, LOG_LEVEL_INF);

/* Safety 인증 대응(2026-09-17, architecture.md §11 항목6-[5]): {fw_version, protocol_version}
 * 4바이트, u16 LE 2개. read-only라 write 콜백/CCC 불필요. */
static ssize_t on_version_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				uint16_t len, uint16_t offset)
{
	ARG_UNUSED(conn);

	uint8_t value[4];

	sys_put_le16(FW_VERSION_PACKED, &value[0]);
	sys_put_le16(BLE_PROTOCOL_VERSION, &value[2]);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

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

static void seq_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	LOG_INF("SEQ(gap 식별) notify %s", value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

/* attrs[] 인덱스 (Zephyr BT_GATT_SERVICE_DEFINE 관례 — 아래 배열 순서와 반드시 일치):
 *   0: Primary Service
 *   1: CONFIG characteristic decl   2: CONFIG value (write)
 *   3: DATA0 characteristic decl    4: DATA0 value (notify)   5: DATA0 CCC
 *   6: DATA1 characteristic decl    7: DATA1 value (notify)   8: DATA1 CCC
 *   9: VERSION characteristic decl 10: VERSION value (read, 2026-09-17 추가)
 *  11: SEQ characteristic decl     12: SEQ value (notify)     13: SEQ CCC (2026-09-17 추가)
 * m_ble_gatt_notify_data0/1/seq()가 이 인덱스에 직접 의존한다 — 배열 순서를 바꾸면 함께 수정.
 * VERSION/SEQ는 맨 뒤에 추가했으므로(additive) 기존 인덱스(0~8)는 전혀 변경되지 않는다.
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

	BT_GATT_CHARACTERISTIC(BT_UUID_AS7341_VERSION,
				BT_GATT_CHRC_READ,
				BT_GATT_PERM_READ,
				on_version_read, NULL, NULL),

	BT_GATT_CHARACTERISTIC(BT_UUID_AS7341_SEQ,
				BT_GATT_CHRC_NOTIFY,
				BT_GATT_PERM_NONE,
				NULL, NULL, NULL),
	BT_GATT_CCC(seq_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

#define ATTR_IDX_DATA0_VALUE 4
#define ATTR_IDX_DATA1_VALUE 7
#define ATTR_IDX_SEQ_VALUE 12

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

int m_ble_gatt_notify_seq(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
	if (conn == NULL) {
		return -ENOTCONN;
	}

	return bt_gatt_notify(conn, &as7341_svc.attrs[ATTR_IDX_SEQ_VALUE], data, len);
}
