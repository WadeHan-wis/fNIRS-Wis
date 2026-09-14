# AGENTS.md — fNIRS 양산형 펌웨어 저장소 작업 지침

이 문서는 이 저장소에서 작업하는 AI 코딩 에이전트(Claude Code 등)와 개발자가 공통으로 참고하는 최상위 안내서다. 세부 아키텍처 근거는 [`architecture.md`](./architecture.md), 코딩 규칙은 [`codingstandard.md`](./codingstandard.md)에 있다. **이 문서만 읽고 바로 코드를 작성하지 말고, 관련 작업이면 반드시 위 두 문서의 해당 절을 먼저 확인한다.**

---

## 1. 프로젝트 한 줄 요약

fNIRS(근적외선 분광법 기반 뇌혈류 모니터링) **양산형** 웨어러블 디바이스의 펌웨어. Zephyr(NCS) 기반 신규 개발이며, 사내 `TedreamS1`(구조)과 `NCS_TedreamS2`(프로토콜 관례)를 계승한다. 기준 논문: Ban et al., Sci. Adv. 12, eaad2056 (2026).

## 2. 지금 가장 먼저 확인할 것

- **현재 최우선 목표(§0)**: 640/680/950nm 3파장 LED 데이터를 분리해서 AS7341로부터 I2C로 정확하게 읽어오는 것. 이게 검증되기 전에는 batching/전력최적화/thermal 세부값 등 후순위 항목에 시간을 쓰지 않는다.
- **현재 Rev 단계와 진행률**: 프로젝트 트래커(`fNIRS_FW_v1.0_트래커.html`)의 "Task 체크리스트"·"Day Plan" 탭에서 확인한다. 이 저장소에 작업을 추가하기 전에 지금이 Rev0~3 중 어디인지, 오늘 배정된 Day의 "목표 산출물"이 무엇인지부터 확인한다.
- **미확정 항목**: `architecture.md` §11에 아직 팀 결정이 안 난 4개 항목(배터리 용량/DFU/calibration, 생산검사 모드, 규제 요구사항, 논문 모순 2건)이 있다. 이 항목들과 관련된 코드는 **임의로 구현하지 말고** 보류 상태를 유지한다.

## 3. 반드시 지키는 아키텍처 규칙 (요약 — 근거는 architecture.md)

1. Acquisition Task와 BLE TX Task는 반드시 분리하고, 서로 ring buffer로만 통신한다. BLE 지연이 acquisition 타이밍에 영향을 주면 안 된다.
2. ISR(특히 RTC 100ms)에는 timestamp capture + semaphore/event set만 넣는다. BLE/로그/flash/I2C/malloc은 절대 금지 (`codingstandard.md` §3).
3. 태스크 우선순위: Acquisition > BLE TX > Control > Battery/Temperature > Logging.
4. 모든 리셋/에러는 CTRL 상태머신을 경유한다. `module_err_t` 하나로 에러코드를 통일한다.
5. MCU는 3파장 raw intensity를 가공 없이 보존한다. Δ[H2O] 같은 파생값 계산은 펌웨어 책임이 아니다.
6. LED는 PWM duty 제어, AS7341 gain/integration은 초기엔 고정값 (변경 시 반드시 로그에 시점 기록).
7. OTA(MCUboot+SMP)는 v1.1로 미루는 기능이 아니라 **v1.0 필수 사양**이다. Rev2에서 구현한다.
8. RTC 100ms는 nrfx RTC 드라이버를 직접 써서 만들고, 5-frame Bresenham 보정으로 drift를 관리한다 (`architecture.md` §2.3).

## 4. 하지 말아야 할 것 (Don'ts)

- ❌ S1/S2 코드를 그대로 복붙하지 않는다 — 반드시 diff를 직접 확인하고 fNIRS 아키텍처 원칙에 맞게 재해석한다 (특히 S2는 커밋 메시지와 diff가 불일치하는 사례가 실제로 있었다).
- ❌ 논문 내 모순 2건(sleep threshold 방향성, spectral band 서술)과 관련된 알고리즘을 펌웨어나 서버 어디에도 구현하지 않는다. 저자 확인 전까지 보류.
- ❌ Thermal threshold/hysteresis, 배터리 용량, 생산검사 모드, 규제 요구사항을 임의의 "합리적으로 보이는 값"으로 확정하지 않는다 — 미확정 항목으로 남기고 팀 결정을 기다린다.
- ❌ Gain/Integration Time을 런타임에 바꾸면서 변경 시점 기록을 빠뜨리지 않는다.
- ❌ ISR 안에서 블로킹 호출(I2C, BLE, flash)을 하지 않는다.
- ❌ 커밋 메시지만 그럴듯하게 쓰고 실제 diff와 다르게 만들지 않는다 (리뷰어가 diff로 재검증한다는 전제).

## 5. 빌드 / 테스트 (west/NCS 기준 — 실제 저장소 설정에 맞춰 조정)

```bash
# 최초 1회: west workspace 초기화 (west.yml 기준)
west init -m <manifest-repo-url> --mr main
west update

# 빌드 (보드명은 devicetree overlay에 맞게 교체)
west build -b <board_name> app/

# 플래시
west flash

# 시리얼 로그 확인
west build -t debug   # 또는 사용 중인 터미널 도구(예: nrfutil, JLinkRTTViewer)
```

> 위 명령은 일반적인 NCS/west 워크플로 기준 템플릿이다. 실제 west.yml, 보드 이름, 빌드 타겟은 이 저장소의 `west.yml`/`CMakeLists.txt`를 확인해서 맞춰 쓴다.

## 6. 코드 작성 후 자체 점검

PR 만들기 전에 `codingstandard.md` §9 체크리스트를 확인한다. 특히:

- 오늘 작업이 트래커 Day Plan의 "목표 산출물"과 대응되는가?
- 관련 AT 항목(`architecture.md` §10)이 있다면 그 근거(로그/테스트/커밋 링크)를 남겼는가?
- 새로 발견한 미확정 사항이 있으면 `architecture.md` §11에 반영을 제안했는가?

## 7. 문서 갱신 규칙

- 아키텍처 결정이 바뀌면 `architecture.md`를 갱신하고, 이 문서(`agents.md`)의 §3/§4도 함께 갱신한다 (오래된 "하지 말 것"이 남아있지 않도록).
- 코딩 규칙이 바뀌면 `codingstandard.md`만 갱신하면 되고, 이 문서는 그대로 둔다 (이 문서는 요약본 역할만 한다).
- 세 문서(`agents.md`, `architecture.md`, `codingstandard.md`)의 개정번호는 독립적으로 관리한다.
