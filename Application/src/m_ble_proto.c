#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/sys/byteorder.h>
#include "m_ble_proto.h"
#include "m_ctrl.h"

LOG_MODULE_REGISTER(m_ble_proto, LOG_LEVEL_INF);

/* 프로토콜 문서 기본값(사용자 제공): 01 01 14 14 14 14 e8 03 c8 00
 * = integration=1(20ms), location=1s, LED1~4=20%, cycle=1000ms, active=200ms.
 */
static uint8_t s_config[BLE_PROTO_CONFIG_LEN] = {
	0x01, 0x01, 0x14, 0x14, 0x14, 0x14, 0xE8, 0x03, 0xC8, 0x00,
};

void m_ble_proto_init(void)
{
	/* s_config는 위 기본값으로 이미 정적 초기화됨 — 별도 런타임 초기화 불필요. */
}

ssize_t m_ble_proto_on_config_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				    void *buf, uint16_t len, uint16_t offset)
{
	ARG_UNUSED(conn);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, s_config, sizeof(s_config));
}

ssize_t m_ble_proto_on_config_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				     const void *buf, uint16_t len, uint16_t offset,
				     uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len != BLE_PROTO_CONFIG_LEN) {
		LOG_WRN("AS7341_CONFIG write 길이 이상: %u (기대값 %u)", len,
			BLE_PROTO_CONFIG_LEN);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(s_config, buf, BLE_PROTO_CONFIG_LEN);

	/* active window > cycle period이면 cycle로 클램프 (프로토콜 스펙 명시 제약). */
	uint16_t cycle_ms = sys_get_le16(&s_config[6]);
	uint16_t active_ms = sys_get_le16(&s_config[8]);

	if (active_ms > cycle_ms) {
		active_ms = cycle_ms;
		sys_put_le16(active_ms, &s_config[8]);
	}

	LOG_INF("AS7341_CONFIG write: integ=%u(x20ms) loc=%us LED1-4=%u/%u/%u/%u%% "
		"cycle=%ums active=%ums",
		s_config[0], s_config[1], s_config[2], s_config[3], s_config[4], s_config[5],
		cycle_ms, active_ms);

	m_ctrl_notify_as7341_config(s_config);

	return len;
}

uint16_t m_ble_proto_encode_sample(nir_sensor_id_t sensor_id, const nirs_sample_t *sample,
				    uint16_t led_index, uint8_t out_buf[BLE_PROTO_MAX_PACKET_LEN])
{
	if (sample == NULL || out_buf == NULL || sensor_id >= NIR_SENSOR_COUNT) {
		return 0;
	}

	/* Red630/Red680/NIR/LED index, 전부 u16 LE (프로토콜 스펙). raw_out 매핑은
	 * m_i2c_as7341.c에서 이미 640NM=F7(~630nm)/680NM=F8(680nm)/950NM=NIR로 확정. */
	sys_put_le16(sample->raw[sensor_id][NIRS_WAVELENGTH_640NM], &out_buf[0]);
	sys_put_le16(sample->raw[sensor_id][NIRS_WAVELENGTH_680NM], &out_buf[2]);
	sys_put_le16(sample->raw[sensor_id][NIRS_WAVELENGTH_950NM], &out_buf[4]);
	sys_put_le16(led_index, &out_buf[6]);

	return BLE_PROTO_DATA_FRAME_LEN;
}
