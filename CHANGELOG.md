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
