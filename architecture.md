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

- Tx power **-8dBm 고정**, 전 패킷 **32bit us 타임스탬프** — S2 정책 그대로 계승
- Connection interval은 S2 선례 범위(7.5~30ms) 내에서 실시간성/배터리 트레이드오프를 실측 후 확정
- MTU는 BLE 5.0 Extended Length 기준 최대 251byte를 목표로 협상, 실패 시 기본 23byte로 폴백하는 경로도 구현
- Batch size는 패킷헤더+3파장raw+seq+timestamp(샘플당 약 16~20byte) 기준, 협상된 MTU 페이로드 안에 들어가는 최대 샘플 수를 역산해 결정 (예: payload 244byte 기준 약 12~15샘플/패킷) — 정확한 실측치는 Rev2 착수 시 확정

### 2.5 Ring Buffer / Flash Logging 구조 — ✅ S2 계승

- Ring buffer 크기: 10Hz 기준 BLE 지연 5~10초를 버틸 수 있는 50~100샘플에 안전마진을 더해 **100~200샘플**로 설계
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

## 8. 배터리 Runtime 목표 — ✅ 확정: 8h

**계산 근거 (논문 기반)**:
- 배터리: 110mAh × 3.7V = 407mWh
- 논문 평균 소비전력: 70~75mW → 실측 재현 약 5.4~5.8h (논문 보고 5.5h와 일치)
- 8h 달성에 필요한 평균 소비전력: 407mWh / 8h ≈ **51mW** (논문 대비 약 30% 절감 필요)
- LED PWM duty 제어(연속점등 → 구간점등 전환)만으로 달성 가능한 범위로 판단 — Rev6(Power Optimization)에서 실측 튜닝
- 10h(≈41mW, 약 44% 절감)는 배터리 용량 변경 없이는 공격적이므로 1차 목표에서 제외, Rev6 실측 후 스트레치 목표로 재검토

배터리 **용량**(현재 110mAh 유지 여부), DFU 방식 사양, calibration 저장 구조, 과충전/과방전(UVLO) 보호 회로 인터페이스는 여전히 **보류**다 (§11).

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

이 5개 항목이 해소되기 전까지 본 문서는 정식 승인 문서로 간주하지 않는다.

### 다음 보드 리비전 반영 예정 (하드웨어 개선 항목, 확정됨 — 일정만 대기)
- **LED3 파장 부품 교체**: 스펙(§0/§5/§9)은 950nm이나, PoC v1 보드(`SCH_fNIRS_Sleep_Project.pdf`) 실장 부품은
  **MTE9730CP(λp=980nm)**로 확인됨 (`pinmap.md` §7 참고). 950nm이 원래 스펙이 맞고, 980nm은 PoC v1의
  임시/오차 실장이다. **다음 PCB 리비전에서 950nm 부품으로 교체**한다. 그 전까지 PoC v1로 진행하는 모든
  측정/검증은 실제 LED3 파장이 980nm이라는 점을 감안해서 해석한다 (펌웨어 코드/패킷 필드명은 그대로
  "950nm"으로 유지 — 소프트웨어가 실제 파장을 알 필요는 없고 raw intensity만 보존하면 되므로).

---

## 12. 한 줄 요약

fNIRS 양산형 프로젝트는 **신규 개발**이되, **S1의 구조(태스크 분할·상태관리·네이밍)와 S2의 프로토콜 관례(BLE 정책·타임스탬프·OTA)를 계승**하고, OS는 **Zephyr(NCS) + HW RTC 하이브리드**로 확정한다. **현 단계 최우선 목표는 파장별(640/680/950nm)로 분리된 정확한 raw 데이터를 AS7341로부터 I2C로 안정적으로 읽어오는 것**이며, LED는 PWM 제어, 배터리 목표는 8h로 확정되었다. Rev0(구조 이식)~Rev3(신뢰성, OTA 포함)까지 확보한 뒤 일정을 재산정한다.
