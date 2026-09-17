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

/* Safety 인증 대응(2026-09-17, architecture.md §11 항목6-[5]): AS7341_VERSION
 * characteristic(0x1528, m_ble_gatt.c)으로 노출되는 프로토콜 버전. DATA0/DATA1/CONFIG의
 * 바이트 레이아웃이 바뀌면 이 값을 올린다. 현재 이 값을 읽고 비교해서 구버전을 거부하는
 * 로직은 앱 쪽에 구현이 없다(테스트 APK는 이 characteristic 자체를 모름) — 프로토콜
 * 버전 협상은 앱이 이 값을 읽고 대응하도록 업데이트된 뒤에나 실효성이 생기는
 * 인프라이며, 지금은 "읽을 수 있게" 만드는 것까지가 이번 범위다 (architecture.md §11 참고).
 *
 * gap 식별(재구성 vs 실측 데이터 구분, architecture.md §11 항목6-[4]): DATA0/DATA1
 * 프레임에는 별도 gap 플래그를 넣을 여유가 없다(8바이트 고정, 앱 키건) — **정정
 * (2026-09-17)**: 처음엔 seq_num을 리셋 안 하는 것만으로 충분하다고 봤으나, 애초에
 * DATA0/DATA1 프레임에 seq_num 자체가 안 들어있어서 앱이 볼 방법이 없었다(과장된
 * 주장이었음, CHANGELOG v0.1.2 참고). 이를 바로잡기 위해 AS7341_SEQ(0x1529, 순수
 * 추가 characteristic, m_ble_gatt.h)를 신설 — DATA0/DATA1과 같은 tick에서 seq_num
 * (u32 LE)을 별도 notify한다(`m_ble_proto_encode_seq()`). 앱은 "가장 최근 SEQ
 * notify"와 "가장 최근 DATA0/DATA1 notify"를 같은 샘플로 짝지어서, seq_num
 * 불연속(이전값+1이 아님)이 보이면 그 구간이 "로컬 ring buffer 오버플로우로 유실된
 * 실측 구간"이라고 판단할 수 있다.
 */
#define BLE_PROTOCOL_VERSION 1

/* SEQ notify 프레임 — seq_num(u32 LE) 그대로. */
#define BLE_PROTO_SEQ_FRAME_LEN 4

uint16_t m_ble_proto_encode_seq(uint32_t seq_num, uint8_t out_buf[BLE_PROTO_SEQ_FRAME_LEN]);

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
