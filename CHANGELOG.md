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
