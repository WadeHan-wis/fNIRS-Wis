# Project-Specific Firmware Context (fNIRS / Zephyr-NCS)

이 문서는 **이 fNIRS 양산형 펌웨어 프로젝트에만** 적용되는 규칙이다.
근거 문서는 [`agents.md`](./agents.md)(작업 지침 요약), [`architecture.md`](./architecture.md)(설계 근거),
[`codingstandard.md`](./codingstandard.md)(코딩 규칙)이며, 이 문서는 그 세 문서를 대체하지 않고
에이전트 작업 방식 관점에서 보충한다. 내용이 서로 어긋나면 세 문서가 우선한다.

> 이 프로젝트는 **Zephyr(NCS)** 기반이다. 사내 `TedreamS1`(nRF5 SDK+FreeRTOS+S132 SoftDevice)이나
> `NCS_TedreamS2`의 세부 구현을 그대로 옮겨 적지 않는다 (agents.md §4 "S1/S2 코드를 그대로 복붙하지 않는다").

---

## 1. 작업 순서

모든 사용자向 커뮤니케이션은 별도 요청이 없는 한 한국어로 한다.

작업 시작 전:

1. 관련 문서(agents.md/architecture.md/codingstandard.md, 필요시 트래커)를 읽는다.
2. 영향받는 소스 코드를 확인한다.
3. 현재 Rev 단계(architecture.md §9)와 확정/미확정 항목(§11)을 확인한다.
4. 프로젝트 고유 제약사항을 식별한다.
5. 요청을 구현 로직으로 변환한다.
6. 영향 범위를 분석한다.
7. 그 다음에만 구현/리뷰한다.

정보가 부족하면:

1. 프로젝트 문서와 기존 코드를 먼저 찾아본다.
2. 관례가 명확히 유추 가능하면 그것을 따른다.
3. 구현이 실질적으로 달라지는 정보 누락일 때만 사용자에게 묻는다.
4. 중요한 가정은 명시적으로 밝힌다.

컨텍스트가 길어졌다는 이유만으로 진행 중인 엔지니어링 작업을 중단하지 않는다.

---

## 2. 최소 변경 원칙

요청된 작업에 필요한 파일/로직만 수정한다.

다음을 요청 없이 하지 않는다: 무관한 리팩터링, 클린업, 네이밍 변경, 포맷팅 변경, 파일 이동, API 재설계, 아키텍처 재설계.

기존 코드가 비효율적이거나 스타일이 마음에 들지 않아도, 요청된 작업에 직접 영향을 주지 않으면 그대로 둔다.

---

## 3. 현재 단계 / 미확정 항목

`architecture.md` §9(Rev 0~7)와 §11(미확정 항목)을 기준으로 현재 작업 범위를 판단한다.

- **현 최우선 목표(§0)**: 640/680/950nm 3파장 raw data를 AS7341 I2C로 정확히 읽는 것 (Rev1).
- **아직 구현하지 않는 것**: Battery/Thermal 모듈, OTA(SMP) 실제 로직, 생산검사 모드, 규제 요구사항.
  이 항목들과 관련된 코드를 요청 없이 임의로 채우지 않는다.
- 새로운 미확정 사항을 발견하면 `architecture.md` §11에 추가를 제안하고, 코드에는
  `// TODO(open-item): ...` 로 표시한다 (codingstandard.md §7).

---

## 4. OTA / 보안

OTA(MCUboot + SMP)는 v1.0 필수 사양이며 Rev2에서 구현한다 (architecture.md §3, §9).
현재는 `Application/sysbuild.conf`에 `SB_CONFIG_BOOTLOADER_MCUBOOT=y`만 켜서 sysbuild가
MCUboot를 child image로 빌드하도록 구조만 준비돼 있다. 서명키/파티션 레이아웃(`pm_static.yml`),
실제 SMP 서비스 로직은 Rev2 전까지 손대지 않는다. 보안/DFU 관련 변경은 항상 고위험 변경으로 취급한다.

---

## 5. Reset 아키텍처

시스템 리셋은 반드시 `m_ctrl`을 경유한다 (architecture.md §3, codingstandard.md §5).

다른 모듈/태스크에서 `sys_reboot()`를 직접 호출하지 않는다. 의도된 경로:

```text
m_ctrl_report_error(err) 또는 m_ctrl_request_reset(reason)
        ↓
m_ctrl_task_entry()가 메시지 큐에서 처리
        ↓
필요 시 m_ctrl_request_reset() → sys_reboot() (m_ctrl.c 내부에서만 호출)
```

일반적인 복구 로직이 이 리셋 경로를 우회하게 만들지 않는다.

---

## 6. 태스크 생성/초기화 관례

이 프로젝트는 각 태스크가 **자기 자신을 `K_THREAD_DEFINE`으로 등록**하고,
**드라이버/스택 init도 태스크 진입 직후 자기 자신이 수행**한다 (S2 `task_i2c` 관례를 재해석해서 계승).

- `main()`은 태스크를 생성하지도, 모듈을 초기화하지도 않는다 — 부팅 배너 출력 후 `k_sleep(K_FOREVER)`뿐이다.
- 새 태스크를 추가할 때 main.c에 `k_thread_create()`나 init 호출을 넣지 않는다. 해당 모듈 `.c` 파일
  맨 아래에 `K_THREAD_DEFINE(...)`을 추가하고, 태스크 진입 함수 안에서 자체 init을 수행한다.
- 태스크 우선순위는 `config_app.h`의 `I2C_TASK_PRIORITY`(2) > `BLE_TASK_PRIORITY`(4) > `CTRL_TASK_PRIORITY`(6) 순서를
  유지한다 (architecture.md §2.2). main 스레드 기본 우선순위(0)가 이 값들보다 높으므로 순서를 함부로 바꾸지 않는다.

---

## 7. ISR 규칙 (Zephyr)

ISR(특히 RTC 100ms, `m_i2c_rtc.c`)에는 다음만 허용한다: timestamp capture, `k_sem_give()`/`k_event` set류 호출.

Zephyr의 `k_sem_give()`, `k_msgq_put(..., K_NO_WAIT)` 등은 그 자체로 ISR-safe하므로
FreeRTOS식 `xSemaphoreGiveFromISR()` 같은 별도 API가 필요 없다. 다만 아래는 ISR에서 여전히 금지한다:

* BLE notify / I2C·SPI 트랜잭션 / flash write
* 로그 출력(`LOG_*`)
* 신호처리·연산
* 동적 할당(`k_malloc` 등)

위반하는 `*_isr()`/`*_handler()` 함수는 리뷰에서 반려한다 (codingstandard.md §3).

---

## 8. 동시성 / 공유 데이터

CTRL/BLE/I2C 태스크, 콜백, ISR 간 공유되는 변수는 다음을 명시적으로 점검한다:

* race condition, atomicity, reentrancy
* critical section 필요 여부
* semaphore/mutex 필요 여부 (`mtx_<name>`, codingstandard.md §1)
* queue/event가 더 적합한지 여부

`volatile`만으로 공유 상태가 안전해진다고 가정하지 않는다. `g_ctrl_status` 같은 변수도
동기화 방식(현재는 `ctrl_msgq`를 통한 단일 소비자 처리)을 별도로 검토한다.

---

## 9. 입력 검증 (Rev2 이후 BLE 활성화 시 적용)

외부 입력(BLE 패킷 등)은 항상 검증한다: 길이, 파라미터 범위, enum 값, 패킷/상태 호환성, CRC/체크섬.
들어오는 패킷 필드를 그대로 신뢰하지 않는다.

---

## 10. 경계/포인터 안전성

배열/포인터 접근 전에 경계를 확인한다. 이 프로젝트는 **Zephyr 프로젝트이므로 `__ASSERT()`를 사용한다**
(S1의 `assert()`/`APP_ERROR_CHECK`가 아니라 Zephyr 표준 관례를 따른다).

---

## 11. 로깅

이 프로젝트는 **Zephyr `LOG_*`**(`LOG_INF`/`LOG_WRN`/`LOG_ERR`, `LOG_MODULE_REGISTER`)를 사용한다
(S1의 `NRF_LOG_*`가 아니다). 새 로깅 프레임워크를 임의로 도입하지 않는다.

---

## 12. 네이밍 컨벤션

`codingstandard.md` §1과 동일:

```text
m_<module>_<verb>_<noun>()  : 모듈 함수
g_<name>                    : 전역 변수
s_<name>                    : 파일 스코프 정적 변수
sem_<name> / mtx_<name>     : 세마포어 / 뮤텍스
<name>_t                    : 타입 정의 (에러코드는 module_err_t 하나로 통일)
UPPER_SNAKE_CASE             : 매크로/상수
```

DEVICE_TYPE 분기(A/C/H)는 이 프로젝트에 없다 — fNIRS는 단일 기기다.

---

## 13. 주석 스타일

기본은 **주석을 쓰지 않는다** — WHY가 비직관적일 때만 남긴다 (숨은 제약, 특정 버그 우회, 놀라운 동작 등).
`// TODO(open-item): ...`은 architecture.md §11의 미확정 항목과 짝을 이룰 때만 사용한다.
Nordic식 `@brief/@param/@return` Doxygen 블록이나 `#pragma region`을 강제하지 않는다.

---

## 14. 고위험 변경 영역

다음은 사소한 수정으로 취급하지 않는다: RTC drift 보정 로직, 신호처리/필터, 배터리/충전(향후 구현 시),
보안/DFU, Reset, Watchdog, 전력관리 로직. 의료기기 신호 유효성/안전성에 영향을 줄 수 있어 더 깊은 리뷰가 필요하다.

---

## 15. 전력 검토

배터리 구동 기기이므로 변경 시 다음을 고려한다: polling 주기, 태스크 wake 빈도, BLE 라디오 활동,
타이머 주기, 센서 duty cycle, peripheral enable 유지시간, CPU sleep 기회. 불필요한 주기적 polling보다
event-driven을 우선한다 (`m_ble.c`의 폴링도 Rev2에서 event-driven으로 전환 예정, TODO 표시돼 있음).

---

## 16. 빌드 / 플래시

이 저장소에는 아직 `doc/Build.md`가 없다 — **`agents.md` §5**의 west 빌드 템플릿과, 이 세션에서 실제
검증한 빌드 절차(로컬 NCS 툴체인 경로, `west build -b <board> Application`)를 기준으로 삼는다.
빌드 전 확인: (1) 대상 보드가 확정됐는지(§11 boards 미확정이면 sanity build용 임시 보드만 사용),
(2) `prj.conf`/`sysbuild.conf` 변경 여부, (3) 보안/OTA 관련 설정 변경 여부.
문서화된 명령이 있으면 임의로 새 빌드 명령을 지어내지 않는다.

---

## 17. 검증 보고 구분

다음을 명확히 구분해서 보고한다: 코드 리뷰 완료 / 빌드 검증 완료 / 플래시 검증 완료 / BLE 검증 완료 /
센서 검증 완료 / 배터리·충전 검증 완료 / 전류소비 검증 완료 / 하드웨어 검증 완료 / 미검증.
빌드만 성공했는데 하드웨어 검증이 끝난 것처럼 보고하지 않는다. 실물 하드웨어가 없으면 무엇이
미검증 상태인지 명시한다.

---

## 18. 리뷰 우선순위

1. 치명적 버그
2. 회귀 위험
3. 하드웨어 안전 문제
4. 검증 누락
5. 타이밍/동시성 결함
6. 보안 이슈
7. 프로젝트 규칙 위반
8. 유지보수성
9. 스타일

스타일 이슈가 기능/안전 결함 리뷰를 방해하지 않게 한다.

---

## 19. 참고 문서

```text
agents.md          작업 지침 요약 (최우선 확인 대상)
architecture.md    설계 근거, Rev 단계, AT 체크리스트, 미확정 항목
codingstandard.md  네이밍/모듈 구조/ISR/커밋 규칙
fNIRS_FW_v1.0_트래커.html   Day Plan / Task 체크리스트 (오늘 목표 산출물 확인용)
```

실제 핀맵/하드웨어 사양은 (작성 예정인) `pinmap.md`와 프로젝트 소스/보드 overlay가 최종 근거다.

---

# 작업 원칙 요약

```text
문서 확인 (agents/architecture/codingstandard + 트래커)
        ↓
현재 Rev 단계 / 미확정 항목 확인
        ↓
요청 분석 → 기존 코드 확인
        ↓
하드웨어/동시성/전력 영향 확인
        ↓
최소 필요 변경만 적용
        ↓
빌드 검증 (가능하면)
        ↓
리뷰
        ↓
결과 보고 (검증 항목 구분)
```

프로젝트 고유 규칙은 항상 일반적인 펌웨어 권장사항보다 우선한다.
