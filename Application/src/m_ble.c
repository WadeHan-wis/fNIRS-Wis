#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <errno.h>
#include "m_ble.h"
#include "m_ble_gatt.h"
#include "m_ble_proto.h"
#include "m_i2c.h"
#include "m_i2c_ring_buffer.h"
#include "m_ctrl.h"
#include "config_app.h"

LOG_MODULE_REGISTER(m_ble, LOG_LEVEL_INF);

/* BLE 지연/재연결 대기 중에도 ring buffer를 계속 소비할 수 있도록 짧은 주기로 polling한다.
 * TODO(open-item): Rev2에서 실제 GATT notify 완료 콜백/큐 기반으로 event-driven 전환.
 */
#define BLE_POLL_INTERVAL_MS 20

/* notify 대상 연결 — on_connected/on_disconnected에서만 갱신(BLE Task 컨텍스트 단일 소비). */
static struct bt_conn *s_conn;

/* §6(codingstandard.md): notify 실패를 silent하게 버리지 않기 위한 카운터.
 * 연결 안 됨/notify 미구독 상태의 실패는 정상 상태이므로 별도(s_notify_skip_count)로 센다. */
static uint32_t s_notify_drop_count;
static uint32_t s_notify_skip_count;

/* 테스트 APK(fnirs-visualizer-app-release.apk) 호환용 advertising — AS7341_SERVICE_UUID로
 * 광고해서 APK가 스캔 필터로 찾을 수 있게 한다 (m_ble_gatt.h 상단 주석 참고).
 */
static const struct bt_data s_adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_AS7341_SERVICE_VAL),
};

static const struct bt_data s_scan_rsp_data[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* bt_enable() 직후(ble_init())와 연결 해제 후(on_disconnected()) 양쪽에서 호출된다.
 * Zephyr peripheral은 연결되면 advertising이 자동 중단되고 연결 해제 후 자동으로
 * 재시작되지 않으므로, 앱이 다시 스캔/연결할 수 있게 매번 명시적으로 시작해야 한다.
 */
static int start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, s_adv_data, ARRAY_SIZE(s_adv_data),
				   s_scan_rsp_data, ARRAY_SIZE(s_scan_rsp_data));

	if (err != 0) {
		LOG_ERR("bt_le_adv_start failed (err=%d)", err);
		return err;
	}

	LOG_INF("BLE advertising started (AS7341_SERVICE_UUID, device name %s)",
		CONFIG_BT_DEVICE_NAME);
	return 0;
}

/* disconnected 콜백 안에서 bt_le_adv_start()를 바로 호출하면, SoftDevice Controller가
 * 연결 해제 HCI 이벤트 처리를 아직 마무리하는 도중이라 광고 시작이 조용히(-EAGAIN 등)
 * 실패할 수 있음을 실기에서 확인(2026-09-16) — LED는 정상적으로 처음 상태로 복귀하는데
 * advertising만 안 돌아오는 증상으로 나타남. 시스템 워크큐로 한 틱 미뤄서 재시작한다.
 */
static void adv_restart_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int err = start_advertising();

	if (err != 0) {
		m_ctrl_report_error(MODULE_ERR_NOT_INITIALIZED);
	}
}

K_WORK_DEFINE(s_adv_restart_work, adv_restart_work_handler);

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		LOG_ERR("BLE connection failed (err=0x%02X)", err);
		return;
	}

	LOG_INF("BLE connected");

	/* DATA0/DATA1 notify 대상 conn을 보관한다 — bt_conn_ref()로 참조를 잡아둬야
	 * 콜백 리턴 이후에도(다음 notify 호출 시점까지) 유효하다. */
	s_conn = bt_conn_ref(conn);

	/* m_i2c는 이 신호를 polling해서 "디바이스 On 순차점등 -> BLE 10회 점멸 -> 측정"
	 * 전환 시점을 판단한다 (m_ctrl.h 참고, Acquisition/BLE 직접 결합 금지).
	 */
	m_ctrl_notify_ble_connected();
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	LOG_INF("BLE disconnected (reason=0x%02X)", reason);

	if (s_conn != NULL) {
		bt_conn_unref(s_conn);
		s_conn = NULL;
	}

	/* m_i2c는 이 신호로 "측정 시퀀스 -> 처음 상태(디바이스 On 순차점등)"로 되돌아간다
	 * (m_ctrl.h 참고, 2026-09-16 R2-2 재연결 시나리오 구현). 연결이 끊긴 뒤에도 기기가
	 * 계속 센싱하던 문제 수정.
	 */
	m_ctrl_notify_ble_disconnected();

	/* Zephyr peripheral은 연결되면 advertising이 자동으로 중단되고, 연결 해제 후
	 * 자동으로 재시작되지 않는다 — 앱이 다시 스캔/연결할 수 있도록 재시작해야 한다.
	 * 이 콜백 컨텍스트에서 직접 호출하지 않고 워크큐로 미룬다(위 주석 참고). */
	k_work_submit(&s_adv_restart_work);
}

BT_CONN_CB_DEFINE(m_ble_conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

/* main()에서 호출하지 않는다 — m_ble_task_entry() 진입 직후 태스크 컨텍스트에서
 * 1회 수행한다 (S2 task_i2c 관례: 드라이버/스택 init은 생성된 task 안에서 진행).
 */
static module_err_t ble_init(void)
{
	m_ble_gatt_init();
	m_ble_proto_init();

	/* AS7341 I2C 초기화가 끝날 때까지 대기 — bt_enable()이 I2C1 SMUX 폴링 도중
	 * 끼어들면 트랜잭션이 멈추는 경합이 확인됨(m_i2c.h 상단 주석 참고, 2026-09-15). */
	k_sem_take(&sem_i2c_init_done, K_FOREVER);

#if TEMP_BLE_DISABLE_TEST
	LOG_WRN("TEMP_BLE_DISABLE_TEST=1 — bt_enable() 건너뜀 (AS7341/RTC 격리 진단용)");
	return MODULE_ERR_OK;
#endif

	int err = bt_enable(NULL);

	if (err != 0) {
		LOG_ERR("bt_enable failed (err=%d)", err);
		return MODULE_ERR_NOT_INITIALIZED;
	}

	if (start_advertising() != 0) {
		return MODULE_ERR_NOT_INITIALIZED;
	}

	/* TODO(open-item): Tx power -8dBm 고정, MTU 협상(251byte 목표) — S2 정책을
	 * 우리 프로덕션 프로토콜(향후 R2-1~R2-4)에 적용할 때 채택. 지금은 테스트 APK
	 * 호환 연결 자체가 목적이라 기본값 사용.
	 */
	return MODULE_ERR_OK;
}

/* NIR1(DATA0)/NIR2(DATA1)를 각각 8바이트 프레임으로 인코드해서 notify한다.
 * 테스트 APK 프로토콜은 배치가 아니라 샘플 1개당 notify 1회를 기대하므로
 * (m_ble_proto.h 프로토콜 스펙 참고) S2식 batching 없이 즉시 전송한다.
 *
 * led_index: cycle period/active window에 따른 순차 LED 전환 로직이 아직 없고
 * (m_ble_proto.h TODO(open-item) 참고) acquire_one_sample()이 매 샘플마다 3파장
 * LED를 동시에 구동하므로, 현재는 "특정 LED 하나만 켜진 상태"라는 개념이 없다 —
 * 고정값 0 사용.
 */
#define BLE_SAMPLE_LED_INDEX_FIXED 0

static void notify_sample(nir_sensor_id_t sensor_id, const nirs_sample_t *sample,
			   int (*notify_fn)(struct bt_conn *, const uint8_t *, uint16_t))
{
	uint8_t frame[BLE_PROTO_DATA_FRAME_LEN];

	m_ble_proto_encode_sample(sensor_id, sample, BLE_SAMPLE_LED_INDEX_FIXED, frame);

	int err = notify_fn(s_conn, frame, sizeof(frame));

	if (err == 0) {
		return;
	}

	if (err == -ENOTCONN || err == -EINVAL) {
		/* 정상 상태(진짜 실패 아님) — §6에 따라 silent하게 버리지 않고 카운터만
		 * 증가시킨다. -ENOTCONN=연결 전/해제 후. -EINVAL=앱이 아직 notify를
		 * 구독(CCC)하지 않은 상태 — Zephyr bt_gatt_notify()가 실제로 이 에러코드를
		 * 쓴다(gatt.c gatt_notify(): !bt_gatt_is_subscribed() 시 -EINVAL 반환,
		 * "통신 실패"가 아니다). 2026-09-17 실기 검증 중 발견: 이전에는 -EINVAL을
		 * 진짜 실패로 오분류해서 MODULE_ERR_BLE_TX_FAILED를 계속 보고했고, 이게
		 * m_ctrl_is_safe_state() 도입 이후로는 앱이 연결만 하고 아직 구독 전인
		 * 정상적인 과도 상태에서 측정을 영구 중단시키는 회귀로 이어질 뻔했다. */
		s_notify_skip_count++;
		return;
	}

	s_notify_drop_count++;
	LOG_WRN("BLE notify 실패(sensor=%d, err=%d), drop_count=%u", sensor_id, err,
		s_notify_drop_count);
	m_ctrl_report_error(MODULE_ERR_BLE_TX_FAILED);
}

/* Safety 인증 대응(2026-09-17, architecture.md §11 항목6-[4]): gap 식별용 SEQ notify.
 * DATA0/DATA1과 같은 tick에서 함께 보낸다 — 앱이 최근 SEQ/DATA notify를 짝지어
 * seq_num 불연속(gap)을 감지할 수 있게 한다. 에러 처리는 notify_sample()과 동일
 * 원칙(§6, -ENOTCONN/-EINVAL은 정상 상태로 skip 카운트).
 */
static void notify_seq(uint32_t seq_num)
{
	uint8_t frame[BLE_PROTO_SEQ_FRAME_LEN];

	m_ble_proto_encode_seq(seq_num, frame);

	int err = m_ble_gatt_notify_seq(s_conn, frame, sizeof(frame));

	if (err == 0 || err == -ENOTCONN || err == -EINVAL) {
		if (err != 0) {
			s_notify_skip_count++;
		}
		return;
	}

	s_notify_drop_count++;
	LOG_WRN("BLE SEQ notify 실패(err=%d), drop_count=%u", err, s_notify_drop_count);
	m_ctrl_report_error(MODULE_ERR_BLE_TX_FAILED);
}

K_SEM_DEFINE(sem_ble_init_done, 0, 1);

void m_ble_task_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	module_err_t init_err = ble_init();

	/* 성공/실패 무관하게 항상 1회 give — m_i2c.c가 RTC 시작을 이 신호로 대기한다
	 * (m_ble.h 상단 주석 참고, 2026-09-15 bt_enable()/RTC2 경합 수정). */
	k_sem_give(&sem_ble_init_done);

	if (init_err != MODULE_ERR_OK) {
		m_ctrl_report_error(init_err);
		k_sleep(K_FOREVER); /* BLE stack 없이는 이 태스크가 할 일이 없다 */
	}

	nirs_sample_t sample;

	while (1) {
		/* Safety 인증 대응(2026-09-17, architecture.md §11 항목6-[3]): 연결이 끊긴
		 * 동안에는 pop하지 않는다. 예전에는 끊긴 상태에서도 계속 pop해서 notify_fn이
		 * -ENOTCONN으로 버리고 있었다(사실상 즉시 폐기) — ring buffer는 이미 overflow 시
		 * 가장 오래된 샘플을 덮어쓰므로(m_i2c_ring_buffer.c), pop을 멈추기만 해도
		 * "짧은 끊김 동안 로컬 버퍼링 지속"이 자연히 동작한다. m_i2c.c는 끊긴 동안에도
		 * 계속 acquire+push한다(reset_to_device_on() 즉시 호출 안 함, m_i2c.c 참고).
		 */
		if (m_ctrl_is_ble_connected() && m_i2c_ring_buffer_pop(&sample)) {
			notify_sample(NIR_SENSOR_1, &sample, m_ble_gatt_notify_data0);
			notify_sample(NIR_SENSOR_2, &sample, m_ble_gatt_notify_data1);
			notify_seq(sample.seq_num);
		} else {
			k_sleep(K_MSEC(BLE_POLL_INTERVAL_MS));
		}

		/* Watchdog(Rev3, R3-1) 생존 신호 (m_ctrl.h 참고). */
		m_ctrl_notify_alive(CTRL_ALIVE_BLE);
	}
}

/* main()에서 생성하지 않는다 — 커널 시작 시 자동 등록/스케줄링된다. */
K_THREAD_DEFINE(m_ble_tid, BLE_TASK_STACK_SIZE, m_ble_task_entry, NULL, NULL, NULL,
		 BLE_TASK_PRIORITY, 0, 0);
