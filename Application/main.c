/*
 * Rev0 | Zephyr(NCS) 스캐폴드 진입점
 *
 * I2C(Acquisition)/BLE/Ctrl 태스크는 main()에서 생성하지도, 초기화하지도 않는다.
 * 각 태스크는 자신의 모듈 파일(m_i2c.c/m_ble.c/m_ctrl.c)에서 K_THREAD_DEFINE으로
 * 정적 선언되어 커널 시작 시 자동으로 스케줄링되고, 드라이버/스택 init도 각 태스크
 * 진입 직후 자기 자신이 수행한다 (S2 task_i2c 관례 계승 — main은 드라이버를 모른다).
 *
 * main()은 부팅 배너 출력 후 영구 대기한다.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "config_app.h"

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("==============================");
	LOG_INF("  fNIRS FW (Rev0 scaffold)");
	LOG_INF("  Board FW Version: v%d.%d.%d", FW_VERSION_MAJOR, FW_VERSION_MINOR,
		FW_VERSION_PATCH);
	LOG_INF("  Zephyr RTOS scheduler started.");
	LOG_INF("==============================");

	k_sleep(K_FOREVER);

	return 0;
}
