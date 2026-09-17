# fNIRS 펌웨어 변경 이력 (PoC v1 보드)

`Application/include/config_app.h`의 `FW_VERSION_MAJOR/MINOR/PATCH`와 1:1로 대응한다.
패치 적용마다 PATCH를 1씩 올린다 (v0.0.1 → v0.0.2 → ...).

## v0.0.1 (2026-09-14)

- Rev0 Zephyr(NCS) 스캐폴드: `m_i2c`/`m_ble`/`m_ctrl` 3개 태스크, RTC 100ms ISR(5-frame
  Bresenham 보정), CTRL 상태머신.
- PoC v1(nRF52832) 실기 부팅 크래시 2건 수정:
  - `NRFX_RTC_INSTANCE(2)` → `NRFX_RTC_INSTANCE(NRF_RTC2)` (포인터 캐스팅 버그, MemManage 폴트)
  - RTC2 IRQ(36) `IRQ_CONNECT` 누락 → 추가 (Spurious IRQ 폴트)
- NFC 핀(P0.09/P0.10) → GPIO 전환 (`app.overlay`, NIR1 I2C 사용을 위해 필요)
- 로깅 백엔드를 RTT로 전환 (PoC v1은 UART 핀 미노출)
- LED PWM 실제 구동 구현 (PWM0 채널 0/1/2, P0.04/05/06) — devicetree 기본 `nordic,invert`
  제거(우리 회로는 active-high)
- LED 상태 시나리오: 디바이스 On 시 LED 1개씩 1초 간격 순차 점등 → BLE 연동 성공 시
  10회 점멸 → 측정 시퀀스 전환
- **알려진 이슈**: D3(680nm)/D4(950nm 스펙, PoC v1 실장 980nm) LED가 점등되지 않음 —
  D2(640nm)만 정상 동작 확인. 소프트웨어(devicetree/PWM 채널 매핑)는 D2와 구조적으로
  동일하게 확인됨 — 하드웨어(Q5/Q6, R18/R20, 배선) 원인 가능성 조사 중.

## v0.0.2 (2026-09-15)

- D3/D4 LED 미점등 1차 진단: 채널 배열 스왑 + `led_test` 브랜치 완전 격리 테스트(태스크/RTC/I2C
  전부 배제, PWM만) + GPIO 스윕 테스트까지 전부 고장 재현 → **하드웨어 결함으로 오판**
  (아래 v0.0.3에서 정정됨).
- 핀맵 재확인: `IR_LED_CTRL1=P0.04, WH_LED_CTRL2=P0.05, IR_LED_CTRL2=P0.06` 매핑을
  스키매틱 재열람 + 레퍼런스 펌웨어(`WisMedical/examples`) 핀 정의로 3중 확인·확정.

## v0.0.3 (2026-09-15)

- **D3/D4 미점등 진짜 원인 확정 (하드웨어 결함 아니었음, v0.0.2 결론 정정)**: 빌드용 스탠드인
  보드 `nrf52dk/nrf52832`의 기본 devicetree가 UART0을 활성화하고 있었고, 그 기본 핀이
  `UART_TX=P0.06(D4), UART_RTS=P0.05(D3)`라 PWM0과 물리 핀을 경합하고 있었음.
  `CONFIG_LOG_BACKEND_UART=n`은 로깅 백엔드만 끌 뿐 UART0 페리페럴/pinctrl 자체는 계속
  활성 상태였던 게 원인. `app.overlay`에 `&uart0 { status = "disabled"; };` 추가로 해결
  (`pinmap.md` §8 참고). D2가 항상 정상이었던 이유(UART가 안 쓰는 핀)까지 정합적으로 설명됨.
- `led_test` 브랜치(격리 PWM 테스트, GPIO 스윕 테스트) 진단 종료, `main`으로 병합 없이 복귀.
- LED 인디케이터 duty 50%→20% (개발 단계 눈부심 방지).

## v0.0.4 (2026-09-15)

- AS7341(NIR1) I2C 드라이버 포팅 (`m_i2c_as7341.c`, `app.overlay`의 `&i2c1` 노드,
  SCL=P0.09/SDA=P0.10, I2C1/TWIM1 사용).
- 실기 검증: WHOAMI 레지스터(0x92) 읽기 성공, 상위 6비트가 예상값 `0x24`와 일치 —
  NIR1 I2C 통신 자체가 정상 동작함을 확인.
- **버그 수정**: SMUX RAM(20바이트) 구성 burst write가 레지스터주소(1)+데이터(20)=21바이트로,
  `i2c_nrfx_twim` 드라이버 기본 내부 버퍼(16바이트)를 초과해 `-ENOSPC(-28)`로 실패
  (실기 RTT 로그로 확인). `app.overlay`의 `&i2c1`에 `zephyr,concat-buf-size = <32>;` 추가로 해결,
  재검증 결과 "SMUX 구성 성공" 로그 확인.
- NIR1 `m_i2c_as7341_init()` 전체 초기화 시퀀스(WHOAMI 확인 → PON → SMUX 구성) 실기 완료.
- TODO(open-item, 미해결): SMUX RAM 20바이트 설정값과 채널→파장(640/680/950nm) 매핑은
  통상적 레퍼런스 값 기반 임시 설정 — ams AS7341 데이터시트로 재검증 필요 (파장별 분리
  정확도 검증 단계에서 확정). NIR2(SCL=P0.08, SDA=P0.07, I2C0)는 아직 미착수.

## v0.0.5 (2026-09-15)

- **AS7341 datasheet(DS000504 v3-00) 기준 SMUX 매핑 오류 발견 및 수정**: v0.0.4의 SMUX 구성이
  F1~F4(415/445/480/515nm)+Clear+NIR 그룹으로 되어 있었으나, 우리 프로젝트 파장
  640/680/950nm에 실제로 가장 가까운 필터는 F7(630nm)/F8(680nm)/NIR(910nm)이다
  (datasheet Figure 7/22). SMUX를 F5~F8+Clear+NIR 그룹으로 교체.
  TODO(open-item): 정확한 SMUX RAM 바이트 값은 이 datasheet에 없음(ams AN000633 앱노트
  별도 필요) — 공개 레퍼런스 구현(Adafruit AS7341 드라이버) 값을 우선 적용, 추후
  공식 앱노트로 바이트 단위 교차검증 필요.
- 채널→파장 매핑 수정: raw[640nm]=CH2(F7), raw[680nm]=CH3(F8, 정확 일치), raw[950nm]=CH5(NIR).
- 드라이버 레지스터/비트 정의(AS7341_REG_*, ENABLE 비트, SMUX 채널 인덱스 등)를
  `m_i2c_as7341.c`에서 `m_i2c_as7341.h`로 이동 (설정값은 헤더에 정의).
- 실기 data read 검증: gain=0(0.5x)/ATIME=0(2.78ms) 고정값으로는 raw 값이 거의 0(노이즈
  수준)이라 광학 반응 확인이 안 됨을 확인 → gain=9(256x)/ATIME=29(약 83ms)로 변경 후
  6채널(F5/F6/F7/F8/Clear/NIR) 모두 안정적인 비영 값 확인 (RTT 로그: F5=4960 F6=6744
  F7=5015 F8=3280 Clear=19275 NIR=1593 — Clear가 가장 높고 NIR이 낮은 것은 실내 조명
  환경에서 물리적으로 타당). SMUX/게인 경로가 실제로 동작함을 실측 확인.
- TODO(open-item): 알려진 파장(640/680nm) LED를 센서에 직접 비추는 정량 검증은 아직
  미실시 — 채널 상대값의 정성적 타당성만 확인된 상태.

## v0.0.6 (2026-09-15)

- **NIR2 AS7341 포팅 착수** (`&i2c0`, SCL=P0.08/SDA=P0.07, pinmap.md §3). `m_i2c_as7341` 드라이버를
  인스턴스 기반(`m_i2c_as7341_dev_t`)으로 리팩터링해서 NIR1/NIR2가 동일 코드를 공유하도록 변경.
  `nirs_sample_t.raw`를 `[NIR_SENSOR_COUNT][NIRS_WAVELENGTH_COUNT]`로 확장(센서별 raw 분리 보존).
- 1차 실기 검증: NIR1은 정상, **NIR2는 간헐적으로 I2C 통신 실패**(`i2c err=-5`, EIO/NACK,
  4회 중 2회). devicetree/핀 설정은 컴파일된 `zephyr.dts`로 직접 확인해 정상임을 검증
  (UART0 비활성화로 P0.07/P0.08 경합 없음, i2c0_default psels가 SDA=P0.07/SCL=P0.08로
  정확히 반영됨) — 배선 문제라면 100% 재현돼야 하는데 확률적으로 실패해서 배선/납땜
  결함이 아니라고 최종 판단.
- **근본 원인 확정 및 수정**: `nrfutil device reset`(MCU 소프트 리셋)은 VDD1.8V를 유지한 채
  MCU만 재시작한다 — 직전 I2C 트랜잭션이 리셋으로 중간에 끊기면 AS7341이 SDA를 물고 있는
  상태로 남아 다음 START가 간헐적으로 NACK난다(전형적인 I2C 버스 stuck). `m_i2c_as7341_init()`의
  WHOAMI 읽기를 `i2c_recover_bus()`(SCL 클럭킹으로 슬레이브 해제) + 최대 3회 재시도로 감싸서
  해결 — 재검증 결과 NIR1/NIR2 모두 6회 연속 성공.

## v0.0.8 (2026-09-15)

- **RTC 100ms ISR 동작 실측 검증** (트래커 "Zephyr Task RTC Timer로 100ms ISR 구현" 항목).
  `m_i2c_rtc.c`(nrfx RTC2 + Bresenham 보정)는 이미 구현/버그수정 완료 상태였으나, 태스크
  측(`m_i2c_task_entry`)에서 세마포어로 깨어나는 시점의 RTC 타임스탬프를 임시 로그로
  확인(`TEMP_RTC_TICK_LOG_TEST`) — RTT 로그의 실제 벽시계 시간 간격이 매 tick마다
  99.98~100.03ms로 지터 0.03ms 이내임을 확인, `m_i2c_rtc_get_timestamp_us()` 누적값도
  정확히 100000us씩 증가함을 확인. 검증 후 임시 로그 비활성화(`TEMP_RTC_TICK_LOG_TEST=0`).

## v0.0.9 (2026-09-15)

- **[버그 수정] RTC 5-frame Bresenham 보정 비율 반전** (트래커 "5프레임 합=13381 tick=500ms
  산술 확인" 코드 리뷰 항목에서 발견). `next_cc_delta()`가 5프레임 중 1회만 HIGH(3277)+4회
  LOW(3276)를 쓰고 있었는데, 합계가 `1*3277+4*3276=16381` tick으로 목표(500ms=`32768*0.5`
  `=16384` tick)보다 3 tick(91.55µs) 부족했다. 100ms=3276.8 tick의 소수부가 0.8이므로 올바른
  비율은 **5프레임 중 4회 HIGH + 1회 LOW**(`4*3277+3276=16384`, 정확히 일치)여야 하는데 반대로
  구현돼 있었음 — 약 183ppm(시간당 약 0.66초) 만큼 `timestamp_us`가 실제 RTC2 크리스탈 기준
  경과시간보다 빠르게 누적되는 오차였다. `next_cc_delta()`의 HIGH/LOW 분기를 반전해서 수정.
  (참고: 이전 세션 코드 주석의 "13381 tick" 표기도 `16381`의 오기였음 — 그 숫자로도 500ms가
  안 나오므로 애초에 주석 자체가 산술적으로 틀려 있었다.)

## v0.0.10 (2026-09-15)

- **[버그 수정] Ring buffer overflow 반환값이 최초 1회 발생 후 영구 고착**. 코드베이스
  전수 리뷰 중 발견: `m_i2c_ring_buffer_push()`가 누적 카운터(`g_dropped_sample_count`,
  init 시에만 리셋)를 기준으로 `MODULE_ERR_RING_BUFFER_OVERFLOW` 반환 여부를 판정하고
  있어서, **최초 1회 overflow가 발생한 이후로는 실제로 overflow가 아닌 정상 push 호출도
  전부 영구히 OVERFLOW를 반환**하고 있었다 — `m_i2c.c`가 매 샘플마다 `m_ctrl_report_error()`를
  호출해 ctrl_msgq를 계속 흘려보내는 부작용. 로컬 `bool overflowed` 플래그로 "이번 호출에서
  실제 overflow가 있었는지"만 반환하도록 수정. (설계 문서/헤더 주석이 원래 의도한 동작과
  실제 구현이 불일치했던 케이스 — 코드 리뷰로 발견)
- 코드베이스 전수 검토(RTC/AS7341/LED/ring buffer/CTRL/BLE) 완료. 아래 2건은 버그는
  아니나 설계상 주의가 필요해 architecture.md §11에 open-item으로 기록 예정:
  1. AS7341 STATUS2.AVALID 폴링 타임아웃(최대 200ms) × NIR1+NIR2 순차 = 최악 400ms인데
     RTC tick 주기는 100ms이고 `sem_i2c_ready`가 binary(max count 1)라, I2C가 느려지거나
     멈추면 세마포어가 소비되지 못한 tick을 조용히 잃어버릴 수 있음 — 이 경우 로그/에러
     보고가 없음(현재는 `m_i2c_as7341_read_raw()`가 실제 I2C 에러를 반환할 때만 보고됨).
  2. AS7341 SP_EN을 매 tick마다 재기록하지만 datasheet SPM 모드는 free-running이라, LED
     on 구간과 실제 적분 구간이 정확히 일치한다는 보장이 없음 — 기존 TODO(LED 시퀀싱
     정책 미확정)와 같은 근본 원인이라 별도 항목으로 새로 만들지 않고 해당 TODO에 통합.
  3. (참고, 코드 아님) NIR2 추가로 `nirs_sample_t.raw`가 2배로 커져 BLE batch 크기
     추정치(architecture.md A3, "샘플당 약 16~20byte" 가정, `BLE_BATCH_MAX_SAMPLES=12`)가
     더 이상 맞지 않음 — Rev2(R2-1) 착수 전 재계산 필요.

## v0.0.11 (2026-09-15)

- **BLE 실 스택 착수** — 테스트 APK(`sw/fnirs-visualizer-app-release.apk`) 호환 연동이 목표.
  파일 트리는 S2(NCS_TedreamS2) 관례 계승: `m_ble.c`(태스크/광고/연결) / `m_ble_gatt.c`(GATT
  서비스·characteristic 선언) / `m_ble_proto.c`(패킷 인코드·디코드) 3분할 신규 추가.
  프로토콜 바이트 포맷 자체는 S2가 아니라 APK가 요구하는 고정 스펙을 따른다.
- APK `classes.dex` 문자열 분석(jadx/apktool 없이 grep -a로 진행)으로 확인한 값 반영:
  기기명 `nRF_fNIRS_Sys_v2`, 서비스 `AS7341_SERVICE_UUID`(00001523-1212-efde-1523-785feabcd123),
  characteristic `AS7341_CONFIG/DATA0/DATA1_UUID`(0x1525/1526/1527, sensor0/1=NIR1/NIR2 대응).
  TODO(open-item): 4개 UUID 값과 4개 이름의 정확한 매핑, CONFIG write의 바이트 레이아웃은
  아직 미검증 — `m_ble_proto_on_config_write()`가 raw bytes를 로그로 남기도록 구현해둠
  (실기+APK 연동 시 LED 슬라이더 조작하며 캡처해서 역설계 예정).
- Zephyr BLE peripheral 활성화(`CONFIG_BT`, `bt_enable()`, `AS7341_SERVICE_UUID` advertising),
  connected 콜백에서 `m_ctrl_notify_ble_connected()` 연동.
- **[버그 수정] I2C/BLE 초기화 순서 경합**: `bt_enable()`이 AS7341(NIR1) SMUX 폴링 도중
  (`k_sleep(1ms)` 구간)에 끼어들면 I2C1 트랜잭션이 멈추는 문제를 실기에서 확인(재현율 100%,
  RTT 로그가 WHOAMI 이후 진행 없이 멈춤). I2C Task가 AS7341 초기화(성공/실패 무관) 완료 후
  `sem_i2c_init_done`을 주고, BLE Task는 `bt_enable()` 전에 이 신호를 기다리도록 수정 — 이후
  NIR1/NIR2 모두 3/3 재현 성공. 추가로 RTC 시작 시점도 BLE `bt_enable()` 완료 후로 미뤄서
  (`sem_ble_init_done`) RTC2의 100ms 인터럽트가 BLE 스택 초기화 도중 계속 발생하지 않게 했다.
- **[진단, 버그 아님으로 결론]** 수정 후에도 RTT 로그가 "AS7341 초기화 완료" 직후 특정
  지점에서 계속 멈추는 현상을 추가 조사 — J-Link로 CPU를 직접 halt해서 fault handler
  미도달·CycleCnt 정상 진행(=idle 상태)을 확인했고, 결정적으로 **Zephyr 표준 `samples/bluetooth/beacon`
  예제를 동일 보드·툴체인으로 빌드해도 동일한 지점 근처에서 RTT 캡처가 끊기는 것**을
  확인 — 우리 코드 결함이 아니라 `bt_enable()`이 초기 로그를 대량 출력하는 구간에서
  이 환경의 JLinkRTTLogger가 못 따라가는 캡처 툴 한계로 결론. **실기 BLE advertising 최종
  성공 여부는 RTT로 확인 불가 — 폰(nRF Connect 등)으로 `nRF_fNIRS_Sys_v2` 스캔 확인이
  필요(미검증 상태로 남김)**.

## v0.0.12 (2026-09-16)

- **DATA0/DATA1 실 notify 구현** — v0.0.11까지는 ring buffer를 소비만 하고 버리는
  S2식 batch placeholder였음(`send_batch()`가 no-op). `m_ble.c`에서 샘플을 pop할
  때마다 `m_ble_proto_encode_sample()` + `m_ble_gatt_notify_data0/1()`로 즉시
  NIR1→DATA0/NIR2→DATA1 notify하도록 교체. 테스트 APK 프로토콜은 배치가 아니라
  샘플 1개당 notify 1회를 기대하므로 S2 batching은 이번 마일스톤에는 적용하지 않음
  (`m_ble_batch.c/h`는 삭제하지 않고 Rev2 프로덕션 프로토콜용으로 유지, 현재는 미사용).
  notify 실패는 §6(silent 버림 금지)에 따라 `s_notify_drop_count`(실전송 실패)/
  `s_notify_skip_count`(미연결·미구독 정상 상태)로 구분 카운트.
- `TEMP_AS7341_RAW_DBG_LOG` 플래그 추가(config_app.h) — `m_i2c_as7341.c`의 기존
  `LOG_DBG(F5~NIR raw)`를 앱 연동 중에도 RTT로 확인할 수 있게 로그 레벨 상향.
  **검증 완료**: RTT 로그 값과 앱(Logcat `fNIRS_RAW`) 수신 hex/decoded 값이 정확히
  일치함을 실기에서 확인 — 인코딩/프로토콜 정합성 검증 완료.
- **[버그 수정] BLE 연결 해제 후에도 계속 센싱하던 문제** — `m_ctrl_notify_ble_disconnected()`
  신규 추가(`m_ctrl.h/c`), `m_i2c.c`에 `reset_to_device_on()` 추가해 BLE_BLINK/ACQUISITION
  상태에서 매 tick `m_ctrl_is_ble_connected()`를 확인, 연결 끊기면 즉시 측정을 멈추고
  LED 순차점등(디바이스 On) 상태로 복귀 + `seq_num` 리셋. 실기 검증 완료(LED 정상 복귀).
- **[버그 수정] 연결 해제 후 advertising이 재개되지 않던 문제** — Zephyr peripheral은
  연결되면 advertising이 자동 중단되고 해제 후 자동 재시작되지 않음. 최초 수정(disconnected
  콜백에서 `bt_le_adv_start()` 직접 호출)은 LED는 복귀하지만 광고는 안 살아나는 증상이
  실기에서 재현됨 — SoftDevice Controller가 disconnect HCI 이벤트를 마무리하는 도중이라
  광고 시작 명령이 조용히 실패하는 것으로 추정. 시스템 워크큐(`k_work_submit`)로 재시작을
  한 틱 미루도록 수정 후 **실기 재검증 완료 — 연결 해제 즉시 앱에서 재스캔 가능**.
- **테스트 Android 앱(`fNIRS_Android_Kotlin`, 별도 저장소) 동시 수정**:
  - DATA0/DATA1 수신 raw hex+decoded 값을 Logcat(`fNIRS_RAW`)과 화면 하단 "Raw Notify
    Log" 탭(1526/1527 전환)으로 확인 가능하도록 추가.
  - **[버그 수정] 재연결 시 AS7341 Controls 값이 기기 기본값으로 보이던 문제**: 매 연결마다
    CONFIG characteristic을 무조건 읽어서 UI를 덮어쓰던 로직을, 앱 최초 연결 시에만 읽고
    이후 재연결부터는 앱이 마지막으로 알던 설정을 기기에 다시 쓰도록 변경.
  - **[버그 수정] Start CSV가 실제로는 저장 안 되던 문제**: 공용 Documents 폴더에 File
    API로 직접 쓰던 방식이 Android 10+ scoped storage에서 조용히 실패 — 최초엔 앱 전용
    외부 저장소(`getExternalFilesDir`)로 우회했다가, 사용자 요청으로 **MediaStore API
    기반 `Documents/fnirs_rawdata/` 공용 경로**로 재변경(권한 요청 없이 공용 폴더 쓰기
    가능, API29 미만은 기존 File API로 폴백). 파일명 `{yyyy-MM-dd_HH-mm-ss}_fNIRS_RAW_DATA.csv`.
    adb로 실기 저장 파일 4개(정상 헤더+데이터) 확인해 정상 동작 재검증 완료.
- **실기 검증 완료**: 앱 연동 BLE advertising/연결/재연결, CONFIG write→LED PWM 제어,
  DATA0/DATA1 raw data 품질(RTT 대조 일치) — v0.0.11에서 미검증으로 남겼던 항목들 전부 해소.
  파장별 정량 캘리브레이션은 레퍼런스 데이터 미확보로 계속 보류(architecture.md A12).
- (참고, 코드 아님) 로컬 개발 환경(NCS 툴체인 Python)에서 Windows 레지스트리의 별도
  Python 3.12 설치 `PythonPath` 키가 cmake reconfigure 시 `ctypes`를 깨뜨리는 문제를
  발견 — 해당 레지스트리 값 제거로 해결(다른 Python 설치의 정상 실행에는 영향 없음,
  이 저장소 코드와 무관한 로컬 머신 설정 이슈).

## v0.0.13 (2026-09-16)

- **[하드웨어 갭 기록] 온도센서/배터리 IC 부재** — PoC v1 스키마틱(`docs/SCH_fNIRS_Sleep_Project.pdf`)
  검토 결과, Thermal 보호(§7)와 배터리 잔량 모니터링(§8)에 필요한 하드웨어가 아예 없음을 확인:
  온도센서(NTC/디지털) 미실장, fuel gauge/monitor IC 미실장, 게다가 nRF52832의 ADC 가능 핀
  (P0.02~P0.05/AIN0~AIN3)이 전부 LED 제어 신호로 이미 점유돼 있어 VBAT 저항분배를 붙일 여유
  ADC 핀도 없음. 소프트웨어로 우회 불가능한 순수 하드웨어 제약이라 architecture.md §7/§8/§11에
  경고 문구와 "다음 보드 리비전 반영 예정" 항목으로 기록. Rev3(Reliability) 범위에서 Thermal/
  Battery 항목은 제외하고 Watchdog만 우선 진행하기로 결정.
- **Watchdog 구현 (R3-1)** — nRF52832 내장 WDT(`wdt0`, 보드 dts 기본 `status="okay"`라
  devicetree overlay 변경 없이 `CONFIG_WATCHDOG=y`만으로 사용 가능)를 `m_ctrl.c`가 소유.
  `m_i2c`/`m_ble` Task가 각자 메인 루프 끝에서 `m_ctrl_notify_alive(CTRL_ALIVE_I2C/BLE)`로
  생존 신호를 보내고, `m_ctrl` Task는 `K_MSEC(WATCHDOG_CHECK_PERIOD_MS=500)` 주기로 깨어나
  두 소스 모두 `WATCHDOG_ALIVE_STALE_MS(1000ms)` 이내에 응답했을 때만 `wdt_feed()`를 호출한다
  — 한쪽이라도(예: I2C 버스 hang) 멈추면 feed가 끊겨 `WATCHDOG_TIMEOUT_MS(4000ms)` 후 SoC
  전체가 자동 리셋된다(`WDT_FLAG_RESET_SOC`). 타임아웃 값은 AS7341 STATUS2 폴링 최악 케이스
  (NIR1+NIR2 순차 최대 400ms, v0.0.7 CHANGELOG 참고)에 여유를 둔 값 — 실측 후 조정 가능.
  CTRL 태스크의 에러 메시지 큐 처리(`k_msgq_get`)도 기존 `K_FOREVER`에서 같은 주기의
  `K_MSEC` 타임아웃으로 바꿔 watchdog feed 체크와 한 루프에서 같이 돈다.
- **빌드 검증 완료** — FLASH 62.66%/RAM 59.66%. **하드웨어 검증 미실시** — 실기에서 I2C
  버스를 의도적으로 막아 watchdog 리셋이 실제로 발생하는지(fault injection) 확인 필요.

## v0.0.14 (2026-09-17)

- **Safety 인증(IEC 60601-1/-1-8) 대응 필수 펌웨어 안전기능 6건 구현** (architecture.md
  §11 항목6, HW 제약 없는 소프트웨어 항목만 — 배터리/온도 관련 4건은 HW 미실장으로 계속 보류).
  1. **[안전상태 전이]** `m_ctrl_is_safe_state()` 신규(`m_ctrl.h/c`) — `CTRL_STATE_DEGRADED`/
     `FAULT`일 때 true. `m_i2c.c` tick 루프 맨 앞에서 체크해서 true면 LED 소등 + 측정/
     ring buffer push를 완전히 건너뛴다. **설계상 래치**(자동복구 없음, 재부팅으로만 해제) —
     IEC 60601 단일고장 안전 철학에 따른 의도적 선택.
  2. **[Watchdog 안전 재시작]** `m_ctrl.c`에 `log_reset_cause()` 추가, `CONFIG_HWINFO=y`
     (`prj.conf`)로 RESETREAS 레지스터를 읽어 직전 리셋이 watchdog이었는지 로그로 남기고
     `hwinfo_clear_reset_cause()`로 정리. 이 프로젝트는 원래 리셋 원인과 무관하게 항상
     안전한 기본 상태(`I2C_LED_MODE_DEVICE_ON`)로 부팅하므로 별도 상태 복원 로직은
     불필요 — 이번 변경은 "왜 리셋됐는지"를 사후 분석 가능하게 로그로 남기는 것.
  3. **[BLE 끊김 로컬 버퍼링 + 저전력 대기]** **버그 수정**: `m_ble.c`가 연결 끊김 중에도
     ring buffer를 계속 pop해서 `-ENOTCONN`으로 즉시 버리고 있었음(사실상 버퍼링이 전혀
     안 되던 상태) — 연결 상태일 때만 pop하도록 수정. `m_i2c.c`는 연결이 끊겨도
     `BLE_DISCONNECT_STANDBY_TIMEOUT_TICKS`(300 tick=30초, 조정 가능) 동안은 즉시 멈추지
     않고 계속 측정+버퍼링하다가, 그 이상 끊겨 있으면 신규 `I2C_LED_MODE_STANDBY`(LED
     소등 + 2초마다 짧은 점멸로 대기 표시, 측정 중단으로 전력/발열 절감)로 전환. 재연결 시
     기존 10회 점멸 경로로 복귀 후 측정 재개. `reset_to_device_on()`의 `s_seq_num = 0`
     초기화 제거 — seq_num을 부팅 세션 내내 단조증가로 유지해야 gap 식별(아래 4번)이 가능.
  4. **[Sanity check + gap 식별]** `m_i2c.c`에 `check_sensor_sanity()` 추가 — module_err.h에
     이미 정의돼 있었지만 지금까지 아무도 판정하지 않던 `MODULE_ERR_SENSOR_SATURATION`/
     `MODULE_ERR_SENSOR_LOW_SIGNAL`을 실제로 채운다(고정 임계값, `config_app.h`
     `AS7341_SATURATION_THRESHOLD`/`AS7341_LOW_SIGNAL_THRESHOLD` — 정확한 채널별/게인별
     풀스케일 계산은 기존 gain/ATIME 캘리브레이션 open-item과 함께 추후 재확정).
     Gap 식별은 wire 프레임 변경 없이 위 3번의 seq_num 단조증가로 대체(앱이 seq_num
     불연속을 감지하면 로컬 버퍼 오버플로우로 유실된 구간). CRC/체크섬은 8바이트 고정
     프레임(테스트 APK 키건)에 추가할 여유가 없어 보류 — BLE 링크레이어 CRC24로 전송
     무결성이 이미 확보된다고 문서화(architecture.md §11).
  5. **[BLE 버전 characteristic 신규, 보안은 설계만]** AS7341_VERSION(0x1528, read-only)
     characteristic 신규 추가(`m_ble_gatt.c/h`) — `{fw_version, protocol_version(=1,
     m_ble_proto.h `BLE_PROTOCOL_VERSION`)}` 4바이트 반환. 순수 추가라 기존 CONFIG/
     DATA0/DATA1과 테스트 APK 호환성 영향 없음. **정정**: 이전 architecture.md에
     "펌웨어 버전 식별 기능 구현 완료"로 잘못 기록했었음 — 실제로는 `fw_version`이
     `nirs_sample_t`에만 있고 BLE로 전송되지 않았음(`m_ble_proto_encode_sample()`이 raw
     3값+led_index만 인코드), 이번에 이 characteristic으로 바로잡음. BLE 페어링/본딩/
     암호화(`CONFIG_BT_SMP`/`BONDABLE`)는 **비활성 상태로 `prj.conf`에 주석만 준비** —
     테스트 APK가 본딩을 지원하는지 불명해서 활성화 시 기존 검증된 연동이 깨질 위험이
     있어, 앱 쪽 확인 후 활성화하기로 함(architecture.md §11).
  6. **[OTA 인프라]** `prj.conf`에 `CONFIG_MCUMGR`/`CONFIG_MCUMGR_TRANSPORT_BT`/
     `CONFIG_IMG_MANAGER` 등 활성화(기존 주석 처리된 TODO 실행). **`pm_static.yml`은
     추가하지 않음** — 계획 단계에서는 신규 작성 예정이었으나, CHANGELOG v0.0.1/v0.0.11
     기록을 재확인한 결과 sysbuild 자동 파티션 매니저가 이미 MCUboot 2-image 빌드를
     정상 생성해온 이력이 있어(예: App Flash 32564B/216752B, mcuboot Flash 34768B/48KB),
     하드웨어 검증 없이 손으로 파티션 오프셋을 새로 지정하는 것이 CLAUDE.md §4/§14가
     경고하는 고위험 변경(브릭 위험)을 오히려 키운다고 판단 — 자동 파티셔닝에 맡기는
     쪽으로 계획을 변경함. 서명키는 sysbuild 기본 자동생성 디버그 키 사용(프로덕션 키
     관리는 별도 범위).
  - **검증 구분(§17)**: 코드 리뷰 완료, **빌드 검증 미실시**(이 세션 환경에 west/NCS
    툴체인이 없어 실행 불가 — 다음 실제 빌드 세션에서 clean build 확인 필요),
    하드웨어 검증 전부 미실시(watchdog RESETREAS 로그, BLE 강제 끊김→재연결 버퍼 flush,
    STANDBY 점멸 육안 확인, OTA 실기 업데이트/롤백/강제전원차단 3-시나리오 전부 미검증).

## v0.0.15 (2026-09-17)

- **빌드 환경 확인**: `C:\ncs\toolchains\`에 NCS v3.4.0 툴체인이 로컬에 설치돼 있음을
  확인 — `west build -b nrf52dk/nrf52832 Application`(sysbuild) clean build 성공.
  FLASH 152700B/216752B(70.45%), RAM 44152B/64KB(67.37%), `dfu_application.zip`
  생성 확인(OTA 페이로드 실제 산출물).
- **[버그 수정] MCUMGR Kconfig 오류(v0.0.14에서 유입)**: `CONFIG_MCUMGR_SMP_BT_AUTHEN`은
  존재하지 않는 심볼(작성자가 잘못 지어낸 이름)이라 Kconfig 경고로 빌드 자체가
  중단됐음 — 제거. `CONFIG_MCUMGR`가 `CONFIG_ZCBOR`에 의존하는데 누락돼 있었음 —
  `CONFIG_ZCBOR=y` 추가. `BT_SMP` 비활성 상태에서는 `MCUMGR_TRANSPORT_BT_PERM`
  choice가 자동으로 암호화 불필요 옵션(`_RW`)을 선택하므로 별도 설정 불필요함을 확인.
- **실기 플래시 검증**: MCUboot+App 2-image 플래시 완료(J-Link, `nrf52dk/nrf52832`
  타겟). 1차 시도에서 App 이미지 검증 실패(주소 0x0000c000 mismatch) — 점퍼 연결
  불안정성으로 추정, 재시도 후 mcuboot/App 둘 다 검증 통과.
- **[버그 수정, 중대] BLE notify 미구독 상태를 통신 실패로 오분류하던 문제**: RTT 실기
  로그에서 `BLE notify 실패(sensor=X, err=-22)`가 반복 발생하는 것을 발견 — 앱이 실제로
  연결돼 있었는데도 나타남. Zephyr 소스(`subsys/bluetooth/host/gatt.c` `gatt_notify()`)
  확인 결과 `bt_gatt_notify()`의 `-EINVAL`은 "해당 characteristic에 아직 notify
  구독(CCC) 안 됨"을 뜻하는 정상 상태이지 통신 실패가 아님을 확인(`-ENOTCONN`만
  실제로 "연결 안 됨"). 기존 코드(v0.0.12부터)는 `-ENOTCONN` 외의 모든 에러를 진짜
  실패로 취급해 `MODULE_ERR_BLE_TX_FAILED`를 보고하고 있었음 — 이 자체는 이전까지는
  무해했으나(DEGRADED 상태를 아무도 소비하지 않았음), v0.0.14의 `m_ctrl_is_safe_state()`
  도입으로 **앱이 연결 직후 아직 구독하기 전인 정상적인 과도 상태에서 측정을 영구
  중단시키는 회귀**로 이어질 뻔했음. `m_ble.c notify_sample()`이 `-EINVAL`도
  `-ENOTCONN`과 동일하게 정상(skip) 처리하도록 수정.
- **검증 구분(§17)**: 빌드 검증 완료, 실기 플래시 검증 완료. **BLE notify EINVAL 수정
  실기 재검증 완료** — 재빌드/재플래시 후 앱 연결 상태로 34초 연속 RTT 캡처, notify
  실패 경고 0건, DATA0/DATA1 notify enabled 확인, NIR1/NIR2 raw 데이터 100ms 간격
  연속 스트리밍 확인. `log_reset_cause()`도 실기에서 동작 확인(RESETREAS에 watchdog
  비트 포함된 것을 감지+로깅). Watchdog fault injection, STANDBY 점멸, OTA 실기
  업데이트/롤백/강제전원차단 3-시나리오는 여전히 미검증.

## v0.0.16 (2026-09-17)

- **[TEMP, OTA 실기 테스트용]** `TEMP_OTA_TEST_MARKER=1`(config_app.h) 추가 — 부팅
  직후(디바이스 On 순차점등 진입 전) LED 3개 동시 5회 빠른 점멸(`m_i2c.c i2c_init()`).
  OTA로 이 이미지가 실제로 올라갔는지 기존 이미지(v0.0.15, 순차 점등만 함)와 육안으로
  바로 구분하기 위한 용도 — **OTA 테스트 완료 후 반드시 0으로 되돌릴 것**(TODO 아님,
  영구 기능 아님). 기능 변경 없음, 오직 이 목적의 임시 마커.
- **[버그 수정, 중대] OTA 업데이트가 "버전 체크"로 거부되던 문제**: nRF Connect Device
  Manager에서 Start를 눌러도 업데이트가 진행되지 않는 문제 발생 — `dfu_application.zip`의
  `manifest.json`을 열어 확인한 결과 `version_MCUBOOT: "0.0.0+0"`으로 찍혀 있었음.
  `config_app.h`의 `FW_VERSION_*`(앱/BLE characteristic용)와 MCUboot 이미지 헤더 버전은
  완전히 별개 체계인데, 프로젝트에 `Application/VERSION` 파일이 없어서 **지금까지의
  모든 빌드가 전부 버전 `0.0.0+0`으로 찍히고 있었음** — 현재 보드의 이미지도, 새로
  올리려던 이미지도 버전이 동일해서 MCUboot/Device Manager가 업데이트로 인정하지
  않은 것으로 추정. `Application/VERSION` 신규 추가(`PATCHLEVEL=16`), pristine
  재빌드 후 `version_MCUBOOT: "0.0.16+0"`으로 정상 반영 확인.
  **운영 규칙(신규)**: 앞으로 패치마다 `config_app.h`의 `FW_VERSION_PATCH`와
  `Application/VERSION`의 `PATCHLEVEL`을 **함께** 올려야 한다 — 빌드 시스템이 두
  값을 자동으로 동기화해주지 않는다.
- **[버그 수정, 중대] OTA 업로드 시작 시 "GATT ERROR"로 실패하던 문제**: 버전 이슈
  해결 후에도 nRF Connect Device Manager에서 Start 시 `State: GATT ERROR`로 실패
  (SMP Service: DISCONNECTED). 원인: `CONFIG_MCUMGR_TRANSPORT_BT_REASSEMBLY`를
  켜지 않은 채였음 — 기본 ATT MTU(23byte)로는 SMP 업로드 요청이 여러 조각으로
  나뉘는데, 재조립(reassembly) 없이는 조각난 요청을 처리 못 해 GATT 에러로
  이어짐. NCS가 제공하는 공식 검증 Kconfig 번들
  (`nrf/samples/common/mcumgr_bt_ota_dfu/Kconfig`의
  `NCS_SAMPLE_MCUMGR_BT_OTA_DFU`/`..._SPEEDUP`)로 `prj.conf`의 수작업 MCUMGR
  설정을 전부 교체 — reassembly, 버퍼 크기(`MCUMGR_TRANSPORT_NETBUF_SIZE=1230`),
  연결 파라미터 제어, MTU 확장(`BT_L2CAP_TX_MTU=247`, `BT_BUF_ACL_TX/RX_SIZE=251`)이
  한 번에 올바르게 설정됨. 이 번들은 build-time에 설정 정합성을 자체 검증하는
  옵션도 포함(`NCS_SAMPLE_MCUMGR_BT_OTA_DFU_VALIDATION`, 기본 활성).
- **검증 구분(§17)**: 빌드 검증 완료(image 버전 0.0.17+0, pristine rebuild 성공,
  FLASH 오버플로우 없음). **실기 OTA 업로드 재검증 필요**(GATT ERROR 수정 후
  아직 미확인) — 다음 실기 테스트에서 확인.
- **[중요 교훈] OTA는 "현재 이미 설치된 이미지가 OTA를 받을 능력이 있어야" 성립**:
  GATT ERROR 수정(reassembly 등) 반영한 v0.0.17을 만들어도, 정작 보드에 실행 중이던
  이미지(v0.0.15, reassembly 없음)가 업데이트 요청 자체를 받을 능력이 없어서
  계속 실패했음 — Device Manager의 "Buffer details: 1 x 20 bytes"가 결정적 단서
  (재조립 없는 구버전의 SMP 버퍼 크기와 정확히 일치). **v0.0.17을 유선으로 한 번
  플래시한 뒤에야** OTA 수신 능력이 생김. 이후 v0.0.18(LED 8회 점멸 마커로 변경,
  기존 v0.0.17의 5회 점멸과 구분)을 실제로 **무선 OTA로 업데이트 성공** —
  Device Manager Buffer details가 `4 x 2475 bytes`로 정상 확인(reassembly 반영),
  Bootloader: MCUboot / Swap Without Scratch, 업데이트 후 재부팅 시 LED 8회
  점멸 육안 확인 + RTT 로그로 `Board FW Version: v0.0.18` 확인. **OTA 실기 검증
  완료** (§17) — 최초 1회는 반드시 유선 플래시 필요, 이후 SMP 설정이 유지되는 한
  버전 상승 방향으로는 무선 업데이트 가능함을 확인. 롤백/강제전원차단 시나리오는
  아직 미검증.

## v0.1.1 (2026-09-17)

- **버전 체계 전환**: OTA 실기 검증 성공을 기점으로 `FW_VERSION_MINOR`를 0→1로 올려
  새 관리 기준선을 v0.1.1로 삼는다(`config_app.h`, `Application/VERSION`). 이후
  패치는 v0.1.1 → v0.1.2 → ... `TEMP_OTA_TEST_MARKER`는 정식 버전이므로 0으로 원복.
  v0.0.18(OTA 최초 성공 버전)에서 이 버전으로 OTA 업그레이드 실기 검증 완료 —
  1차 시도 GATT ERROR(재시도로 성공, BLE 연동 준비 지연으로 추정·설정 문제 아님),
  재시도 후 RTT 로그로 `Board FW Version: v0.1.1` 정상 확인.
- **[정책 결정] OTA 다운그레이드 실기 검증 및 의도적 허용**: MCUboot
  `check_downgrade_prevention()`은 `CONFIG_MCUBOOT_DOWNGRADE_PREVENTION`이 켜져
  있을 때만 동작하는데, 현재 빌드에는 이 옵션이 없음을 `.config`로 확인 —
  즉 지금은 낮은 버전으로도 OTA가 그대로 적용된다. v0.1.1에서 v0.1.0(다운그레이드
  테스트용 임시 이미지, 실사용 릴리스 아님, LED 3회 점멸 마커)으로 실제 다운그레이드
  실기 검증 완료(RTT: `Board FW Version: v0.1.0`). **사용자 결정(2026-09-17)**:
  인증/테스트 단계에서는 다운그레이드를 의도적으로 열어둔다(펌웨어를 이전 버전으로
  되돌려 비교/재현해야 하는 경우가 있음) — **양산 배포 시점에 `CONFIG_MCUBOOT_
  DOWNGRADE_PREVENTION`을 활성화하는 것은 사용자가 별도로 진행**하기로 함
  (architecture.md §11에 기록).
- 다운그레이드 테스트 후 코드 상태는 다시 v0.1.1(정식 기준선)로 원복. 보드는
  v0.1.0 상태이므로 **다음 작업 전 v0.1.1로 다시 OTA 업그레이드 필요**.
- 사용자가 v0.1.0→v0.1.1 OTA 업그레이드 재실기, RTT로 `Board FW Version: v0.1.1`
  정상 확인.

## v0.1.2 (2026-09-17)

- **[신규] 부팅 버전 점멸 표시** — SWD/RTT 연결 없이(조립된 상태) 육안으로 OTA
  적용 여부를 확인하고 싶다는 요청으로 추가. 오늘 쓰던 `TEMP_OTA_TEST_MARKER`를
  대체하는 정식 기능으로 승격: 부팅 직후(디바이스 On 순차점등 진입 전) LED 3개가
  동시에 `FW_VERSION_PATCH + 1`회 점멸한다(`config_app.h` `FW_VERSION_BOOT_BLINK_MS`,
  `m_i2c.c i2c_init()`). +1은 PATCH=0일 때 "0회 점멸"(표시 없음)이 되는 것을 피하기
  위함 — v0.1.1은 2회, 이 버전(v0.1.2)은 3회 점멸. 업데이트 전/후 점멸 횟수 변화만
  보면 OTA 성공 여부를 육안으로 판단할 수 있다.
- **[정정] "seq_num 기반 gap 식별 가능"은 과장된 주장이었음** — `m_ble_proto_
  encode_sample()`의 DATA0/DATA1 8바이트 프레임에는 raw 3값+led_index만 들어가고
  `seq_num`은 애초에 전송되지 않는다(이전부터 그랬음, 오늘 seq_num 리셋을 없앤 것과
  무관하게 앱은 이 값을 볼 방법이 없음). architecture.md §11 항목6-[4] "gap 식별
  방식 확정"을 "인프라 미비, 실질적 gap 식별 불가"로 정정 필요 — TODO(open-item):
  AS7341_VERSION(0x1528)처럼 seq_num을 노출하는 추가 characteristic이 필요하다
  (기존 DATA0/DATA1 프레임 변경은 테스트 APK 호환성 문제로 비권장).
- **[실기 검증 완료] OTA 강제 전원차단 시나리오**: 업데이트 전송 도중 디바이스 전원을
  끊으면 MCUboot가 손상된 이미지로 스왑하지 않고 원래(이전) 버전으로 안전하게
  부팅 롤백됨을 확인. 재부팅 후 BLE도 자동으로 재연결되어 OTA를 이어서 진행할 수
  있음을 실기로 확인 — "Swap Without Scratch" 방식의 원자적 스왑 안전성이 실제로
  보장됨을 검증. 남은 미검증 항목은 "의도적으로 손상시킨(서명 불일치) 이미지
  업로드 시 롤백" 시나리오뿐.
- **[버그 수정] gap 식별용 seq_num이 실제로는 앱에 전달되지 않던 문제** — 신규
  AS7341_SEQ(0x1529, notify-only, u32 LE) characteristic 추가(`m_ble_gatt.h/c`,
  `m_ble_proto.c m_ble_proto_encode_seq()`, `m_ble.c notify_seq()`). DATA0/DATA1과
  같은 tick에서 함께 notify되므로, 앱은 최근 SEQ/DATA notify를 짝지어 seq_num
  불연속을 감지하면 그 구간이 로컬 buffer 오버플로우로 유실된 실측 구간임을 알 수
  있다. 순수 추가(additive)라 기존 CONFIG/DATA0/DATA1/VERSION 동작과 테스트 APK
  호환성에 영향 없음(앱이 이 characteristic을 구독하지 않으면 그냥 무시됨).
  **빌드 검증 완료, 실기 검증 필요**(테스트 APK가 이 characteristic을 아직 모르므로
  BLE 스니퍼 또는 nRF Connect 범용 앱으로 notify 값 확인 필요).
- **[실기 검증 완료] SEQ characteristic 실기 확인** — nRF Connect 범용 앱으로 구독,
  100ms 간격으로 notify 값이 올라옴을 확인. **설계적 한계 발견**: BLE Notification은
  ATT 레벨에서 ACK가 없는 fire-and-forget이라, 관찰상 불규칙하게(+2 정도) 값이
  건너뛰는 현상이 있었음 — RTT로 `RING_BUFFER_OVERFLOW`/`BLE notify 실패` 로그가
  전혀 없었던 것으로 볼 때, 이는 진짜 데이터 유실이 아니라 SEQ notify 패킷 개별
  전달 실패(무해)로 판단됨. 이 방식은 "진짜 gap"과 "notify 전달 실패"를 구분하지
  못하는 한계가 있음(architecture.md §11 항목6-[4] 기록).

## v0.1.3~v0.1.4 (2026-09-17)

- **[TEMP, watchdog 실기 fault injection 테스트용]** `TEMP_WATCHDOG_FAULT_INJECT_TEST`
  추가 — 부팅 후 20번째 tick(약 2초)에서 `m_i2c` 태스크가 고의로 무한 대기하며
  `m_ctrl_notify_alive()` 호출을 멈춘다. **실기 검증 완료**: RTT로 연속 캡처한 결과
  ①경고 로그 발생(t≈3.2s) → ②약 4초 후 하드웨어 watchdog이 SoC 강제 리셋 →
  ③재부팅 시 `log_reset_cause()`가 `RESETREAS=0x00000010`(watchdog 비트)을 정확히
  감지+경고 로그 — 이 사이클이 반복되는 것을 두 번 연속 확인. **item [2](Watchdog
  안전 재시작 시퀀스) 실기 검증 완료로 확정**.
- 테스트 완료 후 `TEMP_WATCHDOG_FAULT_INJECT_TEST`를 0으로 원복(v0.1.4) — 무한
  리셋 루프에서 정상 동작으로 복귀.
