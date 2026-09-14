# fNIRS 펌웨어 코딩 표준 (Coding Standard)

| 항목 | 내용 |
|---|---|
| 문서번호 | STD-001 |
| 개정번호 | Rev.1 |
| 적용 대상 | fNIRS 양산형 디바이스 펌웨어 (Zephyr/NCS) |
| 근거 | `TedreamS1`(구조) + `NCS_TedreamS2`(프로토콜/최신 네이밍) + 본 프로젝트 신규 결정 |
| 작성일 | 2026-09-15 |

> 아키텍처 배경은 [`architecture.md`](./architecture.md), AI 에이전트/개발자 공통 작업 지침은 [`agents.md`](./agents.md)를 함께 참고한다. 이 문서는 "어떻게 짜는가"에 대한 규칙이고, `architecture.md`는 "왜 이렇게 설계했는가"에 대한 근거다.

---

## 1. 네이밍 컨벤션

S2(`NCS_TedreamS2`)가 Rev20~21에서 이미 `module_x_*` → `m_x_*`로 리네이밍한 최신 관례를 채택한다. S1(`TedreamS1`)의 접두사 체계는 의미는 유지하되 표기만 S2식으로 갱신한다.

| 대상 | 규칙 | 예시 |
|---|---|---|
| 모듈 함수 | `m_<module>_<verb>_<noun>()` | `m_acq_start_sampling()`, `m_ble_send_packet()` |
| 전역 변수 | `g_<name>` | `g_ctrl_status` |
| 정적(파일 스코프) 변수 | `s_<name>` | `s_ring_buffer` |
| 모듈 스코프 정적 변수 (구모듈 상태 등) | `m_<name>` (변수) vs `m_<module>_*` (함수)로 문맥상 구분 — 신규 코드에서는 변수에 `s_`, 함수에 `m_<module>_`를 써서 혼동을 피한다 | `s_last_seq_num` |
| 세마포어/뮤텍스 | `sem_<name>`, `mtx_<name>` | `sem_acq_ready`, `mtx_ring_buffer` |
| 타입 정의 | `<name>_t` | `module_err_t`, `nirs_sample_t` |
| 에러코드 enum 값 | `MODULE_ERR_<NAME>` | `MODULE_ERR_I2C_TIMEOUT` |
| 매크로/상수 | `UPPER_SNAKE_CASE` | `RTC_TICK_TARGET_100MS` |
| 파일명 | `snake_case`, 모듈명과 동일 어간 | `m_acquisition.c`, `m_ble_tx.c` |

**디바이스 분기 매크로**(`DEVICE_TYPE` A/C/CS/H 등)는 S2 구조를 그대로 유지하되, fNIRS는 단일 기기이므로 현재는 값 하나만 사용한다. 향후 파생 모델 대비 매크로 골격은 지우지 않는다.

---

## 2. 모듈/디렉터리 구조

west workspace 하위 애플리케이션 트리는 기능 단위로 분리한다 (S1 폴더 구조를 그대로 복사하지 말고 아래처럼 재구성):

```
app/
├── drivers/          # AS7341 I2C, LED PWM 등 센서/구동 드라이버
├── acquisition/       # Acquisition Task, RTC ISR, ring buffer
├── protocol/          # 패킷 포맷, BLE TX task, batching, OTA(SMP)
├── ctrl/              # CTRL 상태머신, 에러코드, reset reason
├── power/             # battery/temperature monitor, Zephyr PM 연동
└── boards/            # 보드별 devicetree overlay
```

각 모듈은 `m_<module>.c/.h` 한 쌍을 기본으로 하고, 모듈이 커지면 `m_<module>_<sub>.c`로 분리한다 (예: `m_acq_i2c.c`, `m_acq_pwm.c`).

---

## 3. ISR 작성 규칙 (반드시 준수)

RTC(100ms) 등 인터럽트 서비스 루틴은 아래 항목만 허용한다:

- ✅ timestamp capture (레지스터/카운터 읽기)
- ✅ semaphore give / k_event set

아래는 **ISR 내부에서 절대 금지**한다:

- ❌ BLE notify / GATT 호출
- ❌ 로그 출력 (printk 등)
- ❌ flash write
- ❌ I2C/SPI 트랜잭션
- ❌ 신호처리/연산 (필터링, 변환 등)
- ❌ malloc / 동적 할당

코드 리뷰 시 ISR 함수(보통 `*_isr()` 또는 `*_handler()` 접미사)를 열었을 때 위 금지 항목이 있으면 **반드시 반려**한다.

---

## 4. 태스크 우선순위 & 동시성 규칙

- 우선순위: **Acquisition > BLE TX > Control/Command > Battery/Temperature > Logging/Diagnostics**
- 모든 태스크는 **event-driven**으로 작성한다 (semaphore/message queue 대기). `k_sleep()`으로 busy-loop을 흉내내는 polling 패턴은 금지.
- Acquisition Task와 BLE TX Task 사이의 유일한 통로는 **ring buffer**다. 서로 직접 함수 호출로 데이터를 넘기지 않는다 (결합도를 낮추고, BLE 지연이 acquisition을 막지 못하게 하기 위함).
- 공유자원(ring buffer 등)은 반드시 뮤텍스 또는 lock-free 큐로 보호한다. 이름은 `mtx_<name>` 규칙을 따른다.

---

## 5. 상태머신 / 에러 처리 패턴

- 모든 리셋/에러는 **CTRL 상태머신을 경유**한다. `NVIC_SystemReset()`, `sys_reboot()` 등을 CTRL 모듈 바깥에서 직접 호출하는 코드는 금지한다 (S1 원칙 계승).
- 에러코드는 `module_err_t` 하나의 타입으로 전 모듈에서 통일한다 (S2 Rev22 패턴). 모듈별로 별도 에러 enum을 새로 만들지 않는다.
- 상태 전이는 `Normal → Warning → Degraded → Fault` 형태의 공통 패턴을 따르고, Thermal Safety(§7)도 동일 패턴(`Normal → Warning → Duty감소 → OFF`)을 사용한다.
- 리셋 원인(reset reason)은 watchdog/전원/소프트웨어 등으로 구분해 기록한다. GPREGRET 등을 이용한 상태 보존 적용 여부는 항목별로 결정하고 문서에 남긴다 (예: Thermal Warning 상태 보존 여부는 `architecture.md` §11 참고).

---

## 6. 데이터/패킷 규칙

- **RAW 보존 원칙**: MCU는 3파장(640/680/950nm) raw intensity를 가공 없이 그대로 전송/저장한다. Δ[H2O] 등 파생값 계산은 PC/Server 몫이다. 펌웨어에서 임의로 필터링·보정한 값만 내보내는 코드는 금지한다.
- 패킷 필드 순서와 타입은 `architecture.md` §4의 표를 그대로 따른다 (timestamp 32bit us, seq, raw×3, gain/integration, LED duty, battery, status, fw version).
- Gain/Integration Time은 초기 버전에서 **고정값**을 사용한다. 런타임에 값이 바뀌는 코드를 작성해야 한다면, 반드시 변경 시점과 값을 패킷/로그에 함께 기록하는 코드를 같이 추가한다 (누락 시 리뷰 반려 사유).
- Diagnostic telemetry(카운터류)는 오버플로/에러 발생 시 **silent하게 버리지 않는다** — 반드시 `dropped_sample_count` 등 관련 카운터를 증가시키거나 flag를 설정하는 코드가 함께 있어야 한다.

---

## 7. 문서화 규칙 (코드와 항상 짝을 이룬다)

이 프로젝트는 "코드 + 문서 + 타이밍 체크"를 매일의 기본 작업 단위로 삼는다 (트래커의 Day Plan 셀프 체크 참고). 코드를 작성할 때 아래를 항상 같이 남긴다:

- **모듈 헤더 주석**: 이 모듈이 속한 Rev, 담당 태스크, 우선순위, 의존 모듈
- **함수 주석**: ISR에서 호출 가능 여부, 블로킹 여부, 어떤 태스크 컨텍스트에서 호출되어야 하는지
- **타이밍 관련 코드**(RTC 보정, PWM 시퀀싱, BLE batching 등)를 수정하면, 반드시 실측 방법과 결과를 커밋 메시지 또는 `docs/` 하위 노트에 남긴다 (예: "RTC 5-frame 보정 적용 후 1시간 누적 drift: OO us" 같은 실측 기록).
- 새 미확정 항목이 발견되면 임의로 판단하지 말고 `architecture.md` §11에 추가하고 코드에는 `// TODO(open-item): ...` 형태로 표시한다.

---

## 8. 커밋 / 브랜치 규칙

- 커밋 메시지는 `[Rev단계] 짧은 요약` 형식을 권장한다. 예: `[Rev1] AS7341 I2C 드라이버 초기 포팅`
- **커밋 메시지와 실제 diff가 반드시 일치해야 한다.** S2 히스토리 분석 중 메시지와 diff가 어긋난 사례(v0.1.4, v0.1.8 등)가 실제로 발견되었으므로, 리뷰어는 메시지만 보고 승인하지 않고 diff를 직접 확인한다.
- 브랜치명: `rev0/...`, `rev1/...`처럼 현재 Rev 단계를 접두어로 둔다.
- Rev 전환(예: Rev1→Rev2) 시점에는 태그를 남긴다: `rev1-complete`, `rev2-complete` 등.

---

## 9. 코드 리뷰 체크리스트 (AT 항목과 연결)

PR을 올리기 전, 아래를 스스로 점검한다 (해당 사항이 있는 경우):

- [ ] ISR에 금지 항목(§3)이 없는가?
- [ ] 태스크 우선순위(§4)를 지켰는가? Acquisition이 BLE보다 우선하는가?
- [ ] 리셋/에러가 CTRL 상태머신을 경유하는가(§5)? `module_err_t`를 썼는가?
- [ ] Raw 데이터를 보존하는가(§6)? Gain/Integration 변경 시점을 기록하는가?
- [ ] 관련 AT 항목(`architecture.md` §10)이 있다면 그 기준을 만족하는 근거(로그/테스트 결과)가 있는가?
- [ ] 새로 발견한 미확정 사항을 `architecture.md` §11에 반영했는가?
