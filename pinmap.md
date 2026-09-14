# fNIRS PoC v1 핀맵 (Pinmap)

| 항목 | 내용 |
|---|---|
| 문서번호 | PIN-001 |
| 개정번호 | Rev.0 |
| 근거 자료 | `docs/SCH_fNIRS_Sleep_Project.pdf` (Altium 스키매틱 네트리스트 직접 파싱, 4개 시트) |
| 대상 하드웨어 | fNIRS PoC v1 보드 (이미 제작된 실물 보드) |
| 작성일 | 2026-09-14 |
| 상태 | 스키매틱 텍스트/네트리스트 기준 1차 추출본. **회로도 vs 실물 보드 리비전 불일치 가능성은 반드시 실측(멀티미터/스코프)으로 재검증한다.** |

> 근거: `architecture.md`(설계), `codingstandard.md`(코딩 규칙), `agents.md`(작업 지침)와 함께 참고한다.
> 이 문서는 스키매틱에 있는 그대로를 기록한다 — 임의로 "이럴 것이다"라고 보정하지 않는다. 스키매틱과
> 다른 문서(architecture.md 등) 사이에 불일치가 발견된 부분은 6장에 그대로 남겨둔다.

---

## 1. 개요 / 시트 구성

스키매틱 파일은 4개 시트로 구성된다:

| 시트 | 내용 |
|---|---|
| Sheet 1 (`nRF52_V3.SchDoc`) | MCU + BLE — nRF52832 (U1), 32.768kHz/32MHz 크리스탈, BLE 안테나 매칭망, LED 제어/I2C 신호 인출 |
| Sheet 2 (`Power_V3.SchDoc`) | DB POWER — 무선충전(BQ51050B), 전원 스위치(MAX16054), Ideal diode(LTC4412), VDD1.8/VDD3.3 LDO |
| Sheet 3 (`AS7341_NIR_V3.SchDoc`) | NIR1/NIR2 — AS7341 멀티스펙트럴 센서 2개, 각각 독립 I2C |
| Sheet 4 (`LED_V3.SchDoc`) | LED1/LED2/LED3 — 640/680/980nm LED 3개 + 트랜지스터 구동회로 |

MCU: **nRF52832-QFAA-R** (U1, 49-pin QFN, Sheet 1).

---

## 2. LED 드라이버 핀맵

| LED | 부품 | 파장(스키매틱 표기) | Vf | 구동 트랜지스터 | 베이스 저항 | 제어 net | MCU 핀 (U1) |
|---|---|---|---|---|---|---|---|
| LED1 | D2, `XZM2CRK54WA-8` | **640nm** | 2.8V | Q4 (DTC123TET1G) | R17 (0Ω) | `IR_LED_CTRL1` | **P0.04/AIN2** (pin6) |
| LED2 | D3, `OIS-330_IE680-X-TU` | **680nm** | 1.8V | Q5 (DTC123TET1G) | R18 (0Ω) | `WH_LED_CTRL2` | **P0.05/AIN3** (pin7) |
| LED3 | D4, `MTE9730CP` | **980nm** | 2.0V | Q6 (DTC123TET1G) | R20 (0Ω) | `IR_LED_CTRL2` | **P0.06** (pin8) |

- LED 전원: VDD3.3, 상단 전류제한저항 R15(90Ω)/R16(50Ω)/R19(90Ω).
- 구동 방식: MCU GPIO → 트랜지스터(NPN, 공통이미터) 베이스 → LED 캐소드 쪽 전류 스위칭 (Low-side switch). **PWM duty 제어는 이 GPIO를 PWM 출력으로 재활용하면 된다** (architecture.md §5 LED PWM duty 정책과 정합).
- `U1 pin5 (P0.03/AIN1) = WH_LED_CTRL1`도 스키매틱에 정의돼 있으나, **LED1/2/3 어디에도 연결되지 않는다** (Sheet 4에 대응 회로 없음). 예비/미사용 핀으로 기록한다.
- net 이름(`IR_LED_CTRL*` / `WH_LED_CTRL*`)이 실제 파장(적외선/백색)과 일치하지 않는다 — 다른 프로젝트(S1/S2) net 네이밍을 재사용한 흔적으로 보인다. **의미가 아니라 실제 결선(위 표)을 기준으로 코드에 반영한다.**

---

## 3. AS7341 (NIR1 / NIR2) I2C 핀맵

보드에는 AS7341 2개가 **각각 독립된 I2C 버스**로 연결되어 있다 (버스 공유 아님).

| 인스턴스 | 부품 | SCL (MCU 핀) | SDA (MCU 핀) | Pull-up | VDD | GND/PGND |
|---|---|---|---|---|---|---|
| NIR1 (U8) | AS7341-DLGM | `SCL_1V8_D1` → **P0.09/NFC1** (pin11) | `SDA_1V8_D1` → **P0.10/NFC2** (pin12) | R3, R4 = 4.7kΩ → VDD1.8 | VDD1.8 | GND |
| NIR2 (U9) | AS7341-DLGM | `SCL_1V8_D2` → **P0.08** (pin10) | `SDA_1V8_D2` → **P0.07** (pin9) | R1, R2 = 4.7kΩ → VDD1.8 | VDD1.8 | GND |

- AS7341의 **GPIO(6)/INT(7)/LDR(4) 핀은 두 인스턴스 모두 MCU에 연결되어 있지 않다** (스키매틱 상 미결선 표시). 즉 현재 보드에서는 AS7341 데이터 준비 인터럽트를 하드웨어적으로 받을 수 없고, **RTC 트리거 기반 polling read만 가능하다** — `architecture.md` §2.3(RTC 100ms 트리거) 구조와 일치하는 제약이다.
- 디커플링: C34/C36 (U8), C35/C37 (U9) — 100nF + 1uF.

### ✅ NFC 핀 → GPIO 전환 결정 및 반영 완료 (2026-09-14)

`P0.09`/`P0.10`은 nRF52832에서 기본적으로 **NFC 안테나 전용 핀**으로 리셋된다. 현재 보드가 이 두 핀을
NIR1 I2C(SCL/SDA)로 실장했으므로, **NFC 기능을 포기하고 GPIO로 전환하기로 결정**했다.

- 이 NCS 버전(Zephyr 4.4, NCS v3.4.0)은 `CONFIG_NFCT_PINS_AS_GPIOS` Kconfig가 **폐지**되었고, 대신
  UICR devicetree 프로퍼티로 설정한다 (`zephyr` migration-guide-3.5).
- `Application/app.overlay`에 반영 완료:
  ```dts
  &uicr {
      nfct-pins-as-gpios;
  };
  ```
- 실제 NIR1 I2C 통신 성공 여부는 Rev1에서 AS7341 드라이버 작업 시 실측으로 최종 검증한다.

---

## 4. 프로그래밍/디버그 핀

| 신호 | MCU 핀 | 비고 |
|---|---|---|
| SWDCLK | pin25 | P1 (2-pin 헤더)로 인출 |
| SWDIO | pin26 | P1 (2-pin 헤더)로 인출 |
| nRESET | P0.21 (pin24) | |

P1은 2핀 헤더 — SWDCLK/SWDIO만 나와 있고 VDD/GND는 별도 확인 필요(스키매틱 상 명시 안 됨, 실측 확인 권장).

---

## 5. 클럭 / RF

| 항목 | MCU 핀 | 부품 | 비고 |
|---|---|---|---|
| LFXO (32.768kHz) | XL1=P0.00(pin2), XL2=P0.01(pin3) | X1, ECS-.327-12.5-12-TR | RTC 100ms 타이밍(architecture.md §2.3)의 기준 클럭 |
| HFXO (32MHz) | XC1(pin34), XC2(pin35) | X2, ECS-240-8-37CKM | |
| BLE 안테나 | ANT (pin30) | E1 칩안테나 + L1/L2/C3 매칭망 | 50Ω 임피던스 매칭, Altium 노트에 라인폭 규격 명시됨 |
| DC/DC (DCC) | pin47 | L3(15nH)+L4(10uH) | nRF52832 내부 벅 컨버터용 인덕터 |

전원 핀: VDD 13/36/48, VSS 31/45, DEC1~4 (1/32/33/46, 내부 레귤레이터 디커플링, 고정 용도), EP(exposed pad)=49=GND.

---

## 6. 전원단 (충전/전원관리) — MCU 연결 없음 확인

Sheet 2(DB POWER)에는 다음 IC들이 있다: `BQ51050B`(무선충전 리시버, U2), `MAX16054`(전원 버튼 컨트롤러, U3),
`LTC4412`(ideal diode controller, U5), `S-1318A18`/`S-1318D33`(각각 VDD1.8/VDD3.3 LDO, U6/U7),
전원 버튼(SW1), 충전 LED(D1, blue).

**이 스키매틱 범위 안에서는 U2/U3/U5/U6/U7 ↔ U1(MCU) 사이에 GPIO 배선이 보이지 않는다.**
즉 현재 확인된 자료 기준으로는 MCU가 충전 상태(`BQ51050B_*CHG`)나 전원 버튼(`SW1`/`MAX16054_OUT`)을
직접 읽을 수 있는 핀이 이 시트에는 없다. 이는 `architecture.md` §11의 "배터리 용량/DFU/calibration ...
UVLO 보호 회로 인터페이스" 미확정 항목과 맥이 닿아 있다 — **배선이 실제로 없는 것인지, 아니면 이
스키매틱에 빠진 것인지 하드웨어 담당자 확인이 필요하다.**

---

## 7. 확정된 사항 / 확인이 필요한 사항

### 확정됨
- **LED3 파장(950 vs 980nm)**: 950nm이 원래 스펙이 맞고, PoC v1의 MTE9730CP(980nm)는 **다음 보드
  리비전에서 950nm 부품으로 교체 예정**인 하드웨어 개선 항목으로 확정 (`architecture.md` §11 "다음
  보드 리비전 반영 예정" 참고, 2026-09-14). 그 전까지 PoC v1로 진행하는 측정은 실제 LED3 파장이
  980nm이라는 점을 감안해서 해석한다.
- **NFC 핀 GPIO 전환**: P0.09/P0.10을 NIR1 I2C(SCL/SDA)로 쓰기로 결정 — `Application/prj.conf`에
  `CONFIG_NFCT_PINS_AS_GPIOS=y` 반영 완료 (§3-1 참고).

### 여전히 확인이 필요한 사항 (제가 임의로 결론 내리지 않은 것)
1. `WH_LED_CTRL1`(P0.03) 미사용 핀의 용도(4번째 LED 예비용인지, 다른 용도인지).
2. §6의 전원/충전 상태 MCU 배선 여부(스키매틱에 없는 것이 맞는지).
3. P1 헤더(SWD)에 VDD/GND 핀이 실제로 있는지(스키매틱에 명시 안 됨) — 실물 보드 실측 필요.

---

## 8. 다음 단계 제안

- 위 §7 "여전히 확인이 필요한 사항"은 실물 PoC v1 보드 멀티미터 실측 또는 하드웨어 담당자 확인 후
  이 문서에 반영한다.
- NFC→GPIO 전환은 `Application/app.overlay`에 반영 완료. 나머지 핀맵(LED PWM/I2C 버스)이 확정되면
  같은 overlay 파일에 devicetree 노드를 추가해 실제 빌드에 연결한다 (`agents.md` §5,
  `codingstandard.md` §2 boards/ 참고).
