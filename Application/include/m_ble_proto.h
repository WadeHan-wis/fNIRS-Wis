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
 * AS7341_DATA0/DATA1(0x1526/0x1527, notify) — **v3(2026-09-21)**: 배칭 프레임으로 재설계.
 * 사용자 요청 배경: (1) LED index 필드는 순차 LED 스트로빙이 아직 미구현이라
 * 매 샘플 고정값 0만 나가는 죽은 필드였음(v2까지) — 제거. (2) 10Hz마다 매번 notify하면
 * BLE 전력 소모가 커진다는 지적 — 샘플 여러 개를 모아 한 notify로 보내는 배칭 도입.
 * NCS_TedreamS2 관례(센서 페이로드 = `[timestamp 4B LE][센서 데이터]`)는 v2에서 이미
 * 계승했고, v3는 그 record를 여러 개 이어붙이는 배치 헤더만 추가한 것이다.
 *
 *   offset 0        : sample_count (u8) — 이 프레임에 실린 레코드 개수(1 이상)
 *   offset 1..       : sample_count개의 14바이트 레코드가 이어짐. 레코드 포맷:
 *     +0-3 : timestamp_us (u32 LE)
 *     +4-7 : seq_num (u32 LE) — 부팅 세션 내내 단조증가, 재연결 후 gap 식별용
 *     +8-9 : Red630 (u16 LE)
 *     +10-11: Red680 (u16 LE)
 *     +12-13: NIR    (u16 LE)
 *
 * 배치 크기(sample_count)는 고정이 아니라 **협상된 ATT MTU에서 자동 산출**한다
 * (`m_ble.c` MTU exchange 콜백 참고) — MTU 협상 실패/구버전 중앙기기는 자동으로
 * sample_count=1(레코드 1개, 15바이트)로 축소되어 별도 레거시 포맷 없이 동일 파서로
 * 처리된다. DATA0/DATA1은 각자 자기 센서의 레코드만 담아 독립적으로 완전한 프레임을
 * 유지한다(한쪽 notify가 유실돼도 다른 쪽은 timestamp/seq_num을 그대로 보존).
 *
 * 기존 AS7341_SEQ(0x1529) notify characteristic은 **더 이상 notify하지 않는다**
 * (m_ble.c에서 호출 제거, m_ble_gatt.h 참고) — DATA0/DATA1 레코드 자체에 이미
 * seq_num이 들어있어 배칭 구조에서는 "같은 tick에 별도 SEQ notify" 개념이 맞지 않게
 * 됐다. GATT 선언 자체는 하위 호환을 위해 제거하지 않음(구독해도 그냥 아무 것도
 * 안 오는 것뿐, 비용 없음).
 *
 * cycle period/active window는 CONFIG에 저장·read-back될 뿐 아니라, RTC tick 기반
 * 게이팅(`m_i2c.c is_gate_active_tick()`)으로 실제 측정 빈도에도 반영된다 — v0.1.5(2026-
 * 09-18)에서 구현, v0.1.6에서 실기 검증 완료(CHANGELOG 참고). integration time도
 * `m_i2c_as7341_set_integration_time()`으로 반영됨(2026-09-22 100ms 설정 실기 확인).
 * TODO(open-item): location interval(offset 1)만 여전히 미사용 — CONFIG read-back
 * 값으로만 저장되고 실제 동작(측정 주기 등)에는 반영되지 않는다.
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
 *
 * v2(2026-09-21): DATA0/DATA1 프레임에 timestamp_us/seq_num을 직접 포함하도록 확장.
 * v3(2026-09-21): 배칭 프레임(헤더+가변개수 레코드)으로 재설계, LED index 제거
 * (위 AS7341_DATA0/DATA1 주석 참고) — 앱이 이 프로토콜 버전을 읽고 파서를 맞춰야 한다.
 */
#define BLE_PROTOCOL_VERSION 3

/* SEQ notify 프레임 포맷은 하위 호환을 위해 그대로 둔다 — 실제로 notify하는 코드는
 * 제거됐다(위 주석 참고, m_ble.c). */
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

/* 배칭 프레임 레코드/헤더 길이 (위 AS7341_DATA0/DATA1 v3 스펙 참고). */
#define BLE_PROTO_BATCH_RECORD_LEN 14
#define BLE_PROTO_BATCH_HEADER_LEN 1

/* MTU 247(ATT payload 244byte, architecture.md §2.4 목표치) 기준 상한 —
 * (244 - BLE_PROTO_BATCH_HEADER_LEN) / BLE_PROTO_BATCH_RECORD_LEN = 17.
 * m_ble.c가 실제 협상된 MTU로 이보다 작은 실사용 배치 크기를 매 연결마다 계산한다.
 */
#define BLE_PROTO_BATCH_MAX_SAMPLES 17
#define BLE_PROTO_MAX_PACKET_LEN \
	(BLE_PROTO_BATCH_HEADER_LEN + (BLE_PROTO_BATCH_MAX_SAMPLES * BLE_PROTO_BATCH_RECORD_LEN))

/* nirs_sample_t 배열(samples[0..count-1])을 DATA0/DATA1 배칭 프레임으로 인코드한다.
 * sensor_id: NIR_SENSOR_1 -> DATA0, NIR_SENSOR_2 -> DATA1.
 * count가 BLE_PROTO_BATCH_MAX_SAMPLES를 넘거나 out_buf_cap이 부족하면 0을 반환한다
 * (호출부가 이미 협상된 배치 크기 이하로 count를 맞추므로 방어적 체크 성격).
 */
uint16_t m_ble_proto_encode_batch(nir_sensor_id_t sensor_id, const nirs_sample_t *samples,
				   uint16_t count, uint8_t *out_buf, uint16_t out_buf_cap);

#ifdef __cplusplus
}
#endif

#endif /* M_BLE_PROTO_H_ */
