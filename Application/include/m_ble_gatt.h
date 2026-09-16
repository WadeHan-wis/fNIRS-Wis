/*
 * Module   : m_ble_gatt
 * Task     : BLE Task 컨텍스트에서만 호출 (GATT 서비스/attribute 정의 + 등록)
 *
 * 파일 트리는 S2(NCS_TedreamS2) 관례를 계승한다 — m_ble(태스크/광고/연결) /
 * m_ble_gatt(서비스·characteristic 선언) / m_ble_proto(패킷 인코드·디코드) 3분할
 * (agents.md §4: S2 "구조"는 계승하되 코드/프로토콜 내용은 그대로 베끼지 않는다).
 *
 * UUID/characteristic 구성은 S2 고유 프로토콜이 아니라, 이미 컴파일된 테스트 APK
 * (fnirs-visualizer-app-release.apk)가 요구하는 고정된 값이다 — APK classes.dex
 * 문자열 분석(2026-09-15, jadx/apktool 없이 grep -a로 UUID/식별자 추출)으로 확인:
 *   서비스 nRF_fNIRS_Sys_v2 / AS7341_SERVICE_UUID / AS7341_CONFIG_UUID /
 *   AS7341_DATA0_UUID / AS7341_DATA1_UUID (sensor0/sensor1 = NIR1/NIR2 대응)
 *
 * TODO(open-item): 4개 UUID 값(0x1523/1525/1526/1527)과 4개 이름(SERVICE/CONFIG/
 * DATA0/DATA1)은 각각 grep으로만 확인했고 "어느 값이 어느 이름인지"는 아직 실기로
 * 교차검증 못 했다 — 아래 매핑은 이름의 알파벳/선언 순서 기준 추정이다. 실기에서
 * APK가 실제로 어느 UUID에 write/subscribe하는지 RTT 로그로 확인 후 확정한다.
 */
#ifndef M_BLE_GATT_H_
#define M_BLE_GATT_H_

#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>

/* TODO(open-item, 검증 전): 아래 4개 중 SERVICE만 확정, CONFIG/DATA0/DATA1 순서는 추정. */
#define BT_UUID_AS7341_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x00001523, 0x1212, 0xefde, 0x1523, 0x785feabcd123)
#define BT_UUID_AS7341_CONFIG_VAL \
	BT_UUID_128_ENCODE(0x00001525, 0x1212, 0xefde, 0x1523, 0x785feabcd123)
#define BT_UUID_AS7341_DATA0_VAL \
	BT_UUID_128_ENCODE(0x00001526, 0x1212, 0xefde, 0x1523, 0x785feabcd123)
#define BT_UUID_AS7341_DATA1_VAL \
	BT_UUID_128_ENCODE(0x00001527, 0x1212, 0xefde, 0x1523, 0x785feabcd123)

#define BT_UUID_AS7341_SERVICE BT_UUID_DECLARE_128(BT_UUID_AS7341_SERVICE_VAL)
#define BT_UUID_AS7341_CONFIG  BT_UUID_DECLARE_128(BT_UUID_AS7341_CONFIG_VAL)
#define BT_UUID_AS7341_DATA0   BT_UUID_DECLARE_128(BT_UUID_AS7341_DATA0_VAL)
#define BT_UUID_AS7341_DATA1   BT_UUID_DECLARE_128(BT_UUID_AS7341_DATA1_VAL)

/* GATT 서비스는 m_ble_gatt.c에 BT_GATT_SERVICE_DEFINE으로 정적 등록된다.
 * m_ble.c는 이 파일의 함수만으로 advertising/notify를 다룬다 — attribute 배열
 * 인덱스 등 GATT 세부사항을 다른 모듈이 알 필요는 없다.
 */
void m_ble_gatt_init(void);

/* DATA0(NIR1)/DATA1(NIR2) notify. conn이 NULL이면 아무 것도 하지 않는다(연결 전 호출 방지). */
int m_ble_gatt_notify_data0(struct bt_conn *conn, const uint8_t *data, uint16_t len);
int m_ble_gatt_notify_data1(struct bt_conn *conn, const uint8_t *data, uint16_t len);

#endif /* M_BLE_GATT_H_ */
