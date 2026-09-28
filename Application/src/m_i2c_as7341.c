#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include "m_i2c_as7341.h"
#include "config_app.h"

#if TEMP_AS7341_RAW_DBG_LOG
LOG_MODULE_REGISTER(m_i2c_as7341, LOG_LEVEL_DBG);
#else
LOG_MODULE_REGISTER(m_i2c_as7341, LOG_LEVEL_INF);
#endif

/* SMUX RAM 20바이트 (레지스터 0x00~0x13) — F5~F8+Clear+NIR 구성 (m_i2c_as7341.h 상단
 * 매핑 주석 참고). datasheet DS000504에는 SMUX 바이트 표가 없어(ams AN000633 앱노트
 * 별도 필요) 공개 레퍼런스 구현(Adafruit AS7341 드라이버) 값을 우선 적용했다.
 * TODO(open-item): ams 공식 앱노트로 바이트 단위 교차검증 필요 (architecture.md §11).
 * NIR1/NIR2 둘 다 동일한 F5~F8+Clear+NIR 구성을 사용한다(공용 상수).
 */
static const uint8_t s_smux_config_f5f8_clear_nir[20] = {
	0x00, 0x00, 0x00, 0x40, 0x02, 0x00,
	0x10, 0x03, 0x50, 0x10, 0x03, 0x00,
	0x00, 0x00, 0x24, 0x00, 0x00, 0x50,
	0x00, 0x06,
};

static int as7341_reg_write(m_i2c_as7341_dev_t *dev, uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(dev->i2c_dev, AS7341_I2C_ADDR, reg, val);
}

static int as7341_reg_read(m_i2c_as7341_dev_t *dev, uint8_t reg, uint8_t *val)
{
	return i2c_reg_read_byte(dev->i2c_dev, AS7341_I2C_ADDR, reg, val);
}

static int as7341_configure_smux(m_i2c_as7341_dev_t *dev)
{
	int err;

	/* SMUX RAM(0x00~0x13)에 구성값 기록 */
	err = i2c_burst_write(dev->i2c_dev, AS7341_I2C_ADDR, 0x00,
			       s_smux_config_f5f8_clear_nir,
			       sizeof(s_smux_config_f5f8_clear_nir));
	if (err != 0) {
		return err;
	}

	/* CFG6: SMUX_CMD = write RAM to SMUX chain (0b10 << 3) */
	err = as7341_reg_write(dev, AS7341_REG_CFG6, (2 << 3));
	if (err != 0) {
		return err;
	}

	/* SMUXEN 스트로브 — 하드웨어가 완료되면 자동으로 클리어됨 */
	err = as7341_reg_write(dev, AS7341_REG_ENABLE, AS7341_ENABLE_PON | AS7341_ENABLE_SMUXEN);
	if (err != 0) {
		return err;
	}

	for (int i = 0; i < 50; i++) {
		uint8_t enable_val;

		err = as7341_reg_read(dev, AS7341_REG_ENABLE, &enable_val);
		if (err != 0) {
			return err;
		}
		if ((enable_val & AS7341_ENABLE_SMUXEN) == 0) {
			return 0;
		}
		k_sleep(K_MSEC(1));
	}

	return -ETIMEDOUT;
}

module_err_t m_i2c_as7341_init(m_i2c_as7341_dev_t *dev, const struct device *i2c_bus)
{
	if (dev == NULL || i2c_bus == NULL) {
		return MODULE_ERR_INVALID_PARAM;
	}

	dev->i2c_dev = i2c_bus;
	dev->initialized = false;

	if (!device_is_ready(dev->i2c_dev)) {
		LOG_ERR("AS7341(%s) i2c bus not ready", dev->i2c_dev->name);
		return MODULE_ERR_NOT_INITIALIZED;
	}

	/* nrfutil/JTAG로 MCU만 소프트 리셋하면 VDD1.8V는 계속 인가된 채라 AS7341/버스
	 * 상태가 유지된다 — 직전 트랜잭션이 리셋으로 끊겼으면 슬레이브가 SDA를 물고
	 * 있을 수 있어 다음 START가 간헐적으로 NACK(-5/EIO)난다 (2026-09-15 실기에서
	 * NIR2가 4회 중 2회 실패, 배선 문제라면 100% 재현돼야 하므로 버스 stuck으로 판단).
	 * i2c_recover_bus()로 SCL을 클럭해 슬레이브를 풀어준 뒤 WHOAMI를 최대 3회 재시도한다.
	 */
	uint8_t id = 0;
	int err = -EIO;

	for (int attempt = 0; attempt < 3; attempt++) {
		if (attempt > 0) {
			(void)i2c_recover_bus(dev->i2c_dev);
		}

		err = as7341_reg_read(dev, AS7341_REG_ID, &id);
		if (err == 0) {
			break;
		}
	}

	if (err != 0) {
		LOG_ERR("AS7341(%s) WHOAMI read failed after retry+bus recovery (i2c err=%d) — "
			"I2C 통신 자체가 안 됨", dev->i2c_dev->name, err);
		return MODULE_ERR_I2C_TIMEOUT;
	}

	LOG_INF("AS7341(%s) WHOAMI = 0x%02X (expect upper 6 bits 0x%02X)", dev->i2c_dev->name, id,
		AS7341_ID_EXPECTED_VALUE);

	if ((id & AS7341_ID_EXPECTED_MASK) != AS7341_ID_EXPECTED_VALUE) {
		LOG_WRN("AS7341(%s) WHOAMI mismatch — I2C 통신은 되나 장치 ID가 예상과 다름",
			dev->i2c_dev->name);
	}

	/* Power on (PON) */
	err = as7341_reg_write(dev, AS7341_REG_ENABLE, AS7341_ENABLE_PON);
	if (err != 0) {
		return MODULE_ERR_I2C_TIMEOUT;
	}
	k_sleep(K_MSEC(1)); /* PON 이후 안정화 시간 (데이터시트 tPON) */

	err = as7341_configure_smux(dev);
	if (err != 0) {
		LOG_ERR("AS7341(%s) SMUX 구성 실패 (err=%d)", dev->i2c_dev->name, err);
		return MODULE_ERR_NOT_INITIALIZED;
	}

	LOG_INF("AS7341(%s) SMUX 구성 성공 — init 완료", dev->i2c_dev->name);

	/* SP_EN은 여기서 켜지 않는다 — m_i2c_as7341_read_raw()가 매 호출마다 새로 트리거한다
	 * (아래 read_raw() 주석 참고, 2026-09-22 정정). init에서는 PON만 걸어둔다. */
	dev->initialized = true;
	return MODULE_ERR_OK;
}

module_err_t m_i2c_as7341_set_gain(m_i2c_as7341_dev_t *dev, uint16_t gain)
{
	/* CFG1.AGAIN[4:0] — gain 0=0.5x ... 10=512x 등 (데이터시트 표 참고).
	 * architecture.md §4/§5: 초기 검증 단계에서는 고정값 사용, 채널별 saturation
	 * 임계 게인은 실측 후 확정 (TODO open-item, §11).
	 */
	int err = as7341_reg_write(dev, AS7341_REG_CFG1, (uint8_t)(gain & 0x1F));

	return (err == 0) ? MODULE_ERR_OK : MODULE_ERR_I2C_TIMEOUT;
}

module_err_t m_i2c_as7341_set_integration_time(m_i2c_as7341_dev_t *dev,
						 uint8_t integration_20ms_units)
{
	/* integration time = (ATIME+1) * (ASTEP+1) * 2.78us.
	 * _ref_fnirs_example(src/as7341.c as7341_set_integration_20ms_device())와 동일하게
	 * ATIME=AS7341_ATIME_FIXED(99)로 고정하고 ASTEP만 목표 적분시간(20ms 단위, BLE
	 * AS7341_CONFIG 필드와 동일 단위)에 맞춰 계산한다 — 이전에는 반대로 ASTEP를
	 * 고정하고 ATIME을 가변으로 뒀었는데(등가이나 레퍼런스와 파라미터화 방향이 달랐음),
	 * "레퍼런스와 동일 세팅" 목표로 2026-09-21 정정. 단위 0은 레퍼런스처럼 최소 1로
	 * 클램프한다.
	 */
	if (integration_20ms_units == 0) {
		integration_20ms_units = 1;
	}

	uint32_t astep_plus_one = ((uint32_t)integration_20ms_units * 20000U) / 278U;

	if (astep_plus_one == 0) {
		astep_plus_one = 1;
	}

	uint32_t astep = astep_plus_one - 1;

	if (astep > 0xFFFF) {
		astep = 0xFFFF;
	}

	int err = as7341_reg_write(dev, AS7341_REG_ATIME, AS7341_ATIME_FIXED);

	if (err != 0) {
		return MODULE_ERR_I2C_TIMEOUT;
	}

	err = as7341_reg_write(dev, AS7341_REG_ASTEP_L, (uint8_t)(astep & 0xFF));
	if (err == 0) {
		err = as7341_reg_write(dev, AS7341_REG_ASTEP_H, (uint8_t)((astep >> 8) & 0xFF));
	}

	return (err == 0) ? MODULE_ERR_OK : MODULE_ERR_I2C_TIMEOUT;
}

module_err_t m_i2c_as7341_trigger_measurement(m_i2c_as7341_dev_t *dev)
{
	if (dev == NULL) {
		return MODULE_ERR_INVALID_PARAM;
	}

	if (!dev->initialized) {
		return MODULE_ERR_NOT_INITIALIZED;
	}

	/* [정정, 2026-09-22] v0.1.12에서 "SP_EN은 init 1회만, free-running(SPM)"으로 바꿨었으나
	 * (당시 근거: _ref_fnirs_example도 이 방식) — 그 레퍼런스는 LED가 상시 켜져 있는
	 * 구조라 free-running이어도 LED와 적분 구간이 항상 겹친다. 우리 프로젝트는 이 함수를
	 * 호출하는 m_i2c.c acquire_one_sample()이 매 샘플마다 LED를 켰다/끄는 구조(architecture.md
	 * §5 "연속점등 금지")라, free-running 적분 주기와 LED on/off 주기가 서로 비동기로 돌면서
	 * beat(맥놀이) 패턴으로 어긋나는 게 실기로 확인됨(architecture.md §11 항목5, 완전 암실
	 * 테스트에서 ~53샘플 주기로만 큰 값이 찍히는 패턴). 그래서 매 샘플마다 SP_EN을
	 * 0->1로 재기록해 새 적분 사이클을 명시적으로 시작한다(datasheet §10.2, SP_EN 재기록
	 * 시 스펙트럴 엔진 재시작) — 호출부가 LED를 켠 "다음"에 이 함수를 호출하므로, 이렇게
	 * 하면 이번에 읽는 적분 구간이 항상 LED on 구간 이후에 시작됨이 보장된다.
	 *
	 * [정정, 2026-09-22 v0.1.23] 트리거와 "적분 대기+읽기"를 분리했다 — NIR1/NIR2를
	 * 순차로 트리거+대기+읽기 하면 두 센서의 적분 대기시간(각각 최대 AS7341_MEASURE_
	 * TIMEOUT_MS)이 더해져서, 실측 결과 의도한 Cycle 300ms 대신 400ms로 밀리는 현상이
	 * 발생함(호출부가 두 센서 모두 트리거를 먼저 걸고 나서 각각 대기+읽기를 하도록
	 * 바꿔, 두 센서의 적분이 동시에 진행되게 함 — m_i2c.c acquire_one_sample() 참고). */
	int err = as7341_reg_write(dev, AS7341_REG_ENABLE, AS7341_ENABLE_PON);
	if (err != 0) {
		return MODULE_ERR_I2C_TIMEOUT;
	}
	err = as7341_reg_write(dev, AS7341_REG_ENABLE, AS7341_ENABLE_PON | AS7341_ENABLE_SP_EN);
	if (err != 0) {
		return MODULE_ERR_I2C_TIMEOUT;
	}

	return MODULE_ERR_OK;
}

module_err_t m_i2c_as7341_read_raw(m_i2c_as7341_dev_t *dev, uint16_t raw_out[NIRS_WAVELENGTH_COUNT],
				    bool *out_saturated)
{
	if (dev == NULL || raw_out == NULL) {
		return MODULE_ERR_INVALID_PARAM;
	}

	if (!dev->initialized) {
		return MODULE_ERR_NOT_INITIALIZED;
	}

	if (out_saturated != NULL) {
		*out_saturated = false;
	}

	/* 호출부가 m_i2c_as7341_trigger_measurement()를 이미 호출해뒀다고 가정 — 여기서는
	 * AVALID 폴링(적분 완료 대기) + 채널 읽기만 수행한다(위 trigger_measurement() 주석
	 * 참고). */
	int err;
	bool valid = false;

	for (int i = 0; i < AS7341_MEASURE_TIMEOUT_MS; i++) {
		uint8_t status2 = 0;

		err = as7341_reg_read(dev, AS7341_REG_STATUS2, &status2);
		if (err != 0) {
			return MODULE_ERR_I2C_TIMEOUT;
		}
		if (status2 & AS7341_STATUS2_AVALID) {
			valid = true;
			/* AVALID가 선 바로 그 순간의 STATUS2 바이트에서 함께 확인 —
			 * 이 값이 latch되는 채널 데이터(아래 CH0_DATA 읽기)와 동일 시점 기준
			 * (datasheet §10.2.7 ASTATUS 설명과 동일 원리). 별도 I2C 트랜잭션 불필요. */
			if (out_saturated != NULL &&
			    (status2 & (AS7341_STATUS2_ASAT_ANALOG | AS7341_STATUS2_ASAT_DIGITAL))) {
				*out_saturated = true;
			}
			break;
		}
		k_sleep(K_MSEC(1));
	}

	if (!valid) {
		return MODULE_ERR_I2C_TIMEOUT;
	}

	uint8_t ch_data[AS7341_SMUX_CHANNEL_COUNT * 2];

	err = i2c_burst_read(dev->i2c_dev, AS7341_I2C_ADDR, AS7341_REG_CH0_DATA_L,
			      ch_data, sizeof(ch_data));
	if (err != 0) {
		return MODULE_ERR_I2C_TIMEOUT;
	}

	uint16_t channels[AS7341_SMUX_CHANNEL_COUNT];

	for (int i = 0; i < AS7341_SMUX_CHANNEL_COUNT; i++) {
		channels[i] = (uint16_t)ch_data[i * 2] | ((uint16_t)ch_data[i * 2 + 1] << 8);
	}

	/* datasheet DS000504 Figure 7/22 기준: 640nm에 가장 가까운 필터는 F7(630nm),
	 * 680nm은 F8(680nm, 정확히 일치), 950nm(스펙)/980nm(실장)은 NIR(910nm)가 가장 가깝다
	 * (m_i2c_as7341.h 상단 채널 매핑 주석 참고).
	 */
	raw_out[NIRS_WAVELENGTH_640NM] = channels[AS7341_CH_F7];
	raw_out[NIRS_WAVELENGTH_680NM] = channels[AS7341_CH_F8];
	raw_out[NIRS_WAVELENGTH_950NM] = channels[AS7341_CH_NIR];

	LOG_DBG("AS7341(%s) raw F5,F6,F7,F8,Clear,NIR = %u %u %u %u %u %u", dev->i2c_dev->name,
		channels[0], channels[1], channels[2], channels[3], channels[4], channels[5]);

	return MODULE_ERR_OK;
}
