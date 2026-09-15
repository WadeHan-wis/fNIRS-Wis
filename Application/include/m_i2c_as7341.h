/*
 * Rev1 구현 — NIR1(&i2c1, SCL=P0.09/SDA=P0.10)/NIR2(&i2c0, SCL=P0.08/SDA=P0.07) 공용
 * 드라이버. 보드에 AS7341이 2개, 각각 독립 I2C 버스로 연결돼 있어(pinmap.md §3) 인스턴스
 * 단위(m_i2c_as7341_dev_t)로 상태를 분리한다 — I2C 주소(0x39)는 두 버스에서 동일해도
 * 물리적으로 다른 버스라 충돌하지 않는다.
 * Module   : m_i2c_as7341
 * Task     : I2C Task 컨텍스트에서만 호출 (I2C blocking, ISR 호출 금지)
 *
 * AS7341 (ams OSRAM)로부터 파장별 raw intensity를 읽는다.
 * gain/integration time은 초기 검증 단계에서 고정값 사용 (architecture.md §4/§5).
 *
 * 채널-파장 매핑 (2026-09-15, AS7341 datasheet DS000504 v3-00 Figure 7/22로 검증):
 * 우리 프로젝트 파장(640/680/950nm)에 가장 가까운 필터는 F7(630nm)/F8(680nm)/NIR(910nm)이다
 * (F1~F4는 415~515nm대라 무관). 따라서 SMUX는 F5~F8+Clear+NIR 그룹으로 구성해야 한다.
 *
 * TODO(open-item): SMUX RAM 20바이트 정확한 값은 이 datasheet에 없다(ams AN000633 앱노트
 * 별도 필요) — 공개 레퍼런스 구현(Adafruit AS7341 드라이버 등) 기준값을 우선 적용했다.
 * 실기로 채널 반응(자사 보드 LED 점등 시 해당 채널 값 상승)은 확인했으나, ams 공식
 * 앱노트로 바이트 단위 교차검증은 아직 안 됨 (architecture.md §11 항목).
 */
#ifndef M_I2C_AS7341_H_
#define M_I2C_AS7341_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h> /* BIT() */
#include "module_err.h"
#include "config_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AS7341 (ams OSRAM) 레지스터 맵 — 데이터시트 DS000504 §10 기준. */
#define AS7341_I2C_ADDR       0x39

#define AS7341_REG_ENABLE     0x80
#define AS7341_REG_ATIME      0x81
#define AS7341_REG_CFG1       0xAA
#define AS7341_REG_CFG6       0xAF
#define AS7341_REG_STATUS2    0xA3
#define AS7341_REG_ASTEP_L    0xCA
#define AS7341_REG_ASTEP_H    0xCB
#define AS7341_REG_CH0_DATA_L 0x95
#define AS7341_REG_ID         0x92

#define AS7341_ENABLE_PON     BIT(0)
#define AS7341_ENABLE_SP_EN   BIT(1)
#define AS7341_ENABLE_SMUXEN  BIT(4)

#define AS7341_STATUS2_AVALID BIT(6)

#define AS7341_ID_EXPECTED_MASK  0xFC
#define AS7341_ID_EXPECTED_VALUE 0x24

#define AS7341_SMUX_CHANNEL_COUNT 6
#define AS7341_MEASURE_TIMEOUT_MS 200

/* SMUX가 F5~F8+Clear+NIR로 구성됐을 때의 ADC 채널(CH0~CH5) 배치
 * (datasheet §8.1/8.2 + 레퍼런스 SMUX 설정 기준):
 *   CH0=F5(555nm) CH1=F6(590nm) CH2=F7(630nm) CH3=F8(680nm) CH4=Clear CH5=NIR(910nm)
 */
#define AS7341_CH_F5    0
#define AS7341_CH_F6    1
#define AS7341_CH_F7    2
#define AS7341_CH_F8    3
#define AS7341_CH_CLEAR 4
#define AS7341_CH_NIR   5

/* 인스턴스 상태 — NIR1/NIR2 각각 1개씩 정적으로 소유(m_i2c.c). 호출부가 struct 내부
 * 필드를 직접 건드리지 않는다(불투명 핸들처럼 사용).
 */
typedef struct {
	const struct device *i2c_dev;
	bool initialized;
} m_i2c_as7341_dev_t;

/* dev: 호출부가 소유한 인스턴스 상태. i2c_bus: DEVICE_DT_GET(DT_NODELABEL(i2c0/i2c1)) 결과. */
module_err_t m_i2c_as7341_init(m_i2c_as7341_dev_t *dev, const struct device *i2c_bus);

/* gain/integration time 설정. 런타임 변경 시 호출부에서 반드시 변경 시점을 로그/패킷에
 * 기록해야 한다 (codingstandard.md §6, 누락 시 리뷰 반려 사유).
 */
module_err_t m_i2c_as7341_set_gain(m_i2c_as7341_dev_t *dev, uint16_t gain);
module_err_t m_i2c_as7341_set_integration_time(m_i2c_as7341_dev_t *dev, uint16_t integration_time);

/* 채널별 raw intensity 1회 읽기 (3파장 raw_out[NIRS_WAVELENGTH_COUNT]에 채움).
 * 포화/저신호 감지 시 MODULE_ERR_SENSOR_SATURATION / MODULE_ERR_SENSOR_LOW_SIGNAL 반환.
 * TODO(open-item): 채널별 saturation 임계 게인 실측 필요 (architecture.md §11).
 */
module_err_t m_i2c_as7341_read_raw(m_i2c_as7341_dev_t *dev, uint16_t raw_out[NIRS_WAVELENGTH_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* M_I2C_AS7341_H_ */
