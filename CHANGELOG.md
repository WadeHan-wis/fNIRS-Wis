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

## v0.1.5 (2026-09-18)

- **주간과제4(BLE 실 스택+APK 연동 PWM 제어) 미달성 지점 해소: AS7341_CONFIG의
  cycle/active window(offset 6-9)를 실제 LED 구동에 반영**. 그동안 `m_ble_proto.c`가
  파싱·클램프만 하고 `m_i2c.c`는 duty(%)만 적용해서, 앱이 "1초 주기 중 200ms만
  점등"처럼 설정해도 실기에서는 duty%로 계속 켜져 있는 상태였다(`m_ble_proto.h`의
  TODO(open-item) 참고).
- `m_i2c.c`에 tick 기반 게이팅 추가: RTC 100ms tick 주기 자체(§2.3 Bresenham 보정)는
  건드리지 않고, ACQUISITION 모드에서 "이번 tick이 active 구간인지"만
  `s_gate_tick_count % s_gate_cycle_ticks < s_gate_active_ticks`로 판정한다.
  active 구간이 아니면 `acquire_one_sample()` 자체를 건너뛴다 — LED만 끄고 AS7341
  read를 계속하면 `check_sensor_sanity()`가 매 tick `LOW_SIGNAL`로 오판해
  `g_ctrl_status`를 영구 `WARNING`으로 전환시키는 부작용이 있어(다운그레이드 경로
  없음, `m_ctrl.c` 참고) acquire 자체를 스킵하는 방식을 택했다. `seq_num`은 push된
  샘플에만 증가하므로 gap 식별(SEQ notify, §11 항목6-4) 의미도 그대로 유지된다.
- 기본값은 cycle=1 tick/active=1 tick(항상 active, 게이팅 없음) — BLE가 CONFIG를
  한 번도 안 보낸 상태(`TEMP_AS7341_READ_TEST` 등)에서 기존 검증된 매 tick 측정
  동작을 그대로 보존하기 위함. `cycle=1000ms/active=200ms` 기본 프로토콜 값은 우리
  100ms RTC tick과 정수배(10 tick/2 tick)로 정확히 나누어떨어짐을 확인.
- TODO(open-item): 빌드/실기 검증 미실시(이 세션 환경에 west/NCS 툴체인 없음, §17).
  다음 실제 빌드 세션에서 clean build 확인 후 앱 연동 실측 필요.

## v0.1.6 (2026-09-18)

- **[코드 변경 없음, 문서만] v0.1.5 실기 검증 결과 정리**: cycle/active window 게이팅이
  실제로 정상 동작함을 RTT 로그로 확인 — `gate=1/1 tick`(cycle=100ms/active=20ms처럼
  1 tick 미만인 값)에서는 게이팅이 no-op이 되는 것도 예측대로 확인됨. `cycle=1000ms/
  active=200ms`로 재테스트 시 육안으로도 정상 동작 확인(사용자 확인, 2026-09-18).
- **BLE 연결 해제 후 LED가 즉시 순차 점등으로 안 돌아오는 것처럼 보인 문제 조사** —
  펌웨어 버그가 아니라 **테스트 APK가 disconnect 후 자동 재연결을 시도하지 않아서**
  항목6-[3]의 30초 grace period(`BLE_DISCONNECT_STANDBY_TIMEOUT_TICKS`)를 매번 끝까지
  체감하게 되는 것으로 확인됨(advertising 자체는 disconnect 즉시 정상 재시작, RTT 로그
  확인). **펌웨어 동작은 그대로 유지하기로 결정** — architecture.md §11 항목7로 기록,
  `m_i2c.c`에 TODO(open-item) 주석 추가(앱 쪽 자동 재연결 로직 추가 필요, 펌웨어 범위 아님).

## v0.1.7 (2026-09-18)

- **오늘 할 일 중 앱 변경과 무관하게 FW 단독으로 안전한 항목만 적용**: BLE
  암호화(항목5-[4])는 architecture.md §11에 이미 "앱 본딩 지원 확인 전까지 보류
  확정"으로 기록돼 있어 제외, OTA 손상 이미지 롤백 검증은 코드 패치가 아닌 수기
  검증 절차라 제외. gap 식별 개선(항목5-[3])만 순수 추가 인프라로 적용.
- **AS7341_DROPPED_COUNT(0x152A, read-only, u32 LE) characteristic 신설**
  (`m_ble_gatt.h`/`m_ble_gatt.c`) — VERSION(0x1528)/SEQ(0x1529)와 동일한 순수 추가
  패턴, 구버전 앱 호환성 영향 없음. `m_i2c_ring_buffer_get_dropped_count()`가
  구현만 되고 아무도 호출하지 않던 죽은 코드였음을 발견해 여기 연결했다 — 앱이
  이 값과 SEQ 불연속을 대조하면 "진짜 ring buffer overflow"와 "SEQ notify 개별
  무해 유실"을 구분할 수 있다(실제 대조 로직은 앱 쪽 작업으로 남김).
- **[버그 수정] `m_i2c_ring_buffer_get_dropped_count()`/`get_max_usage()`가 mutex
  보호 없이 값을 읽고 있었다** — 지금까지 호출부가 없어서 문제가 드러나지 않았지만,
  이번에 BT 호스트 스레드(GATT read 콜백)에서 처음으로 cross-task 호출하게 되면서
  `mtx_ring_buffer`로 감싸도록 수정(codingstandard.md §8).

## v0.1.8 (2026-09-18)

- **[코드 변경 없음, 문서만] OTA 손상 이미지 롤백 케이스 A 실기 검증 완료**: 정상
  서명된 `zephyr.signed.bin`의 payload 중간 1바이트만 변조한 사본을 OTA로 업로드 →
  재부팅 후 `Board FW Version`/git hash가 업데이트 전과 완전히 동일(v0.1.5-
  45c7b088fdec)하고 I2C/BLE init도 전부 정상 — MCUboot가 서명 검증에서 손상된
  이미지를 거부하고 swap을 진행하지 않은 것으로 판단(MCUboot 자체 부팅 로그는
  캡처하지 못해 100% 확정은 아님, 정황 증거 기준). architecture.md §11 항목6-[6]에
  기록.
- **남은 케이스 B(서명 정상, confirm 미실행 시 자동 롤백)는 미검증** — west 빌드로
  구분 가능한 새 버전 이미지가 필요해서 이번 세션에서는 진행하지 못함.

## v0.1.9 (2026-09-18)

- **[코드 변경 없음, 문서만] OTA 케이스 B 시도 — 근거 불완전, 사용자 판단으로 pass
  처리**: v0.1.8을 test 업로드 후 confirm 없이 리셋해서 RTT 확인 시도. 1차 부팅
  (v0.1.8이어야 할 부팅)의 `Board FW Version` 배너가 RTT 캡처 과정에서 깨져서 안
  보였고, 2차 부팅만 `v0.1.5`로 깨끗하게 잡힘 — "1차에서 v0.1.8이 떴다가 롤백된 것"과
  "애초에 swap이 안 일어난 것"을 로그만으로 구분 불가. 사용자가 육안 관찰 근거로
  pass 처리했으나 **RTT 로그 상 명확한 증거는 없음** — architecture.md §11 항목6-[6]에
  근거 불완전 상태 그대로 기록. 재확인 필요 시 리셋 전 RTT 터미널을 미리 연결해서
  1차 부팅 배너부터 캡처해야 함.
- **후속(같은 날)**: Test 업데이트를 반복 실행하면서 매번 동일한 패턴(재부팅 후 이전
  버전으로 복귀)이 일관되게 재현됨을 확인 — 사용자 판단으로 케이스 B도 검증 완료
  (100%) 처리. architecture.md §11 항목6-[6] 갱신, 정식 자동화 TC는 양산 단계에서
  추가 예정.

## v0.1.10 (2026-09-18)

- **Safety 인증 대응 항목5-[2]/[3] 실기 검증 가시화용 로그 추가** (코드 리뷰로는
  완료 상태였으나 실기로 한 번도 트리거해본 적 없던 두 경로):
  - `enter_standby()`(`m_i2c.c`)에 STANDBY 진입 로그 추가 — 30초 grace period
    (`BLE_DISCONNECT_STANDBY_TIMEOUT_TICKS`) 만료 시점을 RTT로 확인 가능하게 함.
  - ring buffer overflow 시(`m_i2c_ring_buffer_push()` 반환값 체크 지점) 누적
    dropped count를 포함한 경고 로그 추가.
  - `check_sensor_sanity()` 실패 시(SATURATION/LOW_SIGNAL) 어느 쪽인지 구분되는
    경고 로그 추가.
  - 세 로그 모두 동작 변경 없이 가시성만 추가한 것 — RING_BUFFER_CAPACITY(150)
    기준 10Hz 샘플링이면 약 15초 연결 끊기면 overflow, 30초면 STANDBY 진입이라
    한 번의 45초 연결 끊기 테스트로 두 경로를 동시에 확인 가능.

## v0.1.11 (2026-09-18)

- **[코드 변경 없음, 문서만] 항목5-[2]/[3] 실기 검증을 다음으로 연기하기로 결정** —
  SWD 연결이 불안정해서(9/17 watchdog 검증 때와 동일한 문제) RTT 로그 캡처 자체가
  너무 어려움. `JLinkRTTLogger.exe`로 파일에 직접 기록하는 방법을 안내했으나, SWD
  안정성 문제가 근본 원인이라 오늘은 검증을 보류. v0.1.10에서 추가한 로그는 그대로
  유효하니 SWD 환경이 안정적일 때 재시도하면 됨. architecture.md §11 항목6-[2]/[3]에
  연기 사유 기록.

## v0.1.12 (2026-09-21)

- **[버그 수정] AS7341 SP_EN(스펙트럴 엔진)을 매 tick마다 재기록하던 것을 init 시
  1회만 켜도록 정정** — `_ref_fnirs_example`(같은 AS7341 3파장 F7/F8/NIR 구성을 실사용
  검증한 레퍼런스, `src/as7341.c`)과 측정 방식을 대조하는 과정에서 발견. 레퍼런스는
  `as7341_init_device()`에서 SMUX 구성 직후 SP_EN을 1회만 켜고 이후 주기적 읽기
  (`as7341_read_current_mux_channels_device()`)에서는 STATUS2.AVALID 폴링과 채널
  읽기만 하고 ENABLE 레지스터를 다시 건드리지 않는다(datasheet가 문서화하는
  free-running SPM 모드 그대로). 우리 `m_i2c_as7341_read_raw()`는 매 호출(매 tick)마다
  `ENABLE=PON|SP_EN`을 재기록하고 있었음 — architecture.md §11 항목5의 미해결 우려사항
  ("SP_EN을 매 tick마다 재기록하지만 free-running 모드라 LED on 구간과 실제 적분 구간이
  정확히 일치한다는 보장이 없음", v0.0.10 기록)과 직결되는 부분. `m_i2c_as7341_init()`에
  SMUX 구성 성공 직후 SP_EN 1회 활성화를 추가하고, `read_raw()`의 재기록 코드는 제거해
  레퍼런스와 동일한 free-running 방식으로 정정. 불필요한 I2C 쓰기 1회/tick도 함께
  줄어듦(v0.0.10 기록된 NIR1+NIR2 순차 최악 400ms 폴링 부담에 소폭 도움).
- **[대조 결과] SMUX RAM 20바이트 설정값은 레퍼런스와 완전히 일치함을 확인** —
  `m_i2c_as7341.c`의 `s_smux_config_f5f8_clear_nir[20]`과 레퍼런스
  `as7341_setup_f5f8_clear_nir()`의 20바이트 값이 레지스터 0x00~0x13 전부 동일. 기존
  TODO(open-item, architecture.md §11 항목5 "공개 레퍼런스 구현 값을 우선 적용, ams 공식
  앱노트 교차검증 필요")의 교차검증 근거 하나가 추가로 확보됨(정식 앱노트 대조는 아니지만
  독립적인 실사용 레퍼런스와 값이 일치).
- **[구조적 차이, 패치 대상 아님] LED 구동 방식은 레퍼런스와 동일하지 않으나 하드웨어
  토폴로지 자체가 다름** — 레퍼런스는 RED/IR 2채널 LED를 2개 물리 위치(location)에
  배치해 놓고 위치 전환 타이머로 순환하며, LED는 BLE CONFIG로 받은 intensity로
  **계측 타이밍과 무관하게 상시 점등**(강도 제어만, 스트로빙 없음). 반면 이 프로젝트는
  640/680/950nm 3파장 LED가 한 위치에 개별 실장돼 있고(pinmap.md §3), 테스트 APK
  프로토콜(디컴파일 확인, CHANGELOG v0.0.11)이 요구하는 파장별 duty + cycle/active
  게이팅(v0.1.5)을 그대로 유지해야 함 — 레퍼런스의 "상시 점등" 방식을 이 프로젝트에
  그대로 옮기면 기존 검증된 APK 연동(CONFIG write→LED 제어)이 깨진다. 레퍼런스와
  구조가 다른 것이 확인된 것이지 버그가 아니므로 패치하지 않음.

## v0.1.13 (2026-09-21)

- **[정책 반영] AS7341 적분시간 파라미터화를 레퍼런스와 동일한 방식으로 정정** — 논문
  (Ban et al., §11 항목5) 재현 가능한 장비를 먼저 만들고 그 위에서 성능을 올리기로
  한 사용자 결정에 따라, "레퍼런스와 동일 세팅"을 우선 적용. 기존에는 ASTEP=999
  고정 + ATIME 가변(물리적으로 등가이나 레퍼런스와 반대 방향)이었던 것을,
  `_ref_fnirs_example`(src/as7341.c `as7341_set_integration_20ms_device()`)과 동일하게
  **ATIME=99(`AS7341_ATIME_FIXED`) 고정 + ASTEP 가변**으로 변경(`m_i2c_as7341.c/.h`).
  `m_i2c_as7341_set_integration_time()`의 파라미터 의미가 "ATIME 레지스터값"에서
  "integration_20ms_units"(BLE AS7341_CONFIG 필드와 동일 단위, 레퍼런스와 동일 semantics)로
  바뀌었다 — `m_i2c.c apply_pending_ble_config()`의 별도 단위 변환 코드
  (`integration_units_to_atime()`)도 더 이상 필요 없어 삭제.
- **[기본값 변경, 실기 재검증 필요]** 부팅 시 기본 적분시간을 기존 83.4ms(ATIME=29/
  ASTEP=999)에서 레퍼런스 기본값과 동일한 **약 19.7ms**(units=1, ATIME=99/ASTEP=70,
  `_ref_fnirs_example`의 `AS7341_DEFAULT_INTEGRATION_20MS=1`과 동일)로 변경. gain=9(256x)는
  그대로 유지(레퍼런스도 CFG1 리셋 기본값이 동일하게 9). 83.4ms는 2026-09-15 실기에서
  "6채널 모두 안정적인 비영 값" 확인된 값이었으므로, 19.7ms로 짧아진 이번 기본값에서도
  신호가 노이즈 수준으로 떨어지지 않는지 **다음 실기 검증에서 재확인 필요**(TODO
  open-item, architecture.md §11 항목5의 gain/ATIME 실측 캘리브레이션과 함께 처리 예정).
- **빌드 검증 완료** — `west build -b nrf52dk/nrf52832 Application -p always`(VERSION 파일
  변경 반영을 위해 pristine 필요, v0.0.16과 동일 이슈) clean build 성공. FLASH 156796B/
  216752B(72.34%), RAM 57584B/64KB(87.87%, 기존 v0.0.15 대비 RAM 사용량이 늘어난 것은
  이번 변경이 아니라 그 사이 추가된 BLE/OTA 버퍼 설정 누적분). `dfu_application.zip`
  manifest 확인 결과 `version_MCUBOOT: "0.1.13+0"` 정상 반영. **하드웨어 검증은 미실시**
  (이 세션 환경엔 실물 보드/J-Link 연결이 없음) — `west flash`로 실기 플래시 후 raw 데이터
  질적 변화(19.7ms 적분시간에서도 노이즈 수준으로 안 떨어지는지) 확인 필요.

## v0.1.14 (2026-09-21)

- **[버그 수정, 고위험(Watchdog/Reset)] v0.1.13 플래시 후 보드가 부팅 중 재부팅되는 현상
  — 원인: 버전 점멸 LED 블로킹 구간에서 watchdog stale 오탐**. 사용자가 "이번 패치 이후
  리셋되는 것 같다"고 보고, 버전 점멸 LED를 원인으로 의심해 조사 요청.
  - `m_ctrl.c watchdog_init()`은 CTRL 태스크 시작 즉시(부팅 초반) watchdog을 무장한다
    (`WATCHDOG_TIMEOUT_MS=4000ms`). `feed_watchdog_if_alive()`는 I2C/BLE 두 소스 모두
    최근 `WATCHDOG_ALIVE_STALE_MS(1000ms)` 이내에 `m_ctrl_notify_alive()`를 호출했어야 feed.
  - `m_i2c.c i2c_init()`의 버전 점멸 루프(`FW_VERSION_BOOT_BLINK_MS=150ms` × 2 ×
    `(FW_VERSION_PATCH+1)`회)는 **블로킹**인데, I2C 태스크의 첫 `m_ctrl_notify_alive
    (CTRL_ALIVE_I2C)` 호출은 이 루프가 끝나고 RTC tick 메인루프에 들어가야만 발생한다.
    v0.1.13(PATCH=13) 기준 점멸 시간이 **14×300ms=4200ms**로 watchdog 타임아웃(4000ms)을
    이미 초과 — `FW_VERSION_PATCH`가 매 패치(CHANGELOG 규칙상 무조건 +1)마다 늘어나므로
    이전부터 존재하던 시한폭탄이 이번 버전에서 실제로 터진 것(v0.1.11=3600ms, v0.1.12=
    3900ms로 이미 여유가 거의 없었음).
  - 추가로 `m_ble.c ble_init()`이 `k_sem_take(&sem_i2c_init_done, K_FOREVER)`로 I2C 초기화
    완료를 기다리는 동안 BLE의 첫 alive 신호도 함께 지연돼, 부팅 초반 **두 소스가 동시에
    stale**해지는 구조였다(둘 다 필요조건이라 하나만 고쳐도 근본 해결이 안 됨).
  - **수정**: ①`m_i2c.c` 점멸 루프 매 반복 + AS7341 init 완료 직후에 `m_ctrl_notify_alive
    (CTRL_ALIVE_I2C)` 추가. ②`m_ble.c`의 `K_FOREVER` 대기를 `K_MSEC(200)` 타임아웃 반복
    폴링으로 변경, 대기 중에도 매번 `m_ctrl_notify_alive(CTRL_ALIVE_BLE)` 호출. 둘 다 정상
    대기/초기화 구간에서만 alive를 더 자주 보고하는 추가적 변경이라, 실제 I2C 버스 행/BLE
    스택 초기화 실패 등 **진짜 hang은 여전히 감지**한다(watchdog 보호 기능 약화 없음).
  - **빌드 검증 완료**: clean pristine rebuild 성공, `version_MCUBOOT: "0.1.14+0"` 확인.
    **하드웨어 검증 필요**(실기에서 부팅 후 재부팅 없이 정상 진행되는지 RTT로 확인) — 이
    세션 환경에는 실물 보드 없음.
  - TODO(open-item): `FW_VERSION_PATCH`가 계속 늘어나면 버전 점멸 시간 자체는 계속
    길어진다(현재 v0.1.14 기준 15×300ms=4500ms) — watchdog stale 문제는 해소했지만, 부팅
    체감 시간이 계속 늘어나는 것은 별개 이슈. 점멸 횟수를 `FW_VERSION_PATCH % N` 등으로
    캡하는 것을 다음에 검토 필요(architecture.md §11 후보 등록 권장).

## v0.1.15 (2026-09-21, **롤백됨 — 아래 v0.1.16 참고**)

- **[신규, 고위험(보안), 같은 날 v0.1.16에서 롤백됨] BLE 페어링/본딩 활성화 — architecture.md §11 항목6-[5]에 설계만
  준비돼 있던 것을 사용자 결정으로 활성화(펌웨어 먼저 구현 후 앱에 적용 예정)**.
  - `prj.conf`: `CONFIG_BT_SMP=y`/`CONFIG_BT_BONDABLE=y` 활성화. 재부팅 후에도 "자동
    재연결"이 유지되도록 본딩 정보(LTK)를 flash(NVS)에 영구 저장하는
    `CONFIG_BT_SETTINGS`/`CONFIG_SETTINGS`/`CONFIG_NVS`/`CONFIG_FLASH_MAP` 계열 신규 추가.
  - `m_ble_gatt.c`: `CONFIG` characteristic read/write, `DATA0`/`DATA1`의 CCC descriptor
    permission을 `BT_GATT_PERM_*_ENCRYPT`로 변경 — 본딩(암호화)되지 않은 연결은 CONFIG
    접근/DATA0·DATA1 구독이 거부된다.
  - `m_ble.c`: `on_connected()`에서 연결 즉시 `bt_conn_set_security(conn, BT_SECURITY_L2)`
    호출로 본딩을 선제적으로 트리거(앱이 characteristic에 접근할 때까지 기다리지 않음).
    `bt_conn_auth_cb`/`bt_conn_auth_info_cb` 등록, `security_changed` 콜백 추가(결과 로깅).
    `bt_enable()` 이후 `settings_load()` 호출로 이전 세션의 본딩 정보 복원.
  - **IO capability 결정**: 이 보드는 디스플레이/입력 수단이 전혀 없어(pinmap.md)
    passkey/confirm 콜백을 등록하지 않았다 — Zephyr가 자동으로 NoInputNoOutput으로
    취급해 **Just Works 페어링만 수행**(MITM 인증 불가, 하드웨어 구조상 불가피).
  - **부수 효과**: `CONFIG_BT_SMP=y`로 `CONFIG_NCS_SAMPLE_MCUMGR_BT_OTA_DFU`의
    `MCUMGR_TRANSPORT_BT_PERM`도 암호화 필요 옵션으로 자동 전환됨 — OTA(SMP)도 이제
    본딩된 연결에서만 가능해진다(기존 OTA 검증, CHANGELOG v0.0.17 이하는 비암호화
    기준이었으므로 재검증 필요).
  - **빌드 검증 완료**: clean pristine rebuild(`west build -b nrf52dk/nrf52832 Application
    -p always`) 성공, `dfu_application.zip` manifest `version_MCUBOOT: "0.1.15+0"` 확인.
  - **미검증 상태**: 테스트 APK가 BLE 페어링 UI/본딩을 지원하는지 이 세션에서 확인하지
    못했다 — 지원하지 않으면 기존 앱 연동(v0.0.12 검증분)이 CONFIG/DATA0/DATA1에서 깨질
    수 있다. **하드웨어 검증(실제 페어링 시퀀스, 재부팅 후 재페어링 없이 재연결)은 이
    세션에 실물 보드가 없어 미실시**.
  - TODO(open-item): 앱 쪽 본딩 지원 확인 및 대응 완료 후, OTA(SMP) 재검증 필요.

## v0.1.16 (2026-09-21)

- **[롤백] v0.1.15 BLE 페어링/본딩 전면 롤백 — 사용자 결정**: "본딩 로직은 롤백해줘,
  아직은 구현할 필요가 없을 것 같다 — 앱과 연동이 끊어진 후 디바이스가 다시 advertising
  으로 전환되서 재연결만 되면 충분하다"는 판단. 그 재연결 동작은 v0.1.15 이전부터
  `m_ble.c on_disconnected()`가 이미 담당하고 있어(연결 해제 시 advertising 재시작,
  2026-09-16 검증 완료) 별도 구현이 필요 없었다.
  - `prj.conf`: `CONFIG_BT_SMP`/`BONDABLE`/`SETTINGS`/`NVS`/`FLASH_MAP` 계열 전부 원복,
    항목6-[5] 주석을 "설계만 준비, 비활성" 원래 상태로 되돌리되 2026-09-21 활성화→롤백
    이력만 짧게 추가.
  - `m_ble_gatt.c`: CONFIG characteristic, DATA0/DATA1 CCC permission을
    `BT_GATT_PERM_READ`/`WRITE`(암호화 불필요)로 원복.
  - `m_ble.c`: `bt_conn_set_security()`/`bt_conn_auth_cb`/`bt_conn_auth_info_cb`/
    `security_changed`/`settings_load()` 등 v0.1.15에서 추가한 코드 전부 제거.
  - v0.1.15는 이 세션에서 커밋되지 않았고 실물 보드에 플래시된 적도 없어, 기존 앱 연동
    (v0.0.12)에 실질적 영향은 없었음.
  - architecture.md §11 항목6-[5]는 다시 "보류" 상태로 복귀.

## v0.1.17 (2026-09-21)

- **[버그 수정] BLE 연결 끊김 시 ring buffer 용량과 grace period(§11 항목6-[3]) 불일치로
  인한 실제 데이터 유실 — architecture.md §2.5(`RING_BUFFER_CAPACITY`)와 §11 항목6-[3]
  (`BLE_DISCONNECT_STANDBY_TIMEOUT_TICKS`)가 서로 다른 시점에 독립적으로 정해지면서
  생긴 불일치를 사용자 요청("연결 끊겼을 때 센싱 데이터 ring buffer 저장 로직 구현")으로
  재검토하다 발견**.
  - "끊겨도 계속 측정+버퍼링" 로직 자체(`m_i2c.c`/`m_ble.c`)는 v0.0.14부터 이미 구현돼
    있었으나, `RING_BUFFER_CAPACITY`(150샘플=15초)가 `BLE_DISCONNECT_STANDBY_TIMEOUT_
    TICKS`(300 tick=30초)보다 작아서, 30초 끊김이 나면 15초 지점부터 이전 데이터가
    overflow로 실시간 덮어써져 유실됐다(`m_i2c_ring_buffer_push()` 특성상 silent는
    아니고 dropped_count는 증가하지만, 실제로는 유실).
  - `config_app.h`: `RING_BUFFER_CAPACITY`를 `BLE_DISCONNECT_STANDBY_TIMEOUT_TICKS`를
    직접 참조하도록 묶어 앞으로 두 값이 다시 어긋나지 않게 함.
  - **1차 시도(300 tick=30초 그대로 유지) 되돌림**: RAM 예산 검토 결과 Application 이미지
    RAM 사용률이 87.8%(150샘플, 기존)→96.07%(300샘플)까지 올라가는 것을 빌드로 실측,
    스택 오버플로우 등 위험 대비 여유(2.5KB)가 부족하다고 판단해 사용자에게 확인 요청.
  - **사용자 결정: "grace period를 버퍼가 감당 가능한 수준으로 단축"** — `BLE_DISCONNECT_
    STANDBY_TIMEOUT_TICKS`를 300→**220 tick(22초)**으로 하향. 결과 RAM 사용률 91.67%
    (60,080/65,536B)로 안전 마진 확보. 22초까지는 데이터 유실 0% 보장(그 이상 끊기면
    STANDBY 진입으로 측정 자체가 멈춰서 추가 유실도 없음).
  - **빌드 검증 완료**: clean pristine rebuild, `dfu_application.zip` manifest
    `version_MCUBOOT: "0.1.17+0"` 확인. **하드웨어 검증은 미실시**(실물 보드 없음) —
    실제로 22초 끊김/재연결 시나리오에서 데이터 gap이 없는지, 22초 초과 시 STANDBY로
    정상 전환되는지 실기 확인 필요.
  - TODO(open-item): 30초 grace period라는 원래 §11 항목6-[3] 값 자체가 정책값이
    아니라 초기값이라고 명시돼 있었음 — 이번 22초 변경도 마찬가지로 실측 후 조정
    가능한 값. RAM 여유를 늘리려면(예: 배터리/온도 센서 등 향후 기능 추가 시) 다시
    검토 필요.

## v0.1.18 (2026-09-21)

- **[프로토콜 변경, v2] DATA0/DATA1 notify 프레임에 timestamp_us/seq_num 추가 (8→16바이트)**
  — 사용자 요청, `NCS_TedreamS2`(`Application/src/a/m_a_ble.c`) 분석 반영. S2는 센서
  페이로드를 `[timestamp 4B LE][센서 데이터]` 순서로 구성하는 관례가 있어(architecture.md
  §2.4 "32bit us 타임스탬프" 정책과 일치) 이를 그대로 계승, S2의 SALT/VER 핸드셰이크·
  AES-CCM 암호화는 이 프로젝트 범위 밖이라 가져오지 않음(agents.md §4).
  - 새 프레임(`m_ble_gatt.h`/`m_ble_proto.h`/`m_ble_proto.c`): offset 0-3 timestamp_us
    (u32 LE), 4-7 seq_num(u32 LE), 8-9 Red630, 10-11 Red680, 12-13 NIR, 14-15 LED
    index(전부 u16 LE).
  - `BLE_PROTOCOL_VERSION` 1→2(`AS7341_VERSION` characteristic 0x1528로 조회 가능).
  - 앱 쪽도 8→16바이트 프레임 파싱으로 업데이트 필요(구버전 앱은 이 프레임을 더 이상
    올바르게 파싱 못 함 — 사용자가 앱을 함께 관리하므로 이번 범위에 포함해 진행).
  - 기존 `AS7341_SEQ`(0x1529) notify characteristic은 제거하지 않고 유지(하위 호환).
  - 기본 ATT MTU(23바이트, payload 20바이트) 안에 16바이트 프레임이 여전히 들어가므로
    별도 MTU 협상 없이도 동작.

- **[버그 수정, 고위험(안전상태)] BLE notify 혼잡(-ENOMEM) 시 안전상태 영구 래치되는
  회귀 방지** — v0.1.17에서 발견된 위험(재연결 후 최대 220샘플 backlog를 텀 없이 몰아
  notify하면 ATT 송신 버퍼 풀 고갈로 `bt_gatt_notify()`가 `-ENOMEM`을 반환할 수 있는데,
  기존 코드는 이를 `MODULE_ERR_BLE_TX_FAILED`로 오분류해 `m_ctrl.c`가
  `CTRL_STATE_DEGRADED`로 래치 — IEC 60601 단일고장 철학에 따라 **재부팅 전까지 자동
  복구 안 됨**, 즉 정상적인 backlog flush 상황에서 측정이 영구 정지될 수 있었음)를
  해소.
  - `m_ble.c`: `notify_sample()`/`notify_seq()`가 `-ENOMEM`을 `-ENOTCONN`/`-EINVAL`과
    같은 "무해한 전달 실패"로 분류(architecture.md §11 항목6-[4] 원칙과 일관) —
    `MODULE_ERR_BLE_TX_FAILED` 미보고, 안전상태 래치 안 됨.
  - 추가로 congestion(`-ENOMEM`) 감지 시 다음 pop 전 `BLE_NOTIFY_CONGESTION_BACKOFF_MS`
    (20ms) 대기를 넣어 컨트롤러가 송신 큐를 비울 시간을 확보 — 텀 없는 재시도로 혼잡이
    반복되는 것을 완화.
  - **빌드 검증 완료**: clean pristine rebuild, `dfu_application.zip` manifest
    `version_MCUBOOT: "0.1.18+0"` 확인. **하드웨어 검증 미실시** — 실제로 220샘플
    backlog flush 시 `-ENOMEM`이 재현되는지, congestion backoff로 안전상태 래치 없이
    정상 flush되는지, 새 16바이트 프레임을 앱이 올바르게 파싱하는지 모두 실기 확인 필요.

## v0.1.19 (2026-09-21)

- **[전력 최적화] DATA0/DATA1 배칭 도입 + LED index 필드 제거 + connection interval/
  peripheral latency 조정 요청** — 사용자 지적: "지금 Data에 LED Index가 필요한가?
  10Hz마다 매번 notify하면 전력 소모가 클 것 같다, 프레임을 키워서 데이터를 묶어
  보내자." 사용자와 두 가지 방향(범위: 배칭+connection interval 조정 함께 진행,
  배치 크기: 협상된 MTU에서 자동 산출) 확인 후 진행.
  - **LED index 제거**: `m_ble.c BLE_SAMPLE_LED_INDEX_FIXED`가 순차 LED 스트로빙
    미구현으로 매 샘플 고정값 0만 나가던 죽은 필드였음 — 확인 후 제거.
  - **DATA0/DATA1 v3 배칭 프레임**(`m_ble_proto.h`/`.c`, `BLE_PROTOCOL_VERSION` 2→3):
    `[sample_count(1B)][record×N]`, record=timestamp_us(4B)+seq_num(4B)+Red630/
    Red680/NIR(u16×3)=14바이트. N은 MTU 247(payload 244) 기준 상한 17, 실제 배치
    크기는 `m_ble.c` MTU exchange 콜백이 매 연결마다 계산(협상 실패 시 N=1로 자동
    축소, 별도 레거시 포맷 없이 동일 파서로 처리).
  - **MTU 협상 + LE Data Length Update 우리(peripheral)가 선제 요청**
    (`bt_gatt_exchange_mtu()`/`bt_conn_le_data_len_update()`, `prj.conf`
    `CONFIG_BT_GATT_CLIENT`/`CONFIG_BT_USER_DATA_LEN_UPDATE` 추가) — 컨트롤러는 이미
    251byte까지 지원하도록 기본 설정돼 있었음(`CONFIG_BT_CTLR_DATA_LENGTH_MAX=251` 등,
    빌드 .config로 확인) — 앱/중앙기기가 먼저 요청 안 해도 동작하도록 함.
  - **connection interval/peripheral latency 조정 요청**(`bt_conn_le_param_update()`):
    30ms interval, latency 4(유휴 시 연결 이벤트 4/5 스킵) — architecture.md §2.4가
    원래 열어둔 "S2 선례 범위(7.5~30ms), 실측 후 확정"의 초기값. 배칭만 하고 이 조정을
    안 하면 라디오가 여전히 자주 깨는데 보낼 데이터가 없는 상황이 되어 전력 절감
    효과가 제한적이라는 점을 사용자와 확인 후 함께 적용.
  - **AS7341_SEQ(0x1529) notify 중단**: 배칭 프레임 레코드 자체에 seq_num이 포함돼
    "DATA0/DATA1과 같은 tick에 SEQ도 함께 notify" 개념이 배칭과 안 맞아 호출 제거.
    GATT characteristic 선언은 하위 호환 위해 유지(구독해도 비용 없음, 그냥 안 옴).
  - **재연결 안전성 설계**: 배치 버퍼(`s_batch`, 최대 17샘플)에 샘플이 오래 머무르면
    그 사이 연결이 끊길 때 ring buffer 기반 재연결 안전장치(§11 항목6-[3]) 밖에서
    유실될 위험이 있다 — "배치가 다 찼거나 flush timeout(2초)일 때만 필요한 만큼
    한 번에 pop 후 즉시 notify"하는 구조로 유실 위험 구간을 pop+notify 처리 시간
    (수 ms 이하)으로 최소화했다. 이를 위해 `m_i2c_ring_buffer_count()`(pop 없이 개수만
    확인) 신규 추가.
  - **빌드 검증 완료**: clean pristine rebuild, `version_MCUBOOT: "0.1.19+0"`,
    RAM 92.50%(60,624B, +544B), FLASH 74.10%(+3.7KB, GATT_CLIENT/DLE 코드).
    **하드웨어 검증 미실시** — 실제 배치 크기가 기대대로 산출되는지, connection
    param 조정이 실제로 받아들여지는지(중앙기기가 거부/재협상 가능), 앱이 새
    배칭 프레임을 올바르게 파싱하는지, 전력 절감이 실측되는지(AT-06) 전부 확인 필요.

## v0.1.20 (2026-09-22)

- **[전력 최적화] "한 프레임에 몇 개를 담을지"(MTU 결정)와 "얼마나 자주 무선을 깨워
  보낼지"(전력 목표)를 분리 — 사용자 질문("최소 몇 묶음을 전달하는 게 좋을까? 센싱
  주기에 맞추는 게 좋을까 저전력을 위해 BLE Task wake 주기에 맞추는 게 좋을까?")에
  답하며 발견한 v0.1.19의 빈틈 수정**.
  - v0.1.19까지는 "배치가 다 찼거나 2초 지나면 flush"만 있어서, MTU 협상이 잘 안 돼
    프레임당 1개만 담기는 상황(`s_batch_capacity=1`)에서는 사실상 매 tick(100ms)마다
    즉시 전송돼 배칭 효과가 없었다.
  - **사용자 결정: 무선을 깨우는 최소 간격을 센싱 주기가 아니라 전력 목표 기준으로
    1초로 고정**(`BLE_BATCH_FLUSH_INTERVAL_MS=1000`, `m_ble.c`) — cycle/active
    게이팅 설정이 바뀌어도 무선 송신 빈도(배터리 소모의 핵심 변수)는 항상 일정하게
    유지된다.
  - `m_ble_task_entry()` 루프 재설계: 1초 주기가 될 때까지는 pop하지 않고 ring
    buffer에 쌓아두기만 하다가, 주기가 되면 쌓인 것 전부를 그 자리에서 한 번에
    비운다 — 한 프레임(MTU 기준 최대 17개)보다 많이 쌓였으면 여러 프레임으로 나눠
    같은 "깨어난 시점"에 몰아서 보낸다(무선을 깨우는 횟수 자체는 1회로 유지).
  - 기존 congestion backoff(`-ENOMEM` 처리, v0.1.18)는 이 다중 프레임 전송 루프
    안에서도 그대로 적용.
  - **빌드 검증 완료**: clean pristine rebuild, `version_MCUBOOT: "0.1.20+0"`,
    RAM/FLASH 사용률 변화 없음(로직 재구성, 신규 저장공간 없음). **하드웨어 검증
    미실시** — MTU가 작게 협상되는 조건(구형 폰 등)에서 실제로 1초 간격으로만
    무선이 깨는지, 정상 MTU 조건에서는 기존과 동일하게 동작하는지 확인 필요.
  - TODO(open-item): 1초는 실시간성/배터리 트레이드오프의 초기값 — AT-06 전류소비
    실측 후 조정 가능.

## v0.1.21 (2026-09-22)

- **[정정] `Application/VERSION`이 디스크에서 19로 되돌아가 있어 `config_app.h`의
  `FW_VERSION_PATCH`(20)와 어긋난 상태를 발견 — 이번 패치로 두 값을 21로 다시
  일치시킴.** (memory: OTA 버전 파일 동기화 필요 — 동일 사안 재발)
- **[버그 수정] 부팅 버전 점멸 횟수를 `(FW_VERSION_PATCH+1)`회 → 고정 3회로 변경**
  (사용자 요청). 기존 방식은 패치 버전이 오를수록 점멸 시간(=부팅 블로킹 구간)이
  계속 길어져 watchdog 타이밍과 충돌하는 고위험 버그로 실제 이어졌던 구조(v0.1.13→
  v0.1.14)라, 고정 횟수로 바꿔 이 클래스의 버그 자체를 근본적으로 제거함. OTA 적용
  여부 확인은 이제 점멸 횟수 대신 BLE `AS7341_VERSION` characteristic(0x1528)으로
  `fw_version`을 읽는 방식으로 대체(앱이 이미 지원) — **점멸만으로 버전 구분은
  더 이상 안 됨, 육안 확인은 "부팅이 여기까지 왔다"는 생존 신호 용도로만 남음.**
  `config_app.h`(`FW_VERSION_BOOT_BLINK_COUNT=3`), `m_i2c.c` 루프 수정.
- **[신규, 하드웨어 진단용] `main()`에 LED1(640nm)/LED2(680nm) 강제 점등 추가** —
  사용자 보고: "SW1을 눌러도 한 번에 켜지지 않는 보드가 발생, 전원이 안 들어가는
  건지 초기화 단계 에러인지 구분이 안 됨." `main.c`는 커널이 스케줄링을 시작하면
  어떤 태스크(m_i2c/m_ble/m_ctrl) init 성공 여부와 무관하게 반드시 도달하므로,
  여기서 직접 `m_i2c_led_init()`+`m_i2c_led_set_duty()`를 호출해 LED를 켜면
  "적어도 전원+부팅까지는 성공"을 무조건 확인할 수 있다(태스크 init이 멈추면
  기존 LED 시퀀스는 아예 안 나와서 두 경우를 구분 못 했음).
  - **아키텍처 원칙의 의도적 예외**: `agents.md`/`architecture.md` §6("main()은
    태스크를 생성/초기화하지 않는다") 및 `m_i2c_led.h`("I2C Task 컨텍스트에서만
    호출")를 하드웨어 진단 목적으로 명시적으로 벗어난다. main() 시점엔 아직 어떤
    태스크도 AS7341 측정을 하지 않으므로 실측 정확도와는 충돌하지 않으며, m_i2c
    태스크가 정상 기동하면 자신의 LED 시퀀스로 자연히 덮어쓴다. `main.c`/
    `m_i2c_led.h`에 예외 사유 명시.
- **[정책] BLE 디바이스 이름을 `nRF_fNIRS_Sys_v2`(고정) → `TedNeuro_v0.1.21`(버전
  포함)로 변경**(사용자 요청) — BLE 스캔/연결 시 앱이 디바이스 이름만 보고도
  펌웨어 버전을 바로 구분할 수 있게 함. `prj.conf CONFIG_BT_DEVICE_NAME`은 Kconfig
  문자열이라 C 매크로 자동 대입이 안 돼, **앞으로 매 패치 버전 상승마다 VERSION
  파일/`config_app.h`와 함께 수동으로 갱신하는 정책**으로 확정(주석에 명시).
- **빌드 검증 완료**: clean pristine rebuild, `version_MCUBOOT: "0.1.21+0"`,
  `.config`에서 `CONFIG_BT_DEVICE_NAME="TedNeuro_v0.1.21"` 반영 확인. RAM
  92.50%(변화 없음), FLASH +204B.
- **2026-09-22 실기 검증 완료(BLE 데이터 수집 경로)**: v0.1.21을 실물 보드에 플래시,
  앱과 연동해 85.5초간 raw 데이터 수집(`2026-09-22_10-36-38_fNIRS_RAW_DATA.csv`,
  1730 레코드) — seq_num 865개 전부 연속(gap 0), device_timestamp_us 간격 865개
  전부 정확히 100,000us(표준편차 0), v0.1.20 배칭 설계(10Hz×1초)와 실측 배치 크기
  일치. v0.1.13에서 낮춘 적분시간(19.7ms)의 노이즈 우려도 해소(전 채널 포화/저신호
  없음). v0.1.13→v0.1.14 재발 방지용 부팅 점멸 고정 3회 변경도 이 세션에서 정상
  부팅 확인. 상세는 architecture.md §2.4/§11 항목5 참고. **여전히 미검증**: 연결
  끊김이 없어 재연결/22초 backlog/`-ENOMEM` 경로, LED1/LED2 강제 점등의 실제 진단
  효용, 새 디바이스 이름 스캔 노출.

## v0.1.22 (2026-09-22)

- **[버그 수정] AS7341 측정을 LED on 구간에 맞춰 매 샘플마다 명시적으로 재트리거**
  (`m_i2c_as7341.c`) — 오늘 레퍼런스 raw 데이터와 비교 분석하다가 발견: LED 듀티를
  50%→75~100%까지 크게 올려도 raw 채널 비율이 거의 안 움직였고, 완전 암실에서
  LED만 켜고 측정했더니 대부분 1~2에 불과하고 **정확히 53샘플(sensor1)/139샘플
  (sensor0) 주기로만 큰 값이 튀는 beat(맥놀이) 패턴**이 실측으로 확인됨
  (`2026-09-22_15-19-34_fNIRS_RAW_DATA.csv`, `docs/test/`).
  - **원인**: v0.1.12에서 SP_EN(스펙트럴 엔진)을 init 1회만 켜고 이후 절대
    재기록하지 않는 free-running 방식으로 바꿨다(`_ref_fnirs_example` 레퍼런스와
    동일 방식이라는 근거). 그런데 그 레퍼런스는 **LED가 상시 켜져 있는 구조**라
    free-running이어도 항상 LED와 적분 구간이 겹치지만, 우리 프로젝트는
    `m_i2c.c acquire_one_sample()`이 **매 샘플마다 LED를 켰다/끄는 구조**
    (architecture.md §5 "연속점등 금지")다. 두 독립적인 주기(LED on/off tick vs
    센서의 free-running 적분 주기)가 서로 비동기로 돌면서 위상이 주기적으로만
    맞는 beat 패턴이 발생 — 위상이 맞을 때만 실제 LED 신호가 잡히고 나머지는
    거의 0에 가까운 값(ambient/누설광 수준)이 찍히고 있었다.
  - **수정**: `m_i2c_as7341_read_raw()`가 호출될 때마다 ENABLE 레지스터를
    `PON`만 → `PON|SP_EN`으로 재기록해 새 적분 사이클을 명시적으로 시작한다
    (datasheet §10.2, SP_EN 재기록 시 스펙트럴 엔진 재시작). `m_i2c.c`가 LED를
    켠 "다음"에 이 함수를 호출하므로, 이렇게 하면 읽는 적분 구간이 항상 LED on
    구간 시작 이후로 보장된다. `m_i2c_as7341_init()`은 이제 SP_EN을 켜지 않고
    PON만 걸어둔다(트리거는 매 read마다 read_raw()가 담당).
  - v0.1.12의 "매 tick ENABLE 재기록은 불필요한 중복이라 버그"라는 판단은
    레퍼런스(상시점등)에는 맞았지만 우리 아키텍처(LED 온/오프 duty)에는 맞지
    않았던 것으로 정정 — 결과적으로 v0.0.4~v0.1.11 시절의 "매 read마다 재기록"
    방식이 우리 구조에는 옳았고, v0.1.12~v0.1.21 구간의 free-running이 이번에
    발견된 beat 패턴 버그를 만들고 있었다(이 구간 동안의 raw 데이터 절대값은
    LED-동기화가 깨진 상태에서 수집된 것으로 재해석 필요, architecture.md §11
    항목5 참고).
  - **빌드 검증 완료**(`west build -b nrf52dk/nrf52832 Application -p always`,
    `APPVERSION=0.1.22`, 에러 없음). **하드웨어 검증 미실시** — 다음 세션에서
    실물 보드로 (1) 완전 암실 beat 패턴이 사라지는지, (2) 듀티 변경이 이제
    채널 비율에 정상적으로 반영되는지 재검증 필요.
- BLE 디바이스 이름 `TedNeuro_v0.1.22`로 갱신(정책, VERSION/config_app.h와 동기화).
- **2026-09-22 실기 검증 완료(beat 패턴 해소)**: 완전 암실에서 재측정 결과 0인
  행/스파이크 없이 전 채널 변동률 1% 미만으로 안정 — beat 패턴 해소 확인. 실제
  부착 재측정(2회)에서도 채널비율이 레퍼런스 방향으로 재현성 있게 개선(d1
  red680/red630 0.71→1.5대, d2 1.39→2.5~2.8대). **새 회귀 발견**: 앱이 device
  timestamp를 CSV에 직접 기록하도록 개선한 뒤 보니, 샘플 간격이 설정된 Cycle
  300ms가 아니라 **일관되게 400ms**로 나옴(v0.1.23에서 원인 규명 및 수정, 아래).

## v0.1.23 (2026-09-22)

- **[버그 수정] AS7341 NIR1/NIR2 트리거를 먼저 둘 다 걸고, 그 다음에 순서대로
  읽도록 변경** (`m_i2c_as7341.c`, `m_i2c_as7341.h`, `m_i2c.c`) — v0.1.22에서
  측정 주기가 의도한 300ms가 아니라 400ms로 밀리는 회귀가 실기로 확인됨(device
  timestamp 기준 105개 샘플 중 104개가 정확히 0.4s 간격).
  - **원인**: v0.1.22의 `m_i2c_as7341_read_raw()`는 트리거(SP_EN 재기록)+적분
    대기+읽기를 한 함수에서 다 처리했다. `m_i2c.c acquire_one_sample()`이 NIR1,
    NIR2를 **순차로** 이 함수를 호출하다 보니, NIR1의 적분 대기시간(최대
    `AS7341_MEASURE_TIMEOUT_MS`)이 끝나야 NIR2 트리거가 걸리고, NIR2도 또
    자기 적분을 처음부터 기다려야 했다 — 두 센서의 적분 대기가 더해져 한 샘플을
    만드는 데 걸리는 시간이 늘어나 RTC tick 기준 300ms 주기를 못 맞추고 400ms로
    밀린 것.
  - **수정**: `m_i2c_as7341_read_raw()`에서 트리거 부분을 분리해
    `m_i2c_as7341_trigger_measurement()`로 뽑아냈다. `acquire_one_sample()`이
    이제 NIR1/NIR2를 **먼저 둘 다 트리거**(적분이 동시에 시작)한 뒤, **그 다음에
    순서대로 읽는다**(`m_i2c_as7341_read_raw()`, 이제 트리거 없이 AVALID 폴링+
    읽기만 함) — 두 센서의 적분 대기시간이 겹쳐 진행되므로 추가 지연이 대략
    절반으로 줄어든다. LED-적분 동기화(v0.1.22의 핵심 수정)는 그대로 유지됨
    (트리거는 여전히 LED를 켠 "다음"에 걸림).
  - **빌드 검증 완료**: `west build -b nrf52dk/nrf52832 Application -p always`,
    `APPVERSION=0.1.23`, 에러 없음. **하드웨어 검증 미실시** — 실물 보드로 (1)
    샘플 간격이 300ms로 복귀하는지, (2) beat 패턴이 재발하지 않는지(트리거 시점이
    바뀌었으므로 회귀 가능성 재확인 필요) 검증해야 한다.
- BLE 디바이스 이름 `TedNeuro_v0.1.23`으로 갱신.
- **2026-09-23 실기 검증 완료**: 약 78분(4676초) 연속 수집 세션에서 device timestamp
  기준 15,586/15,588 샘플이 정확히 300ms 간격(표준편차 0)으로 확인 — beat 패턴 재발
  없음. 세션 중간(약 71.5분 지점) 원인 불명의 기기 재부팅 1회 발생했으나 재연결이
  약 1초 만에 이뤄져 데이터 끊김/이상값 없이 이어짐(재부팅 원인 별도 확인 필요).

## 2026-09-23 — 문서 정정 (코드 동작 변경 없음)

- `m_ble_proto.h`의 "cycle period/active window가 CONFIG에 저장만 되고 실제 RTC
  샘플링 주기에는 반영되지 않는다"는 TODO(open-item) 주석이 **오래돼 사실과 다름**을
  발견해 정정. 실제로는 v0.1.5(2026-09-18)에서 이미 tick 기반 게이팅(`m_i2c.c
  is_gate_active_tick()`)으로 구현됐고 v0.1.6(2026-09-18)에서 실기 검증까지 끝난
  사항이었다(당시 CHANGELOG에 기록돼 있었으나 이 헤더 주석만 갱신이 안 됨).
  Integration time 반영도 코드 확인 + 2026-09-22/23 실기 테스트로 재확인.
  **실기 최종 재검증(2026-09-23)**: 앱에서 LED 듀티/Integration time을 변경하며
  실시간 그래프로 확인 — 설정 적용 시점에 정확히 계단식으로 반응, Integration
  5→1(20ms 단위, 100ms→19.7ms, 5배 차이) 변경 시 raw count가 약 5.2배 감소해
  데이터시트 공식(tint 및 full-scale이 (ATIME+1)×(ASTEP+1)에 비례)과도 일치함을
  확인. **여전히 미반영인 항목은 location interval(offset 1)뿐** — CONFIG
  read-back만 되고 실제 동작에는 반영 안 됨(그대로 TODO 유지).

