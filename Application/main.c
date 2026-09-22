/*
 * Rev0 | Zephyr(NCS) 스캐폴드 진입점
 *
 * I2C(Acquisition)/BLE/Ctrl 태스크는 main()에서 생성하지도, 초기화하지도 않는다.
 * 각 태스크는 자신의 모듈 파일(m_i2c.c/m_ble.c/m_ctrl.c)에서 K_THREAD_DEFINE으로
 * 정적 선언되어 커널 시작 시 자동으로 스케줄링되고, 드라이버/스택 init도 각 태스크
 * 진입 직후 자기 자신이 수행한다 (S2 task_i2c 관례 계승 — main은 드라이버를 모른다).
 *
 * main()은 부팅 배너 출력 후 영구 대기한다.
 *
 * [예외, 2026-09-22, 하드웨어 진단용] 아래 LED1/LED2 강제 점등만은 이 원칙의 의도적
 * 예외다 — SW1을 눌러도 한 번에 켜지지 않는 보드가 발견되어, "전원 자체가 안 들어온
 * 것"과 "전원은 들어왔는데 태스크 초기화(I2C/BLE 드라이버 등) 단계에서 멈춘 것"을
 * 구분해야 한다는 요구로 추가됐다. m_i2c 태스크의 정상 LED 시퀀스(디바이스 On 순차
 * 점등 등)는 그 태스크 자신의 init이 성공해야만 도달하므로, 태스크 init이 멈추면
 * 전원이 들어왔어도 아무 LED도 안 켜져 두 경우를 구분할 수 없었다. main()은 커널이
 * 스케줄링을 시작하면 어떤 태스크 init 결과와도 무관하게 반드시 여기까지 도달하므로,
 * 여기서 직접 LED를 켜면 "적어도 전원+부팅까지는 성공"을 무조건 확인할 수 있다.
 * m_i2c_led_*()는 m_i2c_led.h상 "I2C Task 컨텍스트에서만 호출"이 원칙이지만, 이
 * 시점은 아직 어떤 태스크도 AS7341 측정을 하지 않으므로(m_i2c 태스크 시작 전) 실측
 * 정확도와 충돌하지 않는다 — m_i2c 태스크가 정상 기동하면 곧이어 자신의 LED 시퀀스로
 * 자연스럽게 덮어쓴다.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "config_app.h"
#include "m_i2c_led.h"

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("==============================");
	LOG_INF("  fNIRS FW (Rev0 scaffold)");
	LOG_INF("  Board FW Version: v%d.%d.%d", FW_VERSION_MAJOR, FW_VERSION_MINOR,
		FW_VERSION_PATCH);
	LOG_INF("  Zephyr RTOS scheduler started.");
	LOG_INF("==============================");

	/* [하드웨어 진단용, 2026-09-22] 전원/부팅 확인용 강제 점등 — 위 주석 참고.
	 * m_i2c 태스크가 정상 기동하면 이후 LED 상태는 그쪽 시퀀스로 넘어간다. */
	if (m_i2c_led_init() == MODULE_ERR_OK) {
		m_i2c_led_set_duty(NIRS_WAVELENGTH_640NM, I2C_LED_INDICATOR_DUTY_PERMILLE);
		m_i2c_led_set_duty(NIRS_WAVELENGTH_680NM, I2C_LED_INDICATOR_DUTY_PERMILLE);
		LOG_INF("  [DIAG] LED1(640nm)/LED2(680nm) forced ON from main() — "
			"boot reached this point.");
	} else {
		LOG_ERR("  [DIAG] m_i2c_led_init() failed in main() — LED PWM device not ready.");
	}

	k_sleep(K_FOREVER);

	return 0;
}
