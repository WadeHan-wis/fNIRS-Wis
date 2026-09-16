/*
 * Module   : m_ble_proto
 * Task     : BLE Task 컨텍스트에서만 호출 (GATT read/write 콜백)
 *
 * 패킷 인코드(DATA0/DATA1 notify) / 디코드(CONFIG read/write) 담당. 파일 트리는
 * S2 관례 계승(m_ble_gatt.h 상단 주석 참고) — 바이트 포맷 자체는 S2 프로토콜이
 * 아니라 테스트 APK(fnirs-visualizer-app-release.apk) 고정 프로토콜을 따른다.
 *
 * 프로토콜 스펙 (2026-09-15, 사용자가 APK 소스를 직접 분석해서 제공 — classes.dex
 * 문자열 추정이 아니라 소스 기반 확정값):
 *
 * AS7341_CONFIG(0x1525, read+write) — 10바이트, 리틀엔디안:
 *   offset 0   : integration time (u8, 1 unit = 20ms)
 *   offset 1   : location interval (u8, seconds)
 *   offset 2-5 : LED1~4 PWM (u8 각각, 0~100%) — 이 보드는 LED가 3개(640/680/950nm)뿐이라
 *                LED1→640nm, LED2→680nm, LED3→950nm로 매핑하고 LED4는 무시한다.
 *   offset 6-7 : cycle period (u16 LE, ms)
 *   offset 8-9 : active window (u16 LE, ms) — active > cycle이면 cycle로 클램프
 *
 * AS7341_DATA0/DATA1(0x1526/0x1527, notify) — 8바이트 프레임 1개 이상 배칭:
 *   offset 0-1 : Red630 (u16 LE)
 *   offset 2-3 : Red680 (u16 LE)
 *   offset 4-5 : NIR    (u16 LE)
 *   offset 6-7 : LED index (u16 LE)
 *
 * TODO(open-item): cycle period/active window는 CONFIG에 저장·read-back은 하지만
 * 아직 RTC 샘플링 주기(config_app.h 고정 100ms/10Hz, Bresenham 보정)에는 반영하지
 * 않는다 — 검증된 고정 타이밍 로직을 성급하게 일반화하지 않기 위해 의도적으로 보류
 * (architecture.md §11 기록 예정). location interval도 현재 미사용.
 */
#ifndef M_BLE_PROTO_H_
#define M_BLE_PROTO_H_

#include <zephyr/types.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include "nirs_sample.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_PROTO_CONFIG_LEN 10

void m_ble_proto_init(void);

/* AS7341_CONFIG read/write 콜백 (m_ble_gatt.c의 BT_GATT_CHARACTERISTIC에서 그대로
 * 전달). write는 파싱 후 m_ctrl_notify_as7341_config()로 m_i2c에 중계한다
 * (Acquisition↔BLE는 ring buffer로만 통신 원칙 — architecture.md §2.2). read는
 * 마지막으로 적용된(또는 기본) 설정 10바이트를 그대로 되돌려준다.
 */
ssize_t m_ble_proto_on_config_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				    void *buf, uint16_t len, uint16_t offset);
ssize_t m_ble_proto_on_config_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				     const void *buf, uint16_t len, uint16_t offset,
				     uint8_t flags);

/* nirs_sample_t 한 개를 DATA0/DATA1 notify 8바이트 프레임으로 인코드한다.
 * sensor_id: NIR_SENSOR_1 -> DATA0, NIR_SENSOR_2 -> DATA1.
 * led_index: 이번 샘플이 어느 LED 점등 중 측정됐는지 (프로토콜 스펙의 LED index 필드).
 */
#define BLE_PROTO_DATA_FRAME_LEN 8
#define BLE_PROTO_MAX_PACKET_LEN BLE_PROTO_DATA_FRAME_LEN

uint16_t m_ble_proto_encode_sample(nir_sensor_id_t sensor_id, const nirs_sample_t *sample,
				    uint16_t led_index, uint8_t out_buf[BLE_PROTO_MAX_PACKET_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* M_BLE_PROTO_H_ */
