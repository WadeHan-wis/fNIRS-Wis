# fNIRS 양산형 펌웨어 아키텍처 (Architecture)

| 항목 | 내용 |
|---|---|
| 문서번호 | ARCH-001 |
| 개정번호 | Rev.1 (확정 반영판) |
| 대상 프로젝트 | fNIRS 양산형 디바이스 펌웨어 |
| 기준 논문 | Ban et al., "A soft wearable NIRS system for detecting brain water dynamics linked to glymphatic activity during sleep", Sci. Adv. 12, eaad2056 (2026) |
| 작성자 | Wade Han (분석/정리 보조: Claude) |
| 최초 작성일 | 2026-09-14 |
| 최종 갱신일 | 2026-09-15 |
| 상태 | §11의 미확정 항목 4건이 해소되기 전까지 **정식 승인 문서로 간주하지 않음** |

> 본 문서는 (1) fNIRS 논문 기반 스펙, (2) `TedreamS1` 소스 분석, (3) `NCS_TedreamS2` 개정이력 분석, (4) 팀 결정사항(2026-09-15)을 종합한 아키텍처 확정 문서다. 코딩 규칙은 [`codingstandard.md`](./codingstandard.md), AI 에이전트/개발자 공통 작업 지침은 [`agents.md`](./agents.md)를 참고한다.

---

## 0. 현 단계 최우선 목표 (Top Priority)

> **각 파장(640/680/950nm)별 LED 데이터를 분리해서, AS7341로부터 I2C를 통해 정확하게 읽어오는 것**이 현재 단계의 최우선 목표다. Source multiplexing 타이밍, dark-frame subtraction, SQI, batching 등 세부 조정 사항은 **이 정확한 raw 데이터 획득이 검증된 이후** 개발하면서 순차적으로 결정한다.

이 문서의 §5~7 중 LED/AS7341 정책·BLE 파라미터 구조·Ring buffer 구조·배터리 Runtime 목표는 **2026-09-15부로 확정**되었다. Thermal 세부 수치와 배터리 용량/DFU/생산검사/규제 요구사항, 논문 내 모순 2건은 여전히 **보류**다 (§11 참고).

---

## 1. 프로젝트 성격 정의

**fNIRS는 근적외선 분광법(NIRS) 기반으로 뇌혈류(HbO/HbR)와 뇌 활동을 분석하는 웨어러블 디바이스이며, 신규 양산형 프로젝트다.** 기존 테스트 디바이스는 "센서 연구개발/알고리즘 검증용 프로토타입" 수준이었고, 이번은 **양산형 디바이스로서 처음부터 재설계**한다.

사내 자산을 다음과 같이 명시적으로 차용한다:

| 차용 대상 | 원본 | 계승 범위 |
|---|---|---|
| 펌웨어 구조 (모듈 분할, 상태관리, 네이밍 철학) | `TedreamS1` (nRF5 SDK/FreeRTOS, 기존 양산 제품) | 태스크 분리 철학, CTRL 경유 상태머신, GPREGRET 상태보존 |
| 통신 프로토콜 관례 (BLE 정책, 타임스탬프, 에러코드, OTA) | `NCS_TedreamS2` (NCS/Zephyr로 이미 전환된 사내 프로젝트) | BLE 정책, `module_err_t`, MCUboot/OTA |
| 베이스 플랫폼 (OS) | 신규 결정 | **Nordic nRF Connect SDK + Zephyr OS** |

연구용 프로토타입 코드 자체는 그대로 확장하지 않는다 — 양산 기준으로 구조를 재설계하되, 논문 재현에 쓰인 사양(파장/샘플링레이트/전력 등)은 검증 기준(AT, §10)으로 계승한다.

---

## 2. OS / 아키텍처 결정

### 2.1 Bare-metal vs Zephyr — ✅ 확정: Zephyr(NCS) 채택

| 판단 근거 | 내용 |
|---|---|
| 채택 이유 | BLE 연결관리/재연결, ring buffer, watchdog, 센서 fault recovery, 배터리/온도 모니터링, DFU, 생산검사 모드 등 양산 요구사항이 많고 계속 늘어날 것 — bare-metal 상태머신으로는 유지보수 비용이 빠르게 커짐 |
| 우려/보완 | Zephyr 스케줄러/`k_timer`만으로는 100ms acquisition의 정밀도가 불충분할 수 있음 → HW RTC 기반 트리거를 Zephyr와 병행하는 하이브리드 구조로 보완 (§2.3) |
| 사내 근거 | `NCS_TedreamS2`가 이미 동일한 결정(Zephyr)을 내리고 운영 중 — 신규 결정이 아니라 검증된 선례를 따르는 것 |
| 확정 현황 | 2026-09-15 팀 승인 완료. bare-metal 검토 종료 |

### 2.2 Acquisition / BLE Task 분리 원칙 (핵심 아키텍처)

```
LFCLK → RTC(100ms event) → ISR(timestamp+semaphore만)
                               │
                               ▼
                     Acquisition Task (High)
                     LED PWM 제어 / AS7341 Read / Timestamp / SeqNum
                               │
                          Ring Buffer
                               │
                               ▼
                     BLE TX Task (Medium)
                     Packet Batching / Notify / Retry
```

- Acquisition과 BLE 통신은 반드시 **독립적으로 동작** — BLE 지연/재연결/혼잡이 optical sampling timing에 영향을 주면 안 됨
- ISR은 **timestamp capture + semaphore/event set만** 수행 (BLE notify, log, flash write, I2C 트랜잭션, 신호처리, malloc 금지 — 상세 규칙은 `codingstandard.md` §3)
- Task 우선순위: **Acquisition > BLE TX > Control/Command > Battery/Temperature > Logging/Diagnostics**
- 저전력 원칙: 모든 task는 **event-driven** (semaphore/message queue 대기), busy polling 금지

### 2.3 RTC vs TIMER — ✅ 하이브리드 구조 확정 (100ms 정확도 확보 방법)

- 10Hz(100ms) periodic wake-up은 **RTC/LFCLK 기반**으로 확정 (저전력 유리, HF TIMER 상시구동 불필요)
- **Zephyr `k_timer`를 거치지 않고 `nrfx` RTC 드라이버를 직접 사용**한다 — Zephyr 커널 틱 스케줄링을 경유하면 ISR 지연이 수백 us~1ms 단위로 흔들릴 수 있어, nrfx RTC의 CC(Compare) 인터럽트를 직접 핸들링해 지연을 최소화한다.
- **Drift 보정 (Bresenham형 오차분산):** LFCLK 32.768kHz 기준 100ms는 3276.8 tick으로 정수가 아니므로, 매 프레임 0.8 tick의 소수부 오차가 누적된다. CC 값을 매번 3277로 고정하면 장기적으로 느려지고, 3276으로 고정하면 빨라진다.
  → **5프레임마다 1회 3277, 나머지 4회는 3276으로 교대 설정**한다 (5프레임 합계 = 3276×4 + 3277 = 13381 tick = 정확히 500ms). 이 방식으로 누적 오차를 프레임 5개 단위로 주기적으로 상쇄한다.
- LED sequencing/광 획득 내부의 us~ms 정밀 타이밍이 필요한 구간에서만 **HF TIMER 또는 DPPI/PPI**를 국소적으로 사용한다 (RTC wake 이후 필요할 때만 HFCLK enable).
- 예상 시퀀스: `RTC CC 인터럽트(보정된 tick) → (필요시) HFCLK enable → LED PWM sequence → AS7341 acquisition → peripheral idle → sleep`
- 구현 후 반드시 오실로스코프/로직분석기로 장시간(수십분~1시간) 실측해 보정 없는 경우 대비 누적 drift가 얼마나 줄었는지 검증한다.

### 2.4 BLE 파라미터 구조 — ✅ S2 계승

- Tx power **-8dBm 고정** — 여전히 미착수(TODO, m_ble.c).
- 전 패킷 **32bit us 타임스탬프** — S2 정책 그대로 계승, DATA0/DATA1 배칭 프레임에 실제 적용됨(v2, CHANGELOG v0.1.18).
- **Connection interval/peripheral latency — 2026-09-21 초기값 적용**(CHANGELOG v0.1.19,
  사용자 요청): 30ms interval, latency 4(유휴 시 연결 이벤트 4/5 스킵)로
  `bt_conn_le_param_update()` 요청. S2 선례 범위(7.5~30ms) 내 값이나, 여전히 **초기값**
  — 중앙기기가 거부/재협상 가능하고 실측(AT-06 전류소비 검증) 후 조정 필요.
- **MTU 협상 — 2026-09-21 구현 완료**(CHANGELOG v0.1.19): `bt_gatt_exchange_mtu()`로
  peripheral이 선제 요청, 목표 247(payload 244), 실패 시 배치 크기 1(=사실상 폴백)로
  자동 축소 — 별도 레거시 프레임 없이 동일 파서로 처리(m_ble_proto.h v3 스펙).
  LE Data Length Update도 함께 요청(`bt_conn_le_data_len_update()`) — 컨트롤러는 이미
  251byte까지 지원하도록 기본 설정돼 있었음(빌드 `.config`로 확인).
- **Batch size — 2026-09-21 구현 완료**: 원안 추정(샘플당 16~20byte, payload 244byte
  기준 약 12~15샘플/패킷)과 실제로 거의 일치 — LED index 제거로 샘플당 14byte가 되어
  상한 17샘플/패킷(`BLE_PROTO_BATCH_MAX_SAMPLES`)로 계산됨. 협상된 MTU에서 매 연결마다
  자동 산출(`m_ble.c` MTU exchange 콜백), 고정값이 아님.
- **Flush 주기(무선 wake 빈도) — 2026-09-22 추가**(CHANGELOG v0.1.20): "한 프레임에
  몇 개 담을지"(위 batch size, MTU가 결정)와 "얼마나 자주 무선을 깨워 보낼지"는 서로
  다른 문제로 분리해야 한다는 점을 사용자와 논의 후 확정 — 후자는 센싱 주기(cycle/
  active 게이팅으로 변동 가능)가 아니라 전력 목표 기준으로 통제해야 배터리 소모가
  설정과 무관하게 일정해진다. `BLE_BATCH_FLUSH_INTERVAL_MS=1000`(1초, 초기값)로
  고정 — 이 주기가 되기 전까지는 MTU 협상이 잘 안 돼 프레임당 1개만 담기는 경우에도
  즉시 전송하지 않고 ring buffer에 쌓아뒀다가, 주기가 되면 쌓인 것을 여러 프레임으로
  나눠서라도 한 번에 몰아 보낸다(무선을 깨우는 횟수 자체를 통제).
  **2026-09-22 실기 검증(v0.1.21)**: 85.5초 실측에서 DATA0/DATA1 각각이 연속 10개
  (가끔 11개) 단위로 묶여 도착함을 확인(같은 센서 연속 레코드 개수 분석) — 10Hz×1초
  설계대로 정확히 동작. 이번 연결에서 협상된 MTU가 목표(247)보다 작아(추정 143~
  156byte) 상한 17이 아니라 10~11개/프레임으로 나왔음(오류 아님, 연결마다 달라질 수
  있음). 같은 세션에서 seq_num 865개 전부 연속(gap 0), device_timestamp_us 간격도
  865개 전부 정확히 100,000us(표준편차 0) — 배칭/타이밍 모두 실기로 확인됨.
  **미검증**: 연결 끊김이 없던 세션이라 재연결/22초 backlog flush/`-ENOMEM` 안전상태
  경로는 여전히 미검증(사용자 결정으로 재연결 시나리오 테스트는 추후 진행).

### 2.5 Ring Buffer / Flash Logging 구조 — ✅ S2 계승

- Ring buffer 크기: 10Hz 기준 BLE 지연 5~10초를 버틸 수 있는 50~100샘플에 안전마진을 더해 **100~200샘플**로 설계(원안).
  **2026-09-21 정정**: 이 원안은 BLE notify 지연/혼잡만 고려한 값이었는데, 이후 §11
  항목6-[3]에서 "연결 끊겨도 30초는 계속 측정+버퍼링"이 추가되면서 두 설계가 서로
  검토 없이 따로 갔다 — 150샘플(15초 분량)로는 30초의 절반도 못 버텨서 실제로는 앞부분
  데이터가 유실되고 있었다(`config_app.h`, CHANGELOG v0.1.17). `RING_BUFFER_CAPACITY`를
  `BLE_DISCONNECT_STANDBY_TIMEOUT_TICKS`에 직접 묶어 재발을 방지했고, RAM 예산(nRF52832
  64KB) 여유가 30초 전량을 못 담아서(96%까지 치솟음) grace period 자체를 **22초로
  단축**(사용자 결정) — 결과 RAM 91.67% 사용, 현재 `RING_BUFFER_CAPACITY`=220샘플.
- Flash logging은 초기 버전(Rev0~3)에서는 별도 구현하지 않음 — overflow 발생 시 §4 원칙대로 counter/flag만 기록. S2에 이미 구현된 로깅 방식이 있다면 Rev2 착수 시 소스 재확인 후 재사용 여부 결정

---

## 3. S1 구조 × S2 프로토콜 × 신규 아키텍처 통합 매핑

| 영역 | S1 (구조 원본) | S2 (프로토콜 원본) | fNIRS 신규 결정 |
|---|---|---|---|
| 태스크 분할 | CTRL/BLE/ADC/SPI/I2C 5개 | 동일 개념의 Zephyr 스레드 | Acquisition/BLE TX/CTRL/Housekeeping — Acquisition-BLE 독립성 원칙에 맞게 재설계 (S1의 태스크 분리 철학은 유지) |
| 재부팅/에러 상태머신 | `g_ctrl_status` → CTRL 태스크 경유, `NVIC_SystemReset()` 직접호출 금지 | `module_err_t` 에러코드 패턴 통일(Rev22) | S1의 "CTRL 경유 상태머신" + S2의 "통일된 에러코드 타입" 결합. Thermal Safety도 동일 패턴(Normal→Warning→Duty감소→OFF) 사용 |
| 네이밍 | `g_/s_/m_/sem_/_t`, `module_x_*` | `module_x_*` → `m_x_*` 리네이밍(Rev20-21) | S2식 `m_x_*` 채택 (최신 관례) — 상세는 `codingstandard.md` |
| GPREGRET 상태보존 | LED 디밍 상태 보존 | 계승 | Thermal Warning/Trip 상태도 리부팅 후 복원 검토 (보류, §11) |
| OTA/DFU | 자체 DFU, `SECURE_ENABLED→DFU_ENABLED` 종속 | **MCUboot + sysbuild** 전환(Rev20-21), OTA(SMP, Rev24) | MCUboot 그대로 채택 (Zephyr 표준, S2 선례 존재). **[신규] OTA는 v1.0 범위의 필수 사양으로 확정 — Rev2에서 구현** |
| BLE 정책 | SoftDevice `sd_*` | Tx power **-8dBm 고정**(Rev30), 전 패킷 **32bit us 타임스탬프**(Rev34), HVX 크레딧/대기큐(Rev38) | S2 정책 그대로 차용 — 단, fNIRS는 **batching**이 추가 필요(§2.4) |
| 디바이스 분기 | `DEVICE_TYPE`(A/C/H) | `DEVICE_TYPE`(A/C/CS/H) | 불필요(단일기기) — 매크로 구조만 확장 대비 유지 |

> ⚠️ **주의**: S2 REVISION_HISTORY 분석 중 커밋 메시지와 실제 diff가 불일치하는 사례가 다수 발견됨(v0.1.4, v0.1.8 등). S2 프로토콜을 차용할 때는 **반드시 코드 자체로 재검증**하고 커밋 메시지만으로 판단하지 않는다.

---

## 4. 데이터 무결성 / 패킷 설계 원칙

**RAW 데이터 보존 원칙**: MCU에서 Δ[H2O]로 가공하지 않고 **3파장(640/680/950nm) raw optical intensity를 그대로 보존**한다. 추후 소광계수/DPF/필터링/캘리브레이션이 바뀌어도 원자료로 재분석 가능해야 하기 때문이다. 역할 분리: **Device = acquisition 중심, PC/Server = 생리신호 처리 중심.**

패킷/샘플에 포함할 필드:

| 필드 | 비고 |
|---|---|
| timestamp | S2 관례: 32bit us 단위 |
| monotonic sequence number | 누락 검출용 |
| 640/680/950nm raw intensity | 3파장 개별 |
| sensor gain, integration time | 변경 시점 추적용 |
| LED drive setting/duty | PWM duty 값 |
| battery 상태 | — |
| status/error flag | S2 `module_err_t` 패턴 계승 |
| firmware/config version | — |

Gain/Integration Time은 **초기 검증 단계에서는 고정**한다 (런타임 변경 시 생리신호처럼 보이는 인위적 step 발생 가능). 자동조절이 필요해지면 변경 시점·값을 반드시 데이터에 기록한다. 논문은 구체적 게인 수치를 공개하지 않으므로, Rev1(AS7341 I2C 드라이버 포팅) 단계에서 채널별 saturation 임계 게인을 자체 실측해 고정값을 확정한다.

**Diagnostic telemetry 후보**: `dropped_sample_count`, `max_ring_buffer_usage`, `ble_retry_count`, `disconnect_count`, `sensor_read_error_count`, `watchdog/reset_reason`, `battery_low_event`, `temperature_warning_event`. Silent data loss 방지가 최우선 — overflow 발생 시 반드시 counter/flag로 기록한다.

---

## 5. LED / AS7341 정책 — ✅ 확정

- **LED 구동방식: PWM duty 제어** (연속점등 아님). Rev1(LED source multiplexing) 단계에서 3파장 시퀀싱과 함께 구현한다.
- **AS7341 gain/integration time**: §4 원칙대로 초기 검증 단계에서 고정값 사용. 채널별 saturation 임계 게인은 Rev1 착수 시 실측으로 확정한다.
- 파장 채택 부품: 640nm = **XZM2CRK54WA-8**, 680nm = **OIS-330 IE680** (Opto International), 950nm = **MTE9730CP** (Marktech Optoelectronics). Photodetector = **AS7341** (ams OSRAM). Source-detector 거리 = 30mm(3cm, 논문 Fig.3C/Monte Carlo와 정합 확인됨).

## 6. 전력 / BLE 최적화 방향

전력 최적화 우선순위: **① LED PWM duty cycle → ② BLE packet batching → ③ CPU sleep/Zephyr PM → ④ Peripheral runtime PM**

Ring Buffer로 BLE가 5~10초 막혀도 데이터 유실 없이 버퍼를 유지하는 것이 초기 목표다 (§2.5).

## 7. Thermal 안전 설계 (원칙 확정, 세부 수치는 보류)

논문 기준: 41°C 미만(최대 관측 약 40.1°C, 초기 가열속도 약 0.7°C/min — 첫 5분). 양산형은 FW 보호 로직을 추가한다:

```
Normal → Warning → LED current/duty 감소 → LED OFF
```

이 상태머신 패턴(§3의 "재부팅/에러 상태머신"과 동일한 CTRL 경유 방식)은 확정이나, **온도센서 위치, trip threshold, hysteresis, 충전 중 동작 여부**는 보류 상태다 (§11). Rev3(Reliability)에서 임시값으로 구현하고, Rev7(Productization)에서 최종화한다.

> ⚠️ **하드웨어 갭(2026-09-16 확인)**: PoC v1 보드(`SCH_fNIRS_Sleep_Project.pdf`)에는 온도센서(NTC/디지털 온도센서 IC)가
> 전혀 실장되지 않았다 — 위 상태머신은 현재 하드웨어로는 **구현 자체가 불가능**하다(정책 미확정이 아니라
> 센싱 경로 부재). 상세는 §11 "다음 보드 리비전 반영 예정" 참고. Rev3에서는 이 항목을 제외하고 Watchdog만 진행한다.

## 8. 배터리 Runtime 목표 — ✅ 확정: 8h

**계산 근거 (논문 기반)**:
- 배터리: 110mAh × 3.7V = 407mWh
- 논문 평균 소비전력: 70~75mW → 실측 재현 약 5.4~5.8h (논문 보고 5.5h와 일치)
- 8h 달성에 필요한 평균 소비전력: 407mWh / 8h ≈ **51mW** (논문 대비 약 30% 절감 필요)
- LED PWM duty 제어(연속점등 → 구간점등 전환)만으로 달성 가능한 범위로 판단 — Rev6(Power Optimization)에서 실측 튜닝
- 10h(≈41mW, 약 44% 절감)는 배터리 용량 변경 없이는 공격적이므로 1차 목표에서 제외, Rev6 실측 후 스트레치 목표로 재검토

배터리 **용량**(현재 110mAh 유지 여부), DFU 방식 사양, calibration 저장 구조, 과충전/과방전(UVLO) 보호 회로 인터페이스는 여전히 **보류**다 (§11).

> ⚠️ **하드웨어 갭(2026-09-16 확인)**: PoC v1 보드에는 배터리 잔량을 측정하는 fuel gauge/monitor IC가 없다
> (`BQ51050B`는 무선충전 수신 전용, `MAX16054`/`LTC4412`는 전원경로/버튼 제어용으로 배터리 전압 측정 기능 없음).
> 게다가 nRF52832의 ADC 가능 핀(P0.02~P0.05/AIN0~AIN3)이 전부 LED 제어 신호(WH/IR_LED_CTRL1/2)로 이미
> 점유돼 있어, 저항분배로 VBAT를 MCU ADC에 연결할 여유 핀조차 없다 — **소프트웨어만으로는 해결 불가능한
> 하드웨어 제약**이다. 상세는 §11 "다음 보드 리비전 반영 예정" 참고. Rev3에서 `nirs_sample_t.battery_pct`는
> 계속 0 고정으로 유지한다.

---

## 9. 개발 단계 (Rev 0~7)

| Rev | 범위 | 핵심 산출물 |
|---|---|---|
| Rev 0 | S1 구조/S2 프로토콜 이식 + Zephyr 스캐폴드 | 신규 NCS 프로젝트, 모듈 구조 이식, RTC 100ms 스켈레톤, CTRL 상태머신 |
| Rev 1 | **Optical Acquisition (현 최우선)** | 파장별(640/680/950nm) 분리된 정확한 I2C raw data 획득, LED PWM 시퀀싱, 패킷 반영 |
| Rev 2 | BLE Streaming + OTA | ring buffer, batching, packet loss 검출, reconnect, **OTA(MCUboot+SMP) 업데이트/롤백** |
| Rev 3 | Reliability | watchdog, sensor recovery, battery/temperature monitor, 열보호 상태머신(임시 threshold), 통합 스모크 테스트 |
| Rev 4 | Signal Validation | raw 신호 검증, 운동/breath-holding 실험, Δ[H2O] 재현성 |
| Rev 5 | Overnight Validation | 장시간 안정성(자체 시계열 drift), EEG/EOG 동시측정은 조건부(§11) |
| Rev 6 | Power Optimization | LED PWM duty 세부 튜닝, BLE batching, Zephyr PM, peripheral PM — 8h 목표 실측 |
| Rev 7 | Productization | Thermal 최종화, calibration/versioning, 생산검사, 규제/품질 대응 |

Rev0~3(+OTA)는 **17근무일**(2026-09-14~10-12, 공휴일 제외)로 일정이 확정되어 있다. Rev4 이후는 Rev0~3 실측 데이터를 근거로 **10/12(월) 재산정 지점**에서 팀과 다시 수립한다. 상세 일정과 진행 상황은 프로젝트 트래커(`fNIRS_FW_v1.0_트래커.html`)를 참고한다.

---

## 10. Acceptance Test (AT) 체크리스트

| ID | 이름 | 조건 | 판정 기준 |
|---|---|---|---|
| AT-01 | Sampling | 10Hz 장시간 | timestamp/seq 연속, jitter 기준 만족 |
| AT-02 | BLE Stress | connection parameter 변화/혼잡 | acquisition timing 불변 |
| AT-03 | Buffer | BLE 강제 block | ring buffer 정상, overflow silent loss 없음 |
| AT-04 | Sensor Saturation | 광량 과다/부족 | 포화/저신호 flag |
| AT-05 | Configuration | gain/integration/LED 변경 | 변경 시점 데이터 기록 |
| AT-06 | Power | 논문기준 모드 소비전력 | baseline 확보, FW 변경별 회귀비교 |
| AT-07 | Thermal | 최악조건 장시간 | 41°C 미만, 보호로직 정상 |
| AT-08 | Runtime | 연속구동 | 연구재현 5.5h 이상, **제품목표 8h 확정 검증**(LED PWM duty 절감 포함) |
| AT-09 | Reconnect | 강제 disconnect/reconnect | gap 식별 가능, hang 없음 |
| AT-10 | Watchdog | I2C/BLE fault 주입 | 자동복구 + fault log |
| AT-11 | Physiological Validation | 운동/숨참기 | 상태의존 변화 재현 |
| AT-12 | Overnight | 장시간 연속 측정 (EEG/EOG 동시측정은 실사용 시나리오 미정 — §11) | 장시간 안정성 + 자체 시계열 정확도(clock drift) |

---

## 11. 미확정 항목 (팀 논의 필요, 승인 전 해소)

### 확정된 항목 (참고용으로 기록)
- ✅ OS: Zephyr(NCS) 채택 확정 (2026-09-15)
- ✅ RTC/TIMER 하이브리드 정확도 방법 확정 (§2.3)
- ✅ BLE 파라미터 구조 확정 — S2 계승 (§2.4)
- ✅ Ring buffer/flash logging 구조 확정 — S2 계승 (§2.5)
- ✅ 제품 목표 runtime 확정: 8h (§8)
- ✅ LED 구동방식 확정: PWM duty 제어, AS7341 gain/integration 고정값 원칙 (§5)

### 보류 — 여전히 미확정
1. 배터리 용량, DFU 방식(사양), calibration 저장 구조 — 과충전/과방전(UVLO) 보호 회로 인터페이스 검증 포함
2. 생산검사(production test) 모드 구조
3. 규제 관점 추가 안전/신뢰성 요구사항
4. 논문 내 모순 2건(sleep threshold 방향성, spectral band 서술) — 저자 코드/공개자료 확인 전까지 펌웨어/서버 어느 쪽에도 구현하지 않음
5. **AS7341 gain(AGAIN)/integration time(ATIME/ASTEP) 실측 캘리브레이션 및 SMUX 채널 매핑(F5~F8+Clear+NIR) 바이트 단위 교차검증**
   — 참고문헌: Ban et al., *Sci. Adv.* 12, eaed2056 (2026) "A soft wearable near-infrared
   spectroscopy system for detecting brain water dynamics linked to glymphatic activity
   during sleep" (`docs/nirs_thesis.pdf`). 이 논문은 640/680/950nm LED(680nm=OIS-330 IE680,
   950nm=MTE9730CP) + AS7341 포토디텍터 + **source-detector 30mm** + **10Hz 샘플링**으로 우리
   구조와 시스템 레벨 파라미터가 거의 동일하나, AS7341 레지스터 수준(gain/ATIME 등) 설정값은
   논문에 없다(과학 논문 특성상 당연히 로우레벨 firmware 설정은 미기재). 신호처리는 modified
   Beer-Lambert law(HbO/HbR/H2O extinction matrix + DPF 보정)를 사용하며, 이는 향후 Rev1
   신호처리 단계의 알고리즘 참고 대상이다.
   **검증 방식(2026-09-15 결정)**: 지금 펌웨어 단으로 정성적 광응답만 확인된 상태(v0.0.5,
   CHANGELOG 참고)이고, 정량 캘리브레이션은 **모바일 앱과 연동해서 raw data를 실제로 수집한
   뒤에 진행**하기로 함 — 필요 시 위 논문 저자(교신저자 W.-H. Yeo/C.-H. Yun)에게 직접 문의 가능한
   경로가 있음. 이 항목이 해소되기 전까지 gain=9(256x)/ATIME=29(약 83ms)는 "동작 확인" 수준의
   임시값으로 취급한다.
   **2026-09-22 실기 검증(v0.1.21, 앱 연동)**: v0.1.13에서 ATIME 고정+ASTEP 가변 방식으로
   바꾸고 기본 적분시간을 19.7ms로 낮춘 뒤 "신호가 노이즈 수준으로 떨어지지 않는지" 미검증
   상태였던 우려가 해소됨 — 85.5초 실측(1730 레코드, `2026-09-22_10-36-38_fNIRS_RAW_DATA.csv`)
   에서 Red630/Red680/NIR 전 채널이 포화(65535)·저신호(<10) 없이 안정적인 범위(Red630
   ~930~980/Red680 ~550~580/NIR ~53~59 평균)로 나옴(NIR 채널 1건만 경계값 6). **다만 이건
   "노이즈 문제 없음" 확인일 뿐, gain/ATIME 자체의 정량 캘리브레이션(레퍼런스 대비)은 이
   항목 그대로 미해소** — 다음은 레퍼런스 데이터 대비 raw 값 동등성 비교(주간과제2) 착수.

   **2026-09-22 레퍼런스 데이터 비교 분석 결과** (`2026-09-22_13-48-54_fNIRS_RAW_DATA.csv`
   1043초 vs 레퍼런스 `fnirs_nRF_fNIRS_System_3_20260812_033228.csv` 19,276초, 채널명
   `red630/red680/nir` 동일 매핑으로 직접 비교):
   - **raw 데이터 정의 자체가 다름**: 레퍼런스는 전체 행의 정확히 50%가 6채널 전부 0인
     "다크 프레임"(LED off 구간, ambient 제거용)이고 나머지 50%가 광신호 — 즉 raw 값이
     이미 ambient-subtracted다. 우리 쪽은 dark-frame이 전혀 없이 매 샘플이 광신호 raw
     값 — §2.2/이 항목 "3) Ambient light 제거(dark-frame subtraction)"가 아직 미착수
     상태라 데이터 정의부터 레퍼런스와 다르다. **절대값 1:1 비교는 이 차이 때문에 원천적으로
     제한적**, 정성적 패턴 비교만 유효.
   - 이 세션 측정에서 채널별 실측 샘플 간격은 정확히 300ms(3.3Hz)로 나옴 — RTC tick(100ms)
     기준 3틱마다 갱신되는 duty/cycle-active 설정으로 수집된 것으로 보임(v0.1.21 검증 때의
     100ms/10Hz 세션과 다른 설정, 오류는 아님).
   - **채널 간 상대 비율 불일치 (신규 발견)**: 절대 크기(gain/ATIME/광결합 차이로 원천 비교
     불가) 대신 `red680/red630`, `nir/red630` 비율로 정규화 비교 — 레퍼런스는 두 위치
     모두 red680·nir이 red630보다 **6~8배** 크게 나오는데, 우리 쪽은 두 센서 모두 채널
     간 크기가 서로 **0.4~1.4배** 수준(비슷)으로 나와 스펙트럼 형태 자체가 다르다. AGAIN은
     6채널에 전역으로 걸리는 단일 게인이라 이 채널간 상대비율 차이를 AGAIN/ATIME 조정만으로
     설명·교정할 수 없음 — SMUX 채널 매핑(F7/F8/NIR, 위 TODO) 또는 LED 파장별 광출력
     밸런스(§5) 쪽 원인일 가능성이 있어 추가 조사 필요. **정성적 신호 획득(포화/노이즈 없음)
     자체는 확인됐으나, 정량 캘리브레이션 착수 전 이 채널비율 불일치를 먼저 규명해야 함.**
   - **SMUX 채널 매핑 재검증 완료(2026-09-22) — 원인에서 배제**: 위 SMUX RAM 20바이트가
     ams 공식 앱노트 대조 없이 "공개 레퍼런스(Adafruit) 값 우선 적용" 상태였던 TODO를
     `_ref_fnirs_example/fNIRS/src/as7341.c`(`as7341_setup_f5f8_clear_nir()`)와 대조—
     이 레퍼런스는 각 SMUX 레지스터가 어느 필터를 어느 ADC 채널로 라우팅하는지 레지스터
     단위 주석(`0x03=0x40: route F8-left -> ADC3` 등)까지 남겨뒀고, 우리
     `s_smux_config_f5f8_clear_nir[20]`과 **20바이트 전부 완전 일치**함을 확인. ams 공식
     앱노트(AN000633) 원문 대조는 아니지만, 레지스터별 라우팅 근거가 명시된 자료와 바이트
     단위로 일치하므로 SMUX 매핑 자체는 정확한 것으로 판단 — **채널 간 상대비율 불일치의
     원인은 SMUX 매핑 오류가 아니다.** 다음 조사 방향은 LED 파장별 광출력(640/680/950nm
     각 LED의 실제 광량) 밸런스 쪽으로 전환 필요.
   - **AS7341 정밀 보정 계획 v1 (2026-09-22, 주간과제3 산출물)** — 사용자 결정: 레퍼런스와
     "물리적으로 동일한 광학 표준"이 아니라는 한계를 인지한 채로, 일단 **레퍼런스의 채널
     비율(red680/red630≈6~8배, nir/red630≈4~6배)에 근접하도록 LED 듀티를 실험적으로
     맞춰본다** (엄밀한 정량 캘리브레이션이 아니라 참고용 근사).
     - LED1(640nm)/LED2(680nm)/LED3(950nm) 듀티는 이미 BLE CONFIG 패킷으로 0~100%
       런타임 조정 가능(`m_i2c.c apply_pending_ble_config()`, `s_led_duty_permille[]`,
       기본값 전 채널 50%) — **펌웨어 코드 변경 없이 앱에서 듀티 값만 바꿔서 실험 가능**.
     - 절차: ① 현재(전 채널 50%) 기준 raw 비율 재확인 → ② LED2/LED3 듀티를 올리거나
       LED1 듀티를 낮춰 red680/red630, nir/red630 비율이 목표(6~8배 / 4~6배)에
       가까워지는 조합을 탐색(예: LED1 낮게, LED2/LED3 상대적으로 높게) → ③ 조합을
       찾으면 raw 데이터 재수집 후 이 문서 §11 항목5 비교 스크립트로 비율 재검증 →
       ④ 안정적으로 재현되는 조합을 기본 듀티값으로 채택할지 결정.
     - **주의**: 이 방식으로 레퍼런스와 비율을 맞춘다고 해도 "물리적으로 정확한 절대
       캘리브레이션"이 되는 건 아니다(서로 다른 하드웨어/광결합 조건). 진짜 정량
       캘리브레이션(광량계·반사표준 기반)은 별도 과제로 남아있고, 이번 실험은 "레퍼런스
       데이터와 패턴을 맞춰 정성적 동등성을 개선"하는 참고용 조정으로 한정한다.
     - **1차 실험 결과(2026-09-22, `2026-09-22_14-25-05_fNIRS_RAW_DATA.csv`)**: LED2
       50%→75%, LED3 75%→100%로 올렸으나 채널 비율이 거의 안 움직임(sensor0
       red680/red630 0.71→0.77, nir/red630 0.42→0.41; sensor1 1.39→1.72, 0.66→0.96) —
       듀티를 크게 올렸는데도 목표(6~8배/4~6배)에 거의 접근하지 못함. **듀티 조정이
       예상만큼 효과가 없다는 것 자체가 신호** — 원인 후보: (a) PWM 듀티→LED 실제
       광출력이 이 구간에서 비선형/포화일 가능성, (b) dark-frame subtraction이 없어
       ambient/crosstalk 성분이 채널값을 지배하고 있을 가능성, (c) §5 "LED source
       multiplexing 점등 시퀀스"(3개 LED를 시간분할로 각각 strobe)가 아직 미구현이라
       3개 LED가 동시에 계속 켜진 상태로 측정되고 있어 필터 선택도만으로는 완전히
       분리되지 않을 가능성. 다음 조사 방향 사용자 결정 대기 중.
     - **2차 실험: 완전 암실 테스트로 원인 확정(2026-09-22, `2026-09-22_15-19-34_
       fNIRS_RAW_DATA.csv`, `docs/test/`)** — 주변광을 완전히 차단한 환경에서 LED만
       켜고 측정했더니 대부분 1~2에 불과하고, **정확히 53샘플(sensor1)/139샘플
       (sensor0) 주기로만 큰 값이 튀는 beat(맥놀이) 패턴**이 확인됨. 위 원인후보 (c)가
       맞았음이 정량적으로 확정 — 단, 원인은 "LED source multiplexing 미구현"이
       아니라 **v0.1.12에서 AS7341을 free-running(SP_EN 1회만 켜고 재기록 안 함) 모드로
       바꾼 것과, `m_i2c.c`가 매 샘플마다 LED를 켰다/끄는 우리 아키텍처(§5 "연속점등
       금지")가 서로 비동기 주기로 돌면서 어긋난 것**이었다. free-running 적분 주기와
       LED on/off tick 주기가 서로 다른 속도라 위상이 주기적으로만(beat 주기로) 맞고,
       맞는 순간에만 실제 LED 신호가 잡힌 것. **v0.1.22에서 수정 완료**(CHANGELOG
       v0.1.22 참고) — `m_i2c_as7341_read_raw()`가 매 호출마다 SP_EN을 재기록해 LED가
       켜진 뒤 새 적분을 명시적으로 시작하도록 변경. **v0.1.12~v0.1.21 구간에서 수집한
       raw 데이터(오늘 레퍼런스 비교에 쓴 데이터 포함)는 이 동기화 버그가 있는 상태에서
       수집된 것이므로, 위 채널비율 비교 결과 자체도 재해석 필요** — v0.1.22 이후
       재수집해서 레퍼런스 비교를 다시 해봐야 한다.
     - **3차 실험: v0.1.22 실기 검증 — beat 패턴 해소 확인(2026-09-22,
       `2026-09-22_15-52-02_fNIRS_RAW_DATA_암실 테스트(LED Sync O).csv`,
       `docs/test/`)** — 동일한 완전 암실 조건에서 v0.1.22를 플래시해 재측정한 결과,
       0인 행/스파이크 없이 전 채널이 처음부터 끝까지 안정적(변동률 1% 미만,
       d1_red630 평균 22448±28.5)으로 나옴 — **beat 패턴이 사라진 것을 실기로 확인,
       LED-적분 동기화 버그는 해결된 것으로 판단.** 다만 채널 간 상대비율은 이 수정과
       무관하게 여전히 레퍼런스와 다름(d1 red680/630≈0.99·nir/630≈0.70, d2
       red680/630≈1.03·nir/630≈0.50 vs 목표 6~8배/4~6배) — **동기화 문제와 채널비율
       문제는 서로 독립적인 별개 원인**이었음이 이걸로 확인됨.
     - **4차: 실제 부착 측정 진행 중(2026-09-22, 사용자 관찰)** — 디바이스를 신체에
       다시 부착해 측정 중, 육안상 레퍼런스와 파형 형태는 비슷하고 전체적으로 오프셋만
       위로 올라간 형태로 보인다고 보고됨. **재프레이밍**: 위 3차 실험까지의 "평균
       채널비율(red680/red630 등) 매칭"은 fNIRS 신호처리 관점에서 애초에 맞는 목표가
       아니었을 수 있음 — modified Beer-Lambert law(§11 항목5 논문 참고자료)는 절대
       raw count가 아니라 **baseline 대비 상대 변화(ΔOD, AC/맥동 성분)**를 쓴다. 게인/
       적분시간/광결합이 다른 두 시스템은 DC 오프셋(baseline level)이 다른 게
       정상이고, 중요한 건 오프셋을 뺀 뒤의 파형(맥동 패턴)이 유사한지다. 데이터
       수신 후 이 관점(baseline 제거 후 파형 유사도)으로 재분석 예정.
     - **주의(2026-09-22, 사용자 지적)**: 레퍼런스 데이터(`fnirs_nRF_fNIRS_System_3_
       20260812_033228.csv`)는 **수면 중** 수집된 것이다. 활동 중(각성, 움직임 있음)에
       수집한 우리 데이터와는 대사/혈류 패턴 자체가 다를 수 있어, 채널비율·오프셋·맥동
       크기 차이의 일부는 상태(수면 vs 활동) 차이로 설명될 수 있음 — 레퍼런스와의 불일치를
       전부 펌웨어/캘리브레이션 문제로 단정하지 않는다.

6. **Safety 인증(IEC 60601-1/-1-8) 대응 필수 펌웨어 안전기능 갭 (2026-09-17 확인)**
   — 의료기기 safety 인증 시험 성적을 위한 필수 요구사항 검토 중 발견. 현재 펌웨어에 아래 항목이
   전혀 구현돼 있지 않거나 부분 구현 상태다. 소프트웨어만으로 해결 가능한 항목과, PoC v1 보드
   하드웨어 제약(§11 "다음 보드 리비전 반영 예정" 참고)으로 이번 리비전에서는 구현 자체가
   불가능한 항목을 구분한다.
   **2026-09-17 갱신**: HW 제약이 없는 6개 소프트웨어 항목은 코드 작성 완료(아래 각 항목에
   커밋 근거 표기) — 단, **이 세션 환경에 west/NCS 빌드 툴체인이 없어 빌드 검증은
   미실시**이고 하드웨어 검증도 전부 미실시다(§17 검증구분). 다음 실제 빌드 세션에서
   clean build부터 확인해야 "정식 확정"으로 볼 수 있다.
   - **안전상태(Safe State) 전이**
     - 단일고장(센서/통신/전원) 시 정의된 안전상태로의 명시적 전이 — **코드 작성 완료**
       (`m_ctrl_is_safe_state()`, `m_i2c.c` tick 루프에서 체크 — CHANGELOG v0.0.14 §1).
       래치 방식(자동복구 없음, 재부팅으로만 해제) — IEC 60601 단일고장 안전 철학에 따른
       의도적 설계 결정.
       **버그 수정(2026-09-21, CHANGELOG v0.1.18)**: 이 래치가 진짜 고장이 아닌 상황에서
       오발동할 뻔했다 — 재연결 후 ring buffer backlog(§11 항목6-[3], 최대 220샘플)를
       텀 없이 몰아서 notify하면 BLE ATT 송신 버퍼 풀이 고갈되어 `bt_gatt_notify()`가
       일시적 혼잡(`-ENOMEM`)을 반환할 수 있는데, `m_ble.c`가 이를 진짜 통신 장애로
       오분류해 `MODULE_ERR_BLE_TX_FAILED`를 보고 → `CTRL_STATE_DEGRADED` 래치 →
       재부팅 전까지 측정 영구 정지. `-ENOTCONN`/`-EINVAL`과 같은 "무해한 전달 실패"로
       재분류하고 congestion 감지 시 짧게 backoff하도록 수정. 하드웨어 검증 필요(실물
       backlog flush에서 `-ENOMEM`이 실제로 재현되는지, 래치 없이 정상 flush되는지).
     - Watchdog 리셋 후 안전 재시작 시퀀스 — **2026-09-17 실기 검증 완료** (`m_ctrl.c
       log_reset_cause()`가 RESETREAS로 watchdog 리셋 여부를 로그 — CHANGELOG v0.0.14 §2).
       `TEMP_WATCHDOG_FAULT_INJECT_TEST`로 m_i2c 태스크를 고의로 hang시켜 실기로
       확인: 경고 로그(t≈3.2s) → 약 4초 후 하드웨어 watchdog SoC 강제 리셋 →
       재부팅 시 `RESETREAS=0x00000010`(watchdog 비트) 정확히 감지+로그, 이 사이클이
       반복되는 것을 연속 캡처로 확인(CHANGELOG v0.1.3~v0.1.4). 이 프로젝트는 원래
       리셋 원인과 무관하게 항상 안전한 기본 상태(`DEVICE_ON`)로 부팅하므로 "이전
       상태 복구" 로직 자체가 불필요 — GPREGRET 기반 상태 복원은 **채택하지 않기로
       결정**(복원할 위험한 중간 상태가 애초에 없음). §3의 "GPREGRET 상태보존" 항목은
       LED 디밍 등 다른 맥락 용도로 남겨둔다.
     - BLE 연결 끊김 시 로컬 버퍼링 지속 + 장시간 미연결 시 저전력 대기모드 — **코드 작성
       완료**(CHANGELOG v0.0.14 §3). 구현 중 실제 버그 발견: `m_ble.c`가 끊김 중에도 계속
       pop해서 즉시 폐기하고 있어 "버퍼링"이 이름뿐이었음 — 연결 중에만 pop하도록 수정.
       "사용자 알림"은 진동/푸시 등 별도 액추에이터가 없어(§0 하드웨어 범위) LED 점멸로
       대체.
       **2026-09-18 확인**: 코드 리뷰로는 완료 상태였으나, 30초 grace period(`BLE_
       DISCONNECT_STANDBY_TIMEOUT_TICKS`) 만료 후 STANDBY 진입이 실기로 한 번도
       관찰된 적이 없었음(테스트 중 끊김이 항상 30초 미만이었음) — RTT에서 확인 가능하게
       `enter_standby()`에 로그 추가(`m_i2c.c`, v0.1.10). **실기 검증은 SWD 연결
       불안정(9/17 watchdog 검증 때와 동일한 문제)으로 RTT 캡처 자체가 너무 힘들어서
       2026-09-18에 다음으로 연기하기로 결정** — 코드/로그는 준비된 상태이니 SWD 환경이
       안정적일 때 재시도.
       **2026-09-21 재확정(사용자 결정)**: PoC v1 보드의 SWD 접촉 불안정은 이번 리비전에서
       근본 해결이 어려운 하드웨어 이슈로 판단, **PoC v2 보드로 SWD 환경이 개선된 뒤 재시도
       하기로 함** — 이번 주(9/21~23) 작업 범위에서 제외. 코드/로그(v0.1.10)는 그대로
       유효하므로 PoC v2 입수 후 바로 재시도 가능.
       **2026-09-21 값 변경(CHANGELOG v0.1.17)**: `BLE_DISCONNECT_STANDBY_TIMEOUT_TICKS`가
       ring buffer RAM 예산 재검토로 30초→**22초**로 바뀌었다 — 위 실기 검증(SWD 안정화 후
       재시도) 시 30초가 아니라 22초 기준으로 STANDBY 진입 시점을 확인해야 한다.
     - 배터리 저전압/완전방전 시 안전 셧다운 — **HW 제약으로 구현 불가** (배터리 IC 부재,
       "배터리 전압/잔량 센싱 경로 부재" 항목과 동일 원인, 그대로 보류)
     - 충전 중 착용 시나리오 정의·구현 — 그대로 보류 (오늘 범위 제외, 사용자 확정)
     - 과온 방어 로직 — **HW 제약으로 구현 불가** (온도센서 부재, 그대로 보류)
   - **경보 시스템 (IEC 60601-1-8 연계)**: 저배터리 경보(HW 제약)/BLE 연결끊김 경보/경보
     로깅 3건 — **오늘 범위에서 제외**(사용자 확정, HW 제약 4건과 함께 보류 유지)
   - **데이터 무결성 및 필수성능 보호**
     - CRC/체크섬 — **wire 프레임(DATA0/DATA1) 추가는 보류로 결정**(당시 근거: 8바이트
       고정 프레임에 여유가 없어 앱 프로토콜 갱신 없이는 추가 불가능). BLE 링크레이어가
       모든 패킷에 CRC24를 이미 적용하므로(Bluetooth Core Spec, 전송 구간 무결성은
       기존에 확보됨) 앱 레이어 CRC는 추가 프로토콜 버전에서 재검토.
       **2026-09-21 참고**: 프레임이 v2(16바이트, 아래 재연결 후 gap 식별 항목 참고)로
       확장되면서 "8바이트라 여유 없다"는 전제 자체는 더 이상 유효하지 않다 — CRC 추가가
       다시 필요해지면 이번에 이미 앱 프로토콜을 갱신했으므로 재검토 부담이 줄었다.
       다만 CRC 자체를 요청받은 적은 없어 이번 범위에는 포함하지 않음.
     - 센서 raw 값 물리적 sanity check — **코드 작성 완료**(`m_i2c.c
       check_sensor_sanity()`가 기존 미사용 상태였던 `MODULE_ERR_SENSOR_SATURATION`/
       `LOW_SIGNAL`을 실제로 판정 — CHANGELOG v0.0.14 §4). 임계값은 보수적 고정값
       (`config_app.h`)이며, 채널별/게인별 정확한 풀스케일 계산은 기존 gain/ATIME 실측
       캘리브레이션 open-item(§11 항목5)과 함께 재확정 필요.
       **2026-09-18 확인**: 코드는 있으나 SATURATION/LOW_SIGNAL/ring buffer overflow
       모두 실기에서 한 번도 강제로 재현/확인된 적이 없었음 — RTT에서 확인 가능하게
       로그 추가(`m_i2c.c`의 sanity 실패/overflow 지점, v0.1.10)
       (LOW_SIGNAL/overflow는 센서 가림·BLE 연결 끊고 대기로 물리 재현 가능,
       SATURATION은 강한 광원 필요 — 재현 안 되면 코드 리뷰로 대체 후 그 사실을 기록).
       **실기 검증은 SWD 연결 불안정으로 2026-09-18에 다음으로 연기** — 항목5-[2]와
       동일한 사유, 코드/로그는 준비된 상태.
     - 재연결 후 데이터 gap 식별 — **정정(2026-09-17): 여전히 미구현**. `nirs_sample_t.seq_num`을
       부팅 세션 내내 리셋 없이 단조증가로 유지하도록 변경(`reset_to_device_on()`의
       `s_seq_num = 0` 제거)까지는 했으나, **`m_ble_proto_encode_sample()`이 만드는
       DATA0/DATA1 8바이트 프레임에는 raw 3값+led_index만 들어가고 seq_num 자체가
       전송되지 않는다**(이건 오늘 변경과 무관하게 원래부터 그랬음) — 앱이 seq_num을
       볼 방법이 없어 gap을 실질적으로 감지할 수 없다. "방식 확정"이라는 이전 기록은
       인프라 없이 개념만 세운 것으로, 실제로는 미구현 상태다.
       TODO(open-item): AS7341_VERSION(0x1528)처럼 seq_num(+timestamp)을 노출하는
       추가 read/notify characteristic이 필요 — 기존 DATA0/DATA1 프레임 자체를
       바꾸는 것은 테스트 APK 호환성 문제로 비권장.
       **구현 완료(2026-09-17)**: AS7341_SEQ(0x1529, notify, u32 LE) 신설, DATA0/DATA1과
       같은 tick에서 함께 notify. **실기 확인된 설계적 한계**: BLE Notification은
       ATT 레벨에서 ACK가 없는 fire-and-forget이라, 공중에서 개별 notify 패킷이
       가끔 유실될 수 있다(`bt_gatt_notify()` 자체는 성공을 반환) — 이 경우 SEQ
       값이 앱에서 불규칙하게(관찰상 +2 정도, 링버퍼 overflow 로그 없음) 건너뛰는데,
       이는 **진짜 데이터 유실이 아니라 SEQ notify 패킷 하나만 전달 안 된 것**이다.
       즉 이 방식은 "ring buffer overflow로 인한 진짜 gap"과 "notify 자체의 무해한
       전달 실패"를 구분하지 못한다 — 둘 다 앱 입장에서는 seq_num 불연속으로 동일하게
       보인다. 더 정확한 구분이 필요해지면 DATA0/DATA1 notify 자체도 실패했는지
       상호 대조하거나, Indication(ACK 필요)으로 전환하는 방안을 검토한다(TODO).
       **근본 해결(2026-09-21, CHANGELOG v0.1.18)**: DATA0/DATA1 프레임 자체를 8→16바이트로
       확장해 timestamp_us(u32 LE)/seq_num(u32 LE)을 직접 포함(NCS_TedreamS2 관례 계승,
       `m_ble_proto.h` 프로토콜 스펙 참고) — 위에서 "비권장"이라 했던 프레임 변경을
       사용자 결정으로 결국 진행했다(앱을 함께 관리하므로 호환성 부담을 사용자가
       직접 감수). 이제 DATA0/DATA1 notify 자체에 seq_num이 있어 SEQ 스트림과 별도로
       대조할 필요가 없어졌지만, 위에서 설명한 "notify 자체의 무해한 전달 실패" 한계
       (ACK 없는 fire-and-forget)는 프레임 확장과 무관하게 그대로 남아있다 — DATA0/DATA1
       notify 하나가 유실되면 그 샘플의 seq_num도 함께 유실되므로, 여전히 "ring buffer
       overflow로 인한 진짜 gap"과 "notify 전달 실패"를 앱이 자체적으로 구분하려면
       `AS7341_DROPPED_COUNT`(아래) 대조가 필요하다. `AS7341_SEQ`(0x1529)는 하위 호환을
       위해 제거하지 않고 유지.
       **v3 배칭(2026-09-21, CHANGELOG v0.1.19)**: DATA0/DATA1이 다시 배칭 프레임
       (`[count][record×N]`)으로 바뀌면서 "notify 하나 유실 = 그 안의 여러 샘플이
       한꺼번에 유실"로 위험 단위가 커졌다(최대 17샘플/notify) — 배칭 도입의 트레이드
       오프로 인지해야 함. 이 변경으로 `AS7341_SEQ` notify 호출 자체는 중단(레코드에
       이미 seq_num이 있어 중복) — characteristic 선언은 유지.
       **인프라 추가(2026-09-18, FW 단독)**: `m_i2c_ring_buffer_get_dropped_count()`가
       구현만 되고 아무도 호출하지 않던 상태였음을 발견 — AS7341_DROPPED_COUNT(0x152A,
       read-only, u32 LE) characteristic으로 노출했다(VERSION/SEQ와 동일한 순수 추가
       패턴, 구버전 앱 호환성 영향 없음). 앱이 이 값을 SEQ 불연속 시점과 대조하면 두
       경우를 구분할 수 있으나, **실제 활용(폴링/대조 로직)은 앱 쪽 작업이라 이번
       범위는 인프라 제공까지**다. Indication 전환은 여전히 TODO로 남긴다(앱이 CCCD에
       indicate bit를 실제로 요청하는지 확인 안 된 상태라 FW 단독으로 확정 짓지 않음).
   - **무선통신 및 보안 (14.13 IT-네트워크 요구사항)**
     - BLE 페어링/본딩·암호화 — **설계만 준비, 비활성 상태로 커밋**(`prj.conf`에
       `CONFIG_BT_SMP`/`BONDABLE` 주석 처리). 활성화 시 `m_ble_gatt.c`의 CONFIG/DATA0/
       DATA1 permission을 encrypt-required로 바꿔야 실제로 암호화가 강제된다. **활성화
       조건**: 테스트 APK가 본딩을 지원하는지 확인 후 — 미확인 상태에서 켜면 이미
       검증된 연동(v0.0.12)이 깨질 위험이 있어 사용자가 오늘 범위에서 보류하기로 확정.
       **2026-09-21 경과**: 같은 날 한 차례 활성화했다가(v0.1.15) 사용자 재확인 결과
       "지금 단계에서는 필요 없다, 끊긴 뒤 advertising 재개로 재연결만 되면 충분하다"는
       판단으로 즉시 롤백(v0.1.16) — 그 재연결 동작은 본딩과 무관하게 기존 구조
       (`m_ble.c on_disconnected()`)로 이미 충족되고 있었다. 본딩은 다시 보류 상태로
       복귀, 앱 본딩 지원 확인이라는 원래 활성화 조건도 그대로 유효하다.
     - 통신 프로토콜 버전 관리 — **버전 노출만 구현, 협상/거부 로직은 인프라 없음**.
       AS7341_VERSION characteristic(0x1528, read-only) 신규 추가로 `fw_version`+
       `protocol_version`(`BLE_PROTOCOL_VERSION=1`)을 읽을 수 있게 함(순수 추가, 기존
       characteristic 호환성 영향 없음). 구버전 거부/호환모드는 앱이 이 값을 읽고
       대응하도록 업데이트돼야 실효성이 생기며, 테스트 APK는 이 characteristic 자체를
       모른다 — 앱 갱신 전까지는 "읽을 수 있다"는 인프라 단계.
       **2026-09-21(CHANGELOG v0.1.18)**: `BLE_PROTOCOL_VERSION` 1→2 — DATA0/DATA1
       프레임이 8→16바이트로 바뀌면서(위 "재연결 후 데이터 gap 식별" 항목 참고) 처음으로
       이 버전 값이 실제로 의미를 갖게 됐다. 다만 구버전 거부/협상 로직은 여전히 인프라
       없음 상태 그대로 — 앱이 이 프레임 길이 변경에 맞춰 파서를 업데이트해야 한다.
       **2026-09-21(같은 날, CHANGELOG v0.1.19)**: 배칭 도입으로 `BLE_PROTOCOL_VERSION`
       2→3 — DATA0/DATA1이 고정 16바이트에서 `[count][record×N]` 가변 길이 배칭
       프레임으로 다시 바뀌었다(§2.4/§2.5, m_ble_proto.h v3 스펙). 앱이 또 한 번
       파서를 업데이트해야 한다.
   - **형상관리 및 식별**
     - 펌웨어 버전 식별 기능 — **정정(2026-09-17)**: 이전에 "구현 완료(매 샘플에 포함되어
       앱에 전달됨)"로 잘못 기록했다. 실제로는 `fw_version`이 `nirs_sample_t`에만
       있었고 `m_ble_proto_encode_sample()`은 raw 3값+led_index만 인코드해서 **BLE로
       전송되지 않고 있었다**. 위 AS7341_VERSION characteristic 추가로 바로잡음 — 이제야
       진짜로 앱이 조회 가능.
     - SOUP(nRF SDK/SoftDevice/BLE 스택/RTOS 버전) 목록·anomaly list 검토 — 펌웨어 코드
       아님, 별도 문서화 작업, 보류 유지
     - OTA 업데이트 실 로직 — **2026-09-17 실기 업데이트(업그레이드/다운그레이드) 검증
       완료**. `prj.conf`에 `CONFIG_MCUMGR`/`CONFIG_MCUMGR_TRANSPORT_BT`/
       `CONFIG_IMG_MANAGER` 등 활성화. **`pm_static.yml`은 신규 작성하지 않기로
       계획을 변경**했다 — CHANGELOG v0.0.1/v0.0.11에 이미 sysbuild 자동 파티션
       매니저가 MCUboot 2-image 빌드를 정상 생성해온 이력이 있어(App Flash
       32564B/216752B, mcuboot Flash 34768B/48KB), 검증 안 된 손 계산 파티션 오프셋을
       도입하는 게 CLAUDE.md §4/§14가 경고하는 브릭 위험을 오히려 키운다고 판단, 자동
       파티셔닝에 맡김. 서명키는 sysbuild 기본 디버그 키.
       초기 시도에서 "GATT ERROR"(SMP 패킷 재조립 `MCUMGR_TRANSPORT_BT_REASSEMBLY`
       누락) 발생 → NCS 공식 검증 번들(`NCS_SAMPLE_MCUMGR_BT_OTA_DFU`/`_SPEEDUP`)로
       교체해 해결, 단 **"현재 이미 설치된 이미지가 OTA를 받을 능력이 있어야" 하므로
       이 수정을 반영한 이미지를 최초 1회는 반드시 유선으로 플래시**해야 했음(교훈,
       CHANGELOG v0.0.17 참고). 이후 nRF Connect Device Manager로 무선 업그레이드
       (v0.0.18→v0.1.1) 및 **다운그레이드(v0.1.1→v0.1.0) 모두 실기 성공** 확인
       (RTT 로그로 `Board FW Version` 실측 확인).
       **다운그레이드 정책(2026-09-17 사용자 결정)**: `CONFIG_MCUBOOT_
       DOWNGRADE_PREVENTION`이 비활성이라 다운그레이드가 허용되는 현재 상태를
       인증/테스트 단계에서는 **의도적으로 유지**한다(이전 버전으로 되돌려
       재현/비교해야 할 필요가 있음). **양산 배포 시점에 이 옵션을 활성화하는 것은
       사용자가 별도로 진행**하기로 함 — 이 프로젝트가 정식 승인되기 전까지는
       다운그레이드 방지가 꺼진 상태임을 팀이 인지하고 있어야 한다.
       **2026-09-17 추가 실기 검증**: 업데이트 전송 도중 강제 전원차단 시나리오 —
       MCUboot가 손상된 이미지로 스왑하지 않고 원래 버전으로 안전하게 롤백 부팅,
       재부팅 후 BLE 자동 재연결로 OTA 이어서 진행 가능함을 확인("Swap Without
       Scratch" 원자적 스왑 안전성 실증).
       **2026-09-18 추가 실기 검증(케이스 A, 서명 깨진 이미지 거부)**: 정상 서명된
       `zephyr.signed.bin`의 payload 중간 1바이트를 변조한 사본을 OTA로 업로드 →
       재부팅 후 `Board FW Version`과 git hash가 업데이트 전과 완전히 동일(v0.1.5-
       45c7b088fdec), I2C/BLE init도 전부 정상 — **MCUboot가 서명 검증에서 거부하고
       swap을 진행하지 않은 것으로 판단**(MCUboot 자체 부팅 로그 캡처는 못 해서
       100% 확정은 아니고, 정황 증거 기준 확인).
       **2026-09-18 시도(케이스 B, confirm 없는 test 이미지 자동 롤백)**: v0.1.8을
       test로 업로드 후 confirm 없이 리셋해서 RTT로 확인 시도. 1차 시도에서는 RTT
       캡처 중 1차 부팅(v0.1.8이어야 할 부팅)의 `Board FW Version` 배너 자체가 깨져서
       "1차에서 v0.1.8이 떴다가 롤백된 것"과 "애초에 swap이 안 일어난 것"을 로그만으로
       구분할 수 없었다. **이후 Test 업데이트를 반복 실행하면서 매번 동일한 패턴
       (재부팅 후 이전 버전으로 복귀)이 일관되게 재현됨을 확인 — 사용자 판단으로
       검증 완료(100%) 처리.** 다만 RTT 로그의 부팅 배너 자체를 깨끗하게 캡처하지는
       못했으므로 **정식 자동화 TC(Test Case) 기반 검증은 양산 단계에서 별도 추가
       예정** — 지금은 반복 관찰 기반 확인 수준임을 인지하고 있어야 한다.
   - 근거: `fNIRS_FW_v1.0_트래커.html`이 아닌 별도 일일 업무일지(`일일 업무 일지_2609(3W_4D)_Wade HAN.docx`,
     2026-09-17)에 ⓪ 이번 주 마일스톤 No.5(시료준비)/No.6(안전기능)으로 최초 기록.
     제외된 4개 항목(배터리 안전종료, 충전중 착용, 과온방어, 경보 시스템 3건)은 계속 보류.

이 6개 항목이 해소되기 전까지 본 문서는 정식 승인 문서로 간주하지 않는다.

7. **테스트 APK가 BLE 연결 해제 후 자동 재연결을 시도하지 않음 (2026-09-18 확인, 펌웨어 이슈 아님)**
   — RTT 로그로 확인: disconnect 즉시 펌웨어는 advertising을 정상적으로 재시작한다
   (`m_ble.c on_disconnected()`/`start_advertising()`). 그런데 앱이 재연결을 자동으로
   시도하지 않아서, 사용자가 매번 항목6-[3]의 30초 grace period(`BLE_DISCONNECT_
   STANDBY_TIMEOUT_TICKS`, 로컬 버퍼링 지속 후 STANDBY 전환)를 그대로 체감하게 된다 —
   처음엔 "disconnect 시 LED가 순차 점등으로 안 돌아온다"는 펌웨어 버그로 오인했으나,
   실제로는 앱이 재연결을 안 해서 grace period가 끝까지 진행되는 것이었다.
   **결정(2026-09-18)**: 펌웨어 동작(끊겨도 30초간 측정+버퍼링 지속 후 STANDBY 전환)은
   그대로 유지한다 — 짧은 끊김에도 데이터 유실을 막기 위한 의도된 설계이므로 앱의
   재연결 지연을 이유로 되돌리지 않는다. TODO(open-item): 테스트 APK에 BLE 자동
   재연결 로직 추가 필요(앱 쪽 작업, 펌웨어 범위 아님).
   **해결(2026-09-21, 앱 쪽 검증 완료)**: 사용자가 앱에 "disconnect 이벤트 감지 시
   자동 reconnecting 대기" 로직을 구현. 1차로 디바이스 전원을 꺼서 강제로 연결을 끊은
   뒤 전원을 다시 켜서 앱이 자동 재연동하는 것을 확인 — 이건 펌웨어의 두 advertising
   재시작 경로 중 부팅 시 경로(`m_ble.c ble_init()` → `start_advertising()`)만 검증한
   것이었다. **이후 휴대폰 BT 토글로 디바이스는 켜둔 채 BLE 링크만 끊는 런타임
   경로(`on_disconnected()` → `s_adv_restart_work`)도 추가 검증 완료** — 두 경로 모두
   앱 자동 재연결이 정상 동작함을 확인, 이 항목 완전 해소.
   **grace period 값 변경 참고(2026-09-21, CHANGELOG v0.1.17)**: 위 문단의 "30초"는
   당시 기준이고, RAM 예산 재검토로 현재는 **22초**(`BLE_DISCONNECT_STANDBY_TIMEOUT_
   TICKS=220`)로 바뀌었다.

8. **부팅 진단 관련 정책 변경 (2026-09-22, CHANGELOG v0.1.21)**
   — 부팅 버전 점멸 횟수를 `(FW_VERSION_PATCH+1)`회(§6 항목6-[6] 도입 당시 목적:
   점멸 횟수로 OTA 적용 여부 육안 확인)에서 **고정 3회**로 변경했다. 패치 버전이
   오를수록 점멸 시간(=부팅 블로킹 구간)이 계속 길어져 watchdog 타이밍과 충돌하는
   고위험 버그로 실제 이어졌던 구조(v0.1.13→v0.1.14)라, 이 클래스의 버그 자체를
   근본적으로 제거하기 위한 결정이다. **부수 효과: 점멸 횟수로 OTA 버전 변경을 육안
   확인하던 방법은 더 이상 쓸 수 없다** — 대신 BLE `AS7341_VERSION`
   characteristic(0x1528)으로 `fw_version`을 읽는 방식을 쓴다(앱이 이미 지원).
   — 사용자가 "SW1을 눌러도 한 번에 켜지지 않는 보드"를 발견해, `main()`에 진단용
   LED1/LED2 강제 점등을 추가했다. `agents.md`("main()은 태스크를 생성/초기화하지
   않는다")와 `m_i2c_led.h`("I2C Task 컨텍스트에서만 호출") 원칙의 **의도적 예외**
   — main()은 어떤 태스크 init 성공 여부와 무관하게 커널 스케줄링 시작 시 반드시
   도달하므로, "전원+부팅 자체는 성공"과 "태스크 init 단계에서 멈춤"을 구분하는
   유일한 방법이다. main() 시점엔 AS7341 측정이 없어 실측 정확도와 충돌하지 않음.
   — BLE 디바이스 이름을 `nRF_fNIRS_Sys_v2`(고정)에서 `TedNeuro_v{MAJOR}.{MINOR}.
   {PATCH}` 형식으로 변경 — 앱이 스캔/연결 시 이름만으로 펌웨어 버전을 구분할 수
   있게 함. Kconfig 문자열이라 자동 대입이 안 돼, **매 패치 버전 상승마다 VERSION
   파일/`config_app.h`와 함께 수동 갱신하는 정책**으로 확정(`prj.conf`에 명시).

### 다음 보드 리비전 반영 예정 (하드웨어 개선 항목, 확정됨 — 일정만 대기)
- **LED3 파장 부품 교체**: 스펙(§0/§5/§9)은 950nm이나, PoC v1 보드(`SCH_fNIRS_Sleep_Project.pdf`) 실장 부품은
  **MTE9730CP(λp=980nm)**로 확인됨 (`pinmap.md` §7 참고). 950nm이 원래 스펙이 맞고, 980nm은 PoC v1의
  임시/오차 실장이다. **다음 PCB 리비전에서 950nm 부품으로 교체**한다. 그 전까지 PoC v1로 진행하는 모든
  측정/검증은 실제 LED3 파장이 980nm이라는 점을 감안해서 해석한다 (펌웨어 코드/패킷 필드명은 그대로
  "950nm"으로 유지 — 소프트웨어가 실제 파장을 알 필요는 없고 raw intensity만 보존하면 되므로).
- **온도센서 부재 (2026-09-16 확인, §7 관련)**: PoC v1 보드에 NTC/디지털 온도센서가 전혀 없다 — §7의
  Thermal 상태머신(Normal→Warning→Duty감소→OFF)은 현재 보드로는 구현 불가능하다. **다음 PCB 리비전에서
  LED 근처(피부 접촉부에 가까운 위치)에 온도센서를 추가**해야 한다. 참고: nRF52832 내장 다이(die) 온도센서는
  MCU 칩 자체 온도만 측정하므로 LED/피부 접촉부 온도의 대체재로 쓰기에 부적절해 채택하지 않는다.
- **배터리 전압/잔량 센싱 경로 부재 (2026-09-16 확인, §8 관련)**: PoC v1 보드에 fuel gauge/monitor IC가
  없고, nRF52832의 ADC 가능 핀(P0.02~P0.05/AIN0~AIN3)이 전부 LED 제어 신호로 점유돼 있어 저항분배로
  VBAT를 측정할 여유 ADC 핀도 없다. **다음 PCB 리비전에서 배터리 전압 감지용 저항분배 + 여유 ADC 핀
  (또는 fuel gauge IC)을 추가**해야 한다. 그 전까지 `nirs_sample_t.battery_pct`는 0 고정을 유지한다.

---

## 12. 한 줄 요약

fNIRS 양산형 프로젝트는 **신규 개발**이되, **S1의 구조(태스크 분할·상태관리·네이밍)와 S2의 프로토콜 관례(BLE 정책·타임스탬프·OTA)를 계승**하고, OS는 **Zephyr(NCS) + HW RTC 하이브리드**로 확정한다. **현 단계 최우선 목표는 파장별(640/680/950nm)로 분리된 정확한 raw 데이터를 AS7341로부터 I2C로 안정적으로 읽어오는 것**이며, LED는 PWM 제어, 배터리 목표는 8h로 확정되었다. Rev0(구조 이식)~Rev3(신뢰성, OTA 포함)까지 확보한 뒤 일정을 재산정한다.
