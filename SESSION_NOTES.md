# 세션 노트 (새 세션 컨텍스트 인수인계용)

> 이 문서는 **이 저장소에서만** 의미가 있는 세션별 작업 기록이다. 새 세션이 시작될 때
> `CLAUDE.md`/`architecture.md`와 함께 이 문서의 최신 항목을 먼저 읽으면, 지난 세션에서
> 무엇을 했고 무엇이 남았는지 CHANGELOG.md를 처음부터 훑지 않고도 빠르게 파악할 수 있다.
> 매 세션 끝에 새 날짜 섹션을 위에 추가한다(최신이 맨 위).

---

## 2026-09-22 (펌웨어 v0.1.19 → v0.1.20, 진행 중)

### 오늘 다룬 것
어제(9/21) 커밋 정리 + 오늘 업무 일지 "오늘 할일" 작성 + 트래커 반영 + BLE 배칭 전력
최적화 후속 설계.

### 완료
- **git 커밋**: 어제(9/21) 작업 v0.1.12~v0.1.19 전체를 하나의 커밋으로 정리
  (`22bc65e v0.1.12~v0.1.19 (2026-09-21)`, 15개 파일). push는 안 함(요청 없으면
  안 함).
- **일일 업무 일지 "오늘 할일" 작성**: `일일 업무 일지_2609(4W_2D)_Wade HAN_작성중.docx`
  (Downloads) — 어제 저녁에 잡아둔 내일(오늘) 계획을 그대로 반영: (1) v0.1.14~v0.1.19
  실물 보드 최종 검증, (2) raw 데이터 비교 분석 착수, (3) AS7341 정밀 보정 계획 수립.
- **트래커(`fw_tracker.html`) 반영 시도**: 로컬에 소스 저장소가 없어(별도 GitHub
  저장소 `fNIRS-fw-plan-tracker`, 이 세션 어디에도 클론 안 돼 있음) 직접 수정은
  불가능 — 라이브 페이지를 읽고 `TASKS`(R2-1/R2-2/R3-1) + `RISKS`(A3) 교체용 코드
  스니펫을 채팅으로 전달, 사용자가 외부에서 직접 패치 진행하기로 함.
- **v0.1.20, BLE 배칭 flush 주기 재설계**: 사용자 질문("최소 몇 묶음을 보내는 게
  좋을까 — 센싱 주기 기준? 저전력 BLE wake 주기 기준?")에 답하며 v0.1.19의 빈틈 발견
  — MTU 협상이 잘 안 돼 프레임당 1개만 담기면(`s_batch_capacity=1`) 사실상 매
  tick(100ms)마다 즉시 전송되어 배칭 효과가 전혀 없었음.
  - **사용자 결정: 무선 wake 최소 간격을 센싱 주기가 아니라 전력 목표 기준 1초로
    고정**(`BLE_BATCH_FLUSH_INTERVAL_MS=1000`) — "몇 개 담을지"(MTU가 결정)와
    "얼마나 자주 깨울지"(우리가 통제)를 분리, cycle/active 게이팅 설정이 바뀌어도
    무선 송신 빈도는 항상 일정하게 유지되도록 함.
  - `m_ble_task_entry()` 루프 재설계: 1초 주기 전에는 pop 안 하고 ring buffer에
    쌓아두다가, 주기가 되면 전부 한 번에 비움(여러 프레임 필요하면 같은 wake
    시점에 몰아서 전송).
  - **빌드 검증 완료**(`version_MCUBOOT: "0.1.20+0"`). **하드웨어 검증 미실시** —
    MTU 작게 협상되는 조건에서 실제로 1초 간격만 무선이 깨는지 확인 필요.

### 앱 핸드오프 문서 작성 (BLE 프로토콜 변경사항)
사용자 요청으로 v0.1.18~v0.1.20의 BLE 프로토콜 변경사항(배칭 프레임 포맷, MTU 기반
가변 sample_count, 1초 flush 주기, SEQ notify 중단, LED index 제거)을 앱 개발
외부 세션에 전달할 컨텍스트로 정리해서 채팅으로 전달함(파일로 남기지 않음 — 필요시
이 대화 로그 참고).

### 완료 (v0.1.21) — VERSION 동기화 정정 + 부팅 진단 개선 + BLE 이름 정책
- **`Application/VERSION`(19)과 `config_app.h`(20) 불일치 발견/정정** — 외부에서
  VERSION 파일이 되돌아간 것으로 추정, 원인 불명. 21로 재동기화.
- **부팅 버전 점멸 횟수 `(FW_VERSION_PATCH+1)`회 → 고정 3회로 변경**(사용자 요청).
  패치가 오를수록 점멸시간이 길어져 watchdog과 충돌하던 버그 클래스(v0.1.13→14)를
  근본 제거. **부수 효과**: 점멸 횟수로 OTA 버전 확인하던 방법은 이제 안 됨 —
  `AS7341_VERSION`(0x1528) BLE 읽기로 대체.
- **`main()`에 LED1/LED2 진단용 강제 점등 추가**(사용자 요청) — "SW1 눌러도 안
  켜지는 보드 발생, 전원 문제인지 초기화 문제인지 구분 안 됨" 보고에 대응.
  `agents.md`/`m_i2c_led.h`의 "main()은 초기화 안 함"/"I2C Task만 LED 호출" 원칙의
  **의도적 예외** — main.c/m_i2c_led.h에 사유 명시.
- **BLE 디바이스 이름 `nRF_fNIRS_Sys_v2` → `TedNeuro_v0.1.21`로 변경**(사용자 요청,
  버전 포함 형식) — 앱이 스캔/연결 시 이름만으로 버전 구분 가능. **정책 확정**:
  매 패치 버전 상승마다 VERSION/config_app.h와 함께 수동 갱신(Kconfig 문자열이라
  자동화 안 됨).
- **빌드 검증 완료**(`version_MCUBOOT: "0.1.21+0"`, `.config`에서 새 디바이스 이름
  반영 확인). **하드웨어 검증 미실시** — LED1/LED2 강제 점등이 실제 문제 보드
  진단에 도움되는지, 새 디바이스 이름이 스캔 시 정상 노출되는지 확인 필요.

### 🎉 첫 실기 검증 완료 (v0.1.21, 2026-09-22) — v0.1.13 이후 처음
사용자가 v0.1.21을 실물 보드에 플래시하고 (업데이트된) 앱과 연동해서 85.5초간 raw
데이터를 수집(`2026-09-22_10-36-38_fNIRS_RAW_DATA.csv`, 1730 레코드). 분석 결과:
- **seq_num 865개 전부 연속(gap 0)** — 이 세션 내내 데이터 유실 없음.
- **device_timestamp_us 간격 865개 전부 정확히 100,000us, 표준편차 0** — RTC 10Hz
  타이밍 완벽 안정.
- **배칭(v0.1.19/20) 실기 확인**: 같은 센서 레코드가 연속 10개(가끔 11개) 단위로
  도착 — 10Hz×1초 flush 설계와 정확히 일치. 이번 연결의 MTU 협상 결과가 목표(247)
  보다 작아(추정 143~156byte) 상한 17이 아니라 10~11개/프레임으로 나온 것으로 추정
  (오류 아님).
- **신호 품질 확인**: Red630/Red680/NIR 전 채널 포화(65535)·저신호(<10) 없음(NIR
  1건만 경계값 6) — v0.1.13에서 19.7ms로 낮춘 적분시간이 노이즈 문제를 일으키지
  않는다는, 그동안 미검증이던 우려가 해소됨.
- **미검증으로 남은 것**: 이 세션 동안 연결 끊김이 없어서 재연결/22초 backlog/
  `-ENOMEM` 안전상태 경로는 그대로 미검증 — **사용자 결정으로 재연결 시나리오
  테스트는 추후 진행**.
- 상세는 architecture.md §2.4(flush 주기)/§11 항목5(AS7341 캘리브레이션) 참고.

### 레퍼런스 데이터 비교 검증 — 진행 승인
사용자 질문("이제는 레퍼런스와 비교 검증 시험을 진행해도 괜찮을까?")에 답변: 신호
품질(노이즈 없음)/타이밍/배칭 전부 실기로 확인됐으므로 **레퍼런스 대비 raw 데이터
비교는 진행해도 좋다고 판단** — 단, gain/ATIME 자체의 정량 캘리브레이션은
architecture.md §11 항목5에 여전히 미해소 상태로 남아있어, 이번 비교는 "정성적
동등성 확인" 수준으로 프레이밍하는 게 맞다고 안내함(정량 절대값 캘리브레이션은
별개 후속 작업).

### 오후 — 레퍼런스 비교 분석 중 LED-센서 동기화 버그 발견 및 수정 (v0.1.22)
`2026-09-22_10-36-38_fNIRS_RAW_DATA.csv`를 레퍼런스(`fnirs_nRF_fNIRS_System_3_
20260812_033228.csv`)와 채널 단위(red630/680/nir)로 비교하다가 채널 간 상대비율이
레퍼런스와 크게 다름을 발견(레퍼런스 red680/nir이 red630보다 6~8배인데 우리는
0.4~1.4배). 원인 조사 과정:
1. **SMUX 채널 매핑 재검증** — `_ref_fnirs_example/fNIRS/src/as7341.c`의 레지스터
   단위 라우팅 주석과 20바이트 완전 일치 확인 → 원인에서 배제.
2. **LED 듀티 실험**(LED2 50→75%, LED3 75→100%) — 채널비율이 거의 안 움직임 →
   "듀티를 크게 바꿔도 효과 없음" 자체가 이상 신호.
3. **완전 암실 테스트** — LED만 켜고 측정했는데 대부분 1~2, 그런데 정확히
   53샘플(sensor1)/139샘플(sensor0) 주기로만 값이 크게 튐(beat/맥놀이 패턴) 발견.
4. **원인 확정**: v0.1.12에서 AS7341을 free-running(SP_EN 1회만, 재기록 안 함)으로
   바꾼 게 문제 — 그 근거였던 `_ref_fnirs_example`은 LED가 상시 켜진 구조라
   free-running이어도 되지만, 우리는 매 샘플마다 LED를 켰다/끄는 구조(architecture.md
   §5)라 LED on/off 주기와 센서의 free-running 적분 주기가 서로 비동기로 어긋나고
   있었음.
5. **수정(v0.1.22)**: `m_i2c_as7341_read_raw()`가 매 호출마다 SP_EN을 0→1 재기록해
   새 적분 사이클을 명시적으로 시작(LED가 켜진 "다음"에 호출되므로 동기화 보장).
   `west build` 빌드 검증 완료.
6. **실기 재검증**: 같은 완전 암실 조건에서 v0.1.22 플래시 후 재측정 → beat 패턴
   완전히 사라지고 전 채널 변동률 1% 미만으로 안정 → **버그 해소 확인**.
7. **실제 부착 재측정 2회**(15:58, 16:41) — 채널비율이 레퍼런스 방향으로 재현성
   있게 개선(d1 red680/630 0.71→1.55, d2 1.39→2.51 등). 레퍼런스는 **수면 중** 데이터라
   활동 중 데이터와 절대 일치를 목표할 필요는 없음(사용자 지적, architecture.md에 반영).
- **오후에 있었던 오진 1건, 사용자가 정정**: 이 재측정 CSV들이 겉보기 ~1Hz로 보여서
  "앱이 1Hz로 다운샘플 로깅한다"고 잘못 진단했었음 — 실제로는 **Cycle 300ms/Active
  100ms 게이팅으로 초당 3번 측정 + BLE 패킷 배칭으로 한 번에 2~3개씩 몰려 들어오는데,
  앱의 CSV 로거가 기기 자체 타임스탬프가 아니라 수신 시각(wall-clock)을 찍고 있어서
  겉보기 간격이 뭉쳐 보인 것**. 펌웨어 버그가 아니라 앱 CSV 로깅 방식의 문제 —
  사용자가 앱에서 device timestamp를 별도로 기록하도록 수정하기로 함(펌웨어 측
  조치 불필요).
- architecture.md §11 항목5에 1~7 전체 경과 상세 기록됨.

### 다음 세션에서 먼저 할 일
1. **재연결 시나리오 실기 검증**(사용자가 "나중에 하겠다"고 미룸) — 22초 grace
   period/backlog flush/`-ENOMEM` 안전상태 경로, v0.1.21의 LED1/LED2 진단 점등이
   "SW1 눌러도 안 켜지는" 보드에서 실제로 도움되는지, 새 디바이스 이름
   (`TedNeuro_v0.1.22`) 스캔 노출 확인.
2. **v0.1.22 하드웨어 검증 마무리**: 앱이 device timestamp를 CSV에 직접 기록하도록
   수정된 뒤, 실제 10Hz(또는 Cycle 300/Active 100 기준 3.3Hz) 원본 간격으로 맥동
   파형 분석 재시도.
3. AS7341 정밀 캘리브레이션(광량계/반사표준 기반 절대 보정) — 채널비율 문제는
   해소 방향이나 정량 보정은 여전히 별도 과제로 남음.
4. 오늘 건드린 파일(`Application/src/m_ble.c`, `Application/src/m_i2c.c`,
   `Application/src/m_i2c_as7341.c`, `Application/main.c`,
   `Application/include/config_app.h`, `Application/include/m_i2c_led.h`,
   `Application/prj.conf`, `Application/VERSION`, `CHANGELOG.md`, `architecture.md`)
   + 이 SESSION_NOTES.md 갱신분을 저녁에 한 번에 커밋(사용자 요청, "오늘 작업
   마무리할 때 한 번에 처리").

---

## 2026-09-21 (펌웨어 v0.1.11 → v0.1.19, 오늘 업무 종료)

### 오늘 한눈에 보기 (TL;DR — 내일 세션 시작 시 이것부터)
- **최종 버전 v0.1.19**, 빌드 검증까지 완료. **아직 git 커밋 안 됨** — 15개 파일,
  956줄 추가/137줄 삭제(v0.1.11~v0.1.19 전체). 다음 세션 시작 전에 커밋 여부 확인 필요.
- **하드웨어 검증이 하나도 안 된 상태로 하루가 끝남** — 이 세션 내내 west 툴체인은
  있었지만 실물 보드가 없어서 빌드만 계속 검증했다. **내일 최우선 순위는 실물 보드로
  v0.1.14~v0.1.19를 한 번에 몰아서 검증**하는 것(아래 "다음 세션에서 먼저 할 일" 참고).
- 오늘 만든 산출물: 일일 업무 일지(`일일 업무 일지_2609(4W_1D)_Wade HAN_작성완료.docx`),
  간트차트(`Wis Medical_Tedream_S2_V2_Gantt_V2 (1)_작성완료.xlsx`, fNIRS Project 시트
  9/21 열) — 둘 다 Downloads 폴더, 원본 파일은 보존.
- 주간 마일스톤(BLE 안정성/Raw 데이터 검증/AS7341 보정, 마감 9/23) 중 오늘은 BLE
  안정성 쪽에 집중, Raw 데이터는 수집만 완료(비교분석은 미착수), AS7341 보정은 미착수.

### 오늘 다룬 것
지난 주(9/14~9/18) 작업 리뷰 + 이번 주(연휴로 근무일 3일) 업무 범위 확정, `_ref_fnirs_example`
(같은 AS7341 3파장 F7/F8/NIR 구성을 실사용 검증한 nRF5 SDK 레퍼런스)과 LED 동작/측정 방식 대조.

### 완료
- **[버그 수정] AS7341 SP_EN 매 tick 재기록 → init 1회로 정정** (v0.1.12,
  `m_i2c_as7341.c`) — 레퍼런스(`_ref_fnirs_example/fNIRS/src/as7341.c`)는 SMUX 구성
  직후 SP_EN을 1회만 켜고 이후 STATUS2.AVALID 폴링+읽기만 함(free-running SPM).
  우리 `read_raw()`는 매 tick마다 ENABLE을 재기록하고 있었음 — architecture.md §11
  항목5 관련 미해결 우려사항(CHANGELOG v0.0.10)과 직결. init에서 1회 활성화, read_raw()
  재기록 제거로 정정. 빌드/실기 검증은 이 세션 환경에 west/NCS 툴체인 없어 미실시.
- **[대조 확인] SMUX RAM 20바이트 값은 레퍼런스와 완전 일치** — 별도 크로스체크 근거 확보.
- **[정책, 코드 변경 없음] LED 구동 방식 차이는 하드웨어 토폴로지 차이로 확인, 패치 대상
  아님** — 레퍼런스는 RED/IR 2채널×2위치 상시점등, 이 프로젝트는 640/680/950nm 3파장×
  1위치 + 테스트 APK 프로토콜 기반 duty/cycle-active 게이팅(기존 구조 유지).

### 추가 완료 (v0.1.13)
- **[정책 반영] 적분시간 파라미터화를 레퍼런스와 동일 방식으로 정정** — ASTEP 고정+ATIME
  가변 → ATIME=99 고정(`AS7341_ATIME_FIXED`)+ASTEP 가변으로 변경(`m_i2c_as7341.c/.h`).
  `m_i2c_as7341_set_integration_time()` 파라미터 의미가 "integration_20ms_units"로 바뀜.
  기본 적분시간도 레퍼런스 기본값과 동일하게 83.4ms→**19.7ms**로 변경(`m_i2c.c`) —
  **신호가 노이즈 수준으로 떨어지지 않는지 다음 실기 검증 필요**.
- **빌드 검증 완료**: `west build -b nrf52dk/nrf52832 Application -p always`(VERSION 파일
  갱신 반영 위해 pristine 필요) clean build 성공, FLASH 72.34%/RAM 87.87%,
  `dfu_application.zip` manifest `version_MCUBOOT: "0.1.13+0"` 확인.
  **하드웨어 검증 미실시**(이 세션에 실물 보드 없음).

### 추가 완료 (v0.1.14, 고위험 Watchdog/Reset 버그 수정)
- **사용자가 "v0.1.13 플래시 후 보드가 리셋되는 것 같다, 버전 점멸 LED 때문에 watchdog
  걸리는 게 아닌지" 의심 → 조사 결과 정확히 그 원인으로 확인.** CTRL 태스크가 부팅 즉시
  watchdog을 무장(4000ms 타임아웃)하는데, I2C 태스크의 첫 alive 신호는 버전 점멸 블로킹
  루프(`(PATCH+1)*300ms`)가 끝나야 발생 — v0.1.13(PATCH=13) 기준 4200ms로 타임아웃 초과.
  BLE 태스크도 I2C 초기화를 `K_FOREVER`로 대기하는 동안 자기 alive 신호가 함께 지연돼
  두 소스가 동시에 stale해지는 구조였음. `FW_VERSION_PATCH`가 매 패치 +1되는 정책상
  이전부터 존재하던 시한폭탄이 이번 버전에서 실제로 터진 것(이번 패치 자체가 원인이
  아니라, 버전이 누적되며 필연적으로 발생할 문제였음).
  **수정**: `m_i2c.c` 점멸 루프/AS7341 init 후 `m_ctrl_notify_alive(CTRL_ALIVE_I2C)` 추가,
  `m_ble.c`의 `K_FOREVER` 대기를 `K_MSEC(200)` 폴링+alive 보고로 변경. 실제 hang 감지
  능력은 그대로 유지(정상 대기 구간에서만 더 자주 보고).
- **빌드 검증 완료**(clean pristine rebuild, `version_MCUBOOT: "0.1.14+0"`). **하드웨어
  검증 필요** — 이 세션엔 실물 보드 없음.

### 추가 완료 (v0.1.15→v0.1.16, BLE 페어링/본딩 활성화 후 같은 날 롤백)
- 사용자가 "앱과 자동 연결 가능하도록 본딩 로직 구현, 펌웨어 먼저 구현" 요청 → BLE
  페어링/본딩 활성화(v0.1.15, `prj.conf`/`m_ble.c`/`m_ble_gatt.c`), 빌드 검증 완료
  (`version_MCUBOOT: "0.1.15+0"`)까지 진행.
- 이후 사용자 재확인: **"본딩 로직은 롤백해줘, 아직은 필요 없을 것 같다 — 앱과 연동이
  끊어진 후 디바이스가 다시 advertising으로 전환되서 재연결만 되면 충분하다"** → 즉시
  전면 롤백(v0.1.16). 그 재연결 동작(끊기면 advertising 재개)은 본딩과 무관하게 기존
  구조(`m_ble.c on_disconnected()`, 2026-09-16 검증 완료)가 이미 담당하고 있어서 별도
  구현이 필요 없었음.
- v0.1.15는 이 세션에서 커밋되지도 실물 보드에 플래시되지도 않아 기존 앱 연동
  (v0.0.12)에 실질적 영향 없음. **교훈**: BLE 페어링/본딩 관련 요청이 다시 나오면
  architecture.md §11 항목6-[5]의 활성화 조건(테스트 APK 본딩 지원 확인)이 여전히
  유효하다는 점부터 먼저 확인할 것 — 재연결만 필요한 것인지 본딩까지 필요한 것인지
  구분해서 물어볼 것.
- 롤백 후 빌드 재검증 완료(`version_MCUBOOT: "0.1.16+0"`).
- 이 롤백 이후 사용자에게 현재 재연동(reconnect) 구조를 앱 개발 세션에 전달할 문서로
  정리해서 리뷰 요청 — advertising 재개 2경로(부팅 시/`on_disconnected()` 런타임 시),
  30초 grace period, STANDBY 전환/복귀, notify 구독 필요성, 앱 쪽 자동 재연결 필요성
  (architecture.md §11 항목7)을 정리해 전달함.

### BLE 재연결 앱 쪽 검증 (2026-09-21, architecture.md §11 항목7 부분 해결)
- 사용자가 앱에 "disconnect 이벤트 감지 시 자동 reconnecting 대기" 로직을 구현하고
  디바이스 전원을 꺼서 강제로 연결을 끊은 뒤 다시 켜서 앱이 자동 재연동하는 것을 확인.
- **검증 범위 한정 짚어줌**: 이 테스트는 펌웨어의 두 advertising 재시작 경로 중
  **부팅 시 경로**(`ble_init()`)만 검증한 것 — 디바이스가 켜진 채로 BLE 링크만 끊기는
  **런타임 경로**(`on_disconnected()` → `s_adv_restart_work`, 전파 도달거리 이탈/앱
  백그라운드 전환 등 실사용에서 더 흔한 케이스)는 아직 앱 쪽에서 미검증 — 휴대폰 BT
  토글 등으로 디바이스 전원은 그대로 두고 BLE만 끊어서 추가 검증 권장.

### 추가 완료 (v0.1.17, ring buffer/grace period 용량 불일치 버그 수정)
- 사용자 요청: "연결이 끊겼을 때 센싱 데이터 ring buffer 저장 로직 구현, 구현 전 논리적
  검토 먼저" — 검토 결과 **그 로직 자체는 v0.0.14부터 이미 구현돼 있었으나**,
  `RING_BUFFER_CAPACITY`(150샘플=15초)가 `BLE_DISCONNECT_STANDBY_TIMEOUT_TICKS`
  (당시 300 tick=30초)보다 작아서 30초 끊김 시 15초 지점부터 데이터가 실제로 유실되는
  **버그**를 발견(architecture.md §2.5 vs §11 항목6-[3] 설계가 서로 검토 없이 따로
  정해진 결과).
- **1차 시도(300 그대로 유지, `RING_BUFFER_CAPACITY`만 300으로 증량)를 빌드로 실측 후
  되돌림**: Application 이미지 RAM 사용률이 87.8%(기존)→96.07%로 치솟아 여유(2.5KB)가
  너무 부족하다고 판단, 사용자에게 방향 확인 요청.
- **사용자 결정: "grace period를 버퍼가 감당 가능한 수준으로 단축"** — `BLE_DISCONNECT_
  STANDBY_TIMEOUT_TICKS` 300→**220 tick(22초)**로 하향, `RING_BUFFER_CAPACITY`는 이
  값을 직접 참조하도록 코드로 묶어서 재발 방지. 결과 RAM 사용률 **91.67%**(60,080/
  65,536B)로 안전 마진 확보.
- **빌드 검증 완료**(`version_MCUBOOT: "0.1.17+0"`). **하드웨어 검증 미실시** — 22초
  끊김/재연결 시 데이터 gap 없는지, 22초 초과 시 STANDBY 정상 전환되는지 실기 확인 필요.

### 블루투스 토글 런타임 재연결 검증 완료 (2026-09-21)
- 사용자가 휴대폰 BT 토글로 디바이스는 켜둔 채 BLE 링크만 끊는 시나리오도 확인 —
  architecture.md §11 항목7이 이제 완전 해소(부팅 경로/런타임 경로 둘 다 검증).

### 추가 완료 (v0.1.18, DATA0/DATA1 프레임 확장 + 안전상태 오래치 버그 수정)
- 사용자 질문("재연결 후 backlog 전송하려면 앱에 받는 로직이 필요한가?")에 답하는
  과정에서 코드를 추적하다가 **더 심각한 버그를 발견**: `-ENOMEM`(BLE notify 혼잡, backlog
  flush 시 재현 가능성 높음)을 `m_ble.c`가 진짜 통신 장애로 오분류해 `CTRL_STATE_
  DEGRADED` 안전상태 래치(재부팅 전까지 자동복구 없음) → 정상적인 backlog flush 상황에서
  측정이 영구 정지될 뻔한 회귀. 사용자에게 보고 후 수정 방향 확인.
- 사용자 요청: "DATA0/DATA1 프레임에 timestamp/seq 넘버 추가, NCS_TedreamS2 데이터
  프레임 분석해서 적용 + 추천 수정안(ENOMEM 재분류) 반영 + 영구 정지 방지 로직 추가."
- **NCS_TedreamS2 분석**(`C:\Wade\00_project\NCS_TedreamS2.zip`, `Application/src/a/
  m_a_ble.c`): 센서 페이로드를 `[timestamp 4B LE][센서 데이터]` 순서로 구성하는 관례
  확인(architecture.md §2.4 "32bit us 타임스탬프" 정책과 일치) — 이 순서만 계승,
  SALT/VER 핸드셰이크·AES-CCM 암호화는 범위 밖이라 가져오지 않음.
- **DATA0/DATA1 프레임 8→16바이트로 확장**(`m_ble_proto.h`/`.c`, `BLE_PROTOCOL_VERSION`
  1→2): offset 0-3 timestamp_us, 4-7 seq_num, 8-15 기존 필드(Red630/Red680/NIR/LED
  index). 기본 ATT MTU(20바이트 payload)에 여전히 들어감. `AS7341_SEQ`(0x1529)는
  하위 호환을 위해 유지. **앱도 8→16바이트 파싱으로 업데이트 필요**(사용자가 앱을
  함께 관리).
- **안전상태 오래치 버그 수정**(`m_ble.c`): `notify_sample()`/`notify_seq()`가
  `-ENOMEM`을 `-ENOTCONN`/`-EINVAL`과 같은 무해한 전달 실패로 재분류, congestion
  감지 시 `BLE_NOTIFY_CONGESTION_BACKOFF_MS`(20ms) 대기 추가.
- **빌드 검증 완료**(`version_MCUBOOT: "0.1.18+0"`, RAM 91.67% 그대로, FLASH +64B).
  **하드웨어 검증 미실시** — 220샘플 backlog flush 시 `-ENOMEM` 실제 재현 여부, 래치
  없이 정상 flush되는지, 앱이 16바이트 프레임을 올바르게 파싱하는지 전부 실기 확인 필요.

### 추가 완료 (v0.1.19, DATA0/DATA1 배칭 + LED index 제거 + connection param 조정)
- 사용자 지적: "지금 Data에 LED Index가 필요한가? 10Hz마다 매번 notify하면 전력 소모가
  클 것 같다, 프레임을 키워서 데이터를 묶어 보내자." 확인 방향 두 가지를 먼저
  질문으로 정리해서 사용자에게 확인: (1) 배칭만 할지 connection interval/peripheral
  latency 조정도 같이 할지 → **"둘 다 같이 적용"**, (2) 배치 크기(N) 고정값 vs MTU
  자동산출 → **"협상된 MTU에서 자동 산출"**.
- **LED index 제거**: 항상 0(순차 LED 스트로빙 미구현)이라 죽은 필드였음 확인 후 제거.
- **DATA0/DATA1 v3 배칭 프레임**: `[count(1B)][record×N]`, record=timestamp_us+
  seq_num+Red630/680/NIR=14바이트. N은 MTU 협상 결과로 매 연결마다 자동 계산(상한 17,
  협상 실패 시 1로 자동 축소, 별도 레거시 포맷 없음).
  - **재연결 안전성 설계**: 배치 버퍼에 샘플이 여러 loop 반복에 걸쳐 누적되면 그 사이
    연결이 끊길 때 ring buffer 안전장치 밖에서 유실될 위험이 있어, "배치가 다 찼거나
    flush timeout(2초)일 때만 필요한 만큼 한 번에 pop 후 즉시 notify"하는 구조로
    위험 구간을 최소화(`m_i2c_ring_buffer_count()` 신규 추가).
- **MTU 협상 + LE Data Length Update를 우리가 선제 요청**(`bt_gatt_exchange_mtu()`/
  `bt_conn_le_data_len_update()`) — 컨트롤러는 이미 251byte까지 지원하도록 기본
  설정돼 있었음(`.config`로 확인, 추가 Kconfig 불필요했던 부분).
- **connection interval/peripheral latency 조정 요청**(`bt_conn_le_param_update()`,
  30ms/latency 4) — 배칭만 하면 라디오가 여전히 자주 깨는데 보낼 데이터가 없어
  전력 절감 효과가 제한적이라는 점을 설명하고 사용자가 함께 적용하기로 결정.
- **AS7341_SEQ notify 중단**(characteristic 선언은 유지) — 배칭 레코드에 이미
  seq_num이 있어 중복이 됨.
- **빌드 검증 완료**(`version_MCUBOOT: "0.1.19+0"`, RAM 92.50%/60,624B(+544B),
  FLASH 74.10%(+3.7KB)). **하드웨어 검증 미실시** — 배치 크기 산출, connection param
  수용 여부(중앙기기가 거부 가능), 앱의 새 프레임 파싱, 실제 전력 절감 효과(AT-06)
  전부 확인 필요.

### 다음 세션에서 먼저 할 일
1. **v0.1.19 실기 검증 최우선**: 앱을 새 배칭 프레임(`[count][record×N]`) 파서로
   업데이트한 뒤 (a) MTU 협상 후 실제 산출된 배치 크기가 로그와 일치하는지, (b) 각
   record의 timestamp_us/seq_num이 올바른지, (c) connection param 조정이 실제로
   반영되는지(중앙기기 로그/nRF Connect로 확인), (d) 전류소비 측정으로 배칭+interval
   조정 전/후 실제 절감 효과 확인(AT-06).
2. **v0.1.18 실기 검증**: 22초 이상 BLE 끊김 후 재연결로 backlog를 몰아 flush할 때
   `-ENOMEM` 혼잡이 재현되는지, 안전상태(`CTRL_STATE_DEGRADED`) 래치 없이 정상적으로
   flush되는지 RTT로 확인.
3. **v0.1.17 실기 검증**: 22초 미만 BLE 끊김 후 재연결 시 데이터 gap이 없는지 확인,
   22초 초과 시 STANDBY(LED1 점멸)로 정상 전환되는지 확인.
4. **v0.1.14 watchdog 수정 실기 확인**(이월): 부팅 후 재부팅 없이 정상 진행되는지 RTT로
   확인. 그 다음 19.7ms 적분시간에서 raw 데이터가 여전히 안정적인 비영 값인지 확인 —
   노이즈 수준으로 떨어지면 gain을 더 올리거나(현재 9=256x, 최대 10=512x) 적분시간을
   다시 조정.
5. Safety 항목5-[2]/[3] 실기 검증 — **PoC v2 보드로 SWD 환경 개선된 뒤 재시도**로 이월
   확정(2026-09-21 사용자 결정, architecture.md §11 항목6-[2]/[3]).
6. AS7341 gain/ATIME 정밀 캘리브레이션(논문 재현 목표) 계속 진행 — 이번 세션은 "레퍼런스와
   동일 세팅"까지만 완료, 논문 수준 데이터 재현은 다음 단계.
7. TODO(open-item, 별개 이슈): 버전 점멸 시간이 PATCH 증가에 따라 계속 길어짐(v0.1.19
   기준 6000ms) — watchdog stale 문제는 해소했지만 부팅 체감 시간 증가 자체는 남아있음,
   점멸 횟수 캡 방식 검토 필요.
8. RAM 사용률이 92.50%까지 올라온 상태 — 향후 배터리/온도 센서 등 기능 추가 시 RAM
   여유를 다시 검토해야 함(현재 여유 약 4,912B).

### 오늘 건드린 파일
- 코드: `Application/src/m_i2c_as7341.c`, `Application/include/m_i2c_as7341.h`,
  `Application/src/m_i2c.c`, `Application/src/m_ble.c`, `Application/src/m_ble_gatt.c`/
  `.h`(v0.1.15에서 변경 후 v0.1.16에서 원복, v0.1.18 congestion 처리, v0.1.19 배칭+
  MTU/DLE/conn param 조정), `Application/src/m_ble_proto.c`/`Application/include/
  m_ble_proto.h`(v0.1.18 프레임 확장, v0.1.19 배칭 재설계), `Application/src/
  m_i2c_ring_buffer.c`/`Application/include/m_i2c_ring_buffer.h`(v0.1.19 count getter),
  `Application/prj.conf`(v0.1.15/16 본딩, v0.1.19 BT_GATT_CLIENT/USER_DATA_LEN_UPDATE),
  `config_app.h`(FW_VERSION_PATCH, RING_BUFFER_CAPACITY/BLE_DISCONNECT_STANDBY_TIMEOUT_
  TICKS), `Application/VERSION`
- 문서: `CHANGELOG.md`(v0.1.12~v0.1.19), `architecture.md`(§2.4 BLE 파라미터 구조
  구현 완료 기록, §2.5 ring buffer 불일치 기록, §11 항목6-[1] 안전상태 오래치 버그
  기록, §11 항목6-[2]/[3] PoC v2 이월 기록 + grace
  period 값 변경, §11 항목6-[4] DATA0/DATA1 프레임 확장 기록, §11 항목6-[5] 본딩
  활성화→롤백 기록 + BLE_PROTOCOL_VERSION 갱신, §11 항목7 grace period 값 갱신 +
  런타임 재연결 검증 완료), `SESSION_NOTES.md`(본 항목)
- 빌드 환경: `C:\ncs\toolchains\dcbdc366a1`(west v1.5.0) + `C:\ncs\v3.4.0` 워크스페이스로
  `west build -b nrf52dk/nrf52832 Application` 정상 동작 확인(이전 세션엔 툴체인 없었음).
- 업무 문서(코드 저장소 밖, Downloads 폴더): 일일 업무 일지·간트차트 작성 완료(위 TL;DR
  참고) — 원본 옆에 `_작성완료` 접미사로 별도 저장, 원본은 안 건드림. 트래커 HTML은
  기존 방침대로 직접 수정 안 함(붙여넣기용 컨텍스트만 채팅으로 전달).

### git 상태
**2026-09-22 커밋 완료**: `22bc65e v0.1.12~v0.1.19 (2026-09-21)` — 어제 작업(v0.1.12~
v0.1.19) 15개 파일 전부 하나의 커밋으로 정리(사용자 요청으로 다음날 진행). 아직 push는
안 함(요청 없으면 하지 않음).

---

## 2026-09-18 (펌웨어 v0.1.4 → v0.1.11)

### 오늘 다룬 것
주간과제4(BLE 실 스택+APK 연동 PWM 제어)와 주간과제5(Safety 인증 대응) 마무리 작업.

### 완료
- **cycle/active 점멸 주기 게이팅 구현+실기 검증** (v0.1.5/v0.1.6) — `m_i2c.c`.
  RTC 100ms tick 주기는 유지한 채 tick 카운터로 "이번 tick이 active 구간인지"만
  판정. 1 tick(100ms) 미만 설정값에서는 의도적으로 no-op(항상 active)이 되도록
  설계 — 실기 테스트(cycle=100ms/active=20ms)에서 실제로 no-op 확인, cycle=1000ms/
  active=200ms로 재테스트 시 정상 동작 확인.
- **gap 식별 인프라 추가**: `AS7341_DROPPED_COUNT`(0x152A, read-only) characteristic
  신설(v0.1.7) — 죽어있던 `m_i2c_ring_buffer_get_dropped_count()`를 연결. 부수적으로
  이 getter들에 mutex 보호가 없던 버그도 같이 수정.
- **OTA 손상 이미지 롤백 검증** (v0.1.8/v0.1.9): 케이스A(서명 깨진 이미지 → swap 거부)
  정황 증거로 확인, 케이스B(test 미확정 → 자동 롤백)는 반복 재현으로 **사용자 판단
  100% 처리** — 단 RTT 부팅배너 자체는 못 잡음, **정식 자동화 TC는 양산 단계에서
  추가 예정**(지금은 반복 관찰 수준).
- **disconnect 후 앱이 안 재연결되는 현상 조사**: 펌웨어는 정상(advertising 즉시
  재시작 확인), 테스트 APK가 자동 재연결을 안 하는 게 원인 — 펌웨어 버그 아님으로
  결론, `architecture.md` §11 항목7에 앱 개선 필요 항목으로 기록.
- **Safety 항목5-[2]/[3] 실기 검증용 로그 추가** (v0.1.10): STANDBY 진입, ring buffer
  overflow, sanity check(SATURATION/LOW_SIGNAL) 실패 로그. **실기 검증 자체는 SWD
  연결 불안정으로 다음 세션으로 연기**(v0.1.11) — 코드/로그는 준비 완료.

### 미착수 (의도적 보류, 코드 변경 없음)
- **BLE 페어링/암호화**: 테스트 앱이 본딩을 지원하는지 확인 전까지 보류 — 이미
  `architecture.md`에 기록된 기존 결정, 오늘도 그대로 유지.

### 다음 세션에서 먼저 할 일
1. **SWD 연결이 안정적인 환경에서** Safety 항목5-[2]/[3] 실기 검증 재시도 —
   BLE 끊고 45초 대기하면 overflow(~15초)/STANDBY(~30초) 둘 다 한 번에 확인 가능
   (방법 상세는 `architecture.md` §11 항목6-[2]/[3] 참고).
2. 테스트 APK가 페어링(본딩)을 지원하는지 앱 담당자에게 확인 → 지원하면 BLE 암호화
   착수.
3. OTA 검증을 양산 단계에서 정식 TC로 승격.

### 오늘 건드린 파일
- 코드: `Application/src/m_i2c.c`, `m_ble_gatt.c/.h`, `m_i2c_ring_buffer.c`,
  `config_app.h`, `Application/VERSION`
- 문서: `architecture.md`(§11 항목4/6/7 갱신), `CHANGELOG.md`(v0.1.5~v0.1.11),
  `tracker_context.md`(신규, 트래커 외부개발 인수인계용)
- 프로젝트 외부: 일일 업무일지 docx, Gantt xlsx("fNIRS Project" 시트 J열=9/18)
  최신화 완료
- 전역(이 저장소 밖): `~/.claude/CLAUDE.md`에 사용자 공통 작업 규칙 5개 저장
  (용어/문서 동기화/일지·주간보고 작성 관점)

### 빌드/검증 환경 참고
- 이 세션 환경엔 west/NCS 툴체인이 없음 — 사용자가 별도 환경에서 빌드해서 결과물을
  넘겨주는 방식으로 진행함. RTT 로그 캡처는 SWD 접촉 불안정으로 자주 실패함(9/17
  watchdog 검증 때부터 반복된 이슈) — `JLinkRTTLogger.exe`로 파일에 직접 기록하는
  방법을 안내해뒀음(터미널 실시간 관찰 대신).
