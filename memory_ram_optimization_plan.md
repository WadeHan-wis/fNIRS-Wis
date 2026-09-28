# 메모리/RAM 최적화 계획 (기준: v0.1.27)

> `west build -t ram_report` 실측 기반 분석. 추측이 아니라 링커 맵에서 직접 뽑은 숫자다.
> 전체 RAM 64KB 중 현재 92.50% 사용 중(App Flash 74.45%).

## 실측 상위 RAM 소비 항목

| 항목 | 크기 | 비중 | 성격 |
|---|---|---|---|
| mcumgr SMP netbuf pool (`NETBUF_SIZE=2475 × COUNT=4`) | 9,900B | 16.3% | OTA 속도 최적화용(`NCS_SAMPLE_MCUMGR_BT_OTA_DFU_SPEEDUP`) — 현재 펌웨어 이미지 크기 대비 과한 설정 |
| `s_ring_buffer`(`m_i2c_ring_buffer.c`, 220샘플) | 7,920B | 13.1% | 22초 grace period(`BLE_DISCONNECT_STANDBY_TIMEOUT_TICKS`) 유지용, 기능적으로 이미 필요해서 맞춘 값 |
| RTT `_acUpBuffer`(SEGGER_RTT) | 4,096B | 6.8% | 로그 유실 방지용으로 1KB→4KB 확대(의도적, prj.conf 주석 참고) |
| `s_batch`(`m_ble.c`, 17샘플) | 612B | 1.0% | BLE 배칭 버퍼, 이미 MTU 기준 최소 크기 |
| BLE 스택 스레드 스택/큐(hci_core, l2cap 등) | 나머지 대부분 | - | Zephyr/Nordic 기본값 |

## 실행 계획

| 순위 | 항목 | 예상 절감 | 리스크 | 상태 |
|---|---|---|---|---|
| 1 | mcumgr netbuf 축소(`NETBUF_COUNT=4→2` 또는 `NETBUF_SIZE` 축소) | 최대 ~5,000B(8%) | 🔴 **고위험**(codingstandard/CLAUDE.md §14: DFU=고위험 변경) — 축소 후 **OTA 업그레이드/롤백 재검증 필수** | **보류 — 별도 승인 후 진행** |
| 2 | `m_ble_batch.c`/`.h` 삭제 | RAM 영향 미미, 코드 정리 | 🟢 안전 — 완전 미사용 확인(`m_ble.c`가 자체 배칭 로직으로 이미 대체, 어디서도 호출 안 됨) | **오늘 실행** |
| 3 | 해소된 TEMP_ 디버그 플래그 3개 제거: `TEMP_RTC_TICK_LOG_TEST`, `TEMP_BLE_DISABLE_TEST`, `TEMP_LED_STATIC_ALL_ON_TEST` | 코드/플래시 정리 | 🟢 안전 — 전부 검증 완료 후 0으로 비활성화된 채 방치된 초기 개발용 훅 | **오늘 실행** |
| 4 | `TEMP_WATCHDOG_FAULT_INJECT_TEST` | - | - | **유지** — AT-10 watchdog fault injection 실기검증에 재사용 예정, 제거 안 함 |
| 5 | `TEMP_AS7341_READ_TEST` | - | 🟢 안전(항목3과 동일 성격이나 이번 승인 범위 밖) | 보류 — 향후 별도 정리 대상으로 기록만 |
| 6 | `TEMP_AS7341_RAW_DBG_LOG=1` → 0 복귀 검토 | 플래시만 소폭 | 🟡 낮음 | 보류 — dark-frame/STATUS2 하드웨어 검증 끝난 뒤 복귀(지금 끄면 진행 중인 검증 방해) |
| 7 | `MODULE_ERR_I2C_NACK` enum 미사용 | - | 🟢 안전(사소) | 보류 — 드라이버가 실제로는 항상 `I2C_TIMEOUT`만 반환, NACK 구분 반영하거나 enum 정리 필요 |
| 8 | RTT 버퍼 4KB→축소 | 최대 3KB | 🟡 낮음, 타이밍 나쁨 | 보류 — STATUS2/dark-frame/fault-recovery 등 RTT 의존 검증이 한창 진행 중이라 이번 주 검증 다 끝난 뒤 재검토 |

## 2026-09-28 실행분 (항목 2, 3)

- `m_ble_batch.c`/`m_ble_batch.h` 삭제
- `TEMP_RTC_TICK_LOG_TEST`, `TEMP_BLE_DISABLE_TEST`, `TEMP_LED_STATIC_ALL_ON_TEST` 매크로 및 관련 `#if` 블록 제거(`config_app.h`, `m_i2c.c`, `m_ble.c`)
- 빌드 검증 결과는 `CHANGELOG.md` 해당 버전 항목 참고

## 향후 진행 시 참고

- 항목 1(mcumgr netbuf)을 진행하기로 하면: 축소 후 반드시 (a) OTA 업그레이드, (b) 다운그레이드, (c) 강제 전원차단 중 롤백, (d) 손상 이미지 거부 — 4개 시나리오 전부 재검증.
- 항목 8(RTT 버퍼)은 이번 주 하드웨어 검증(다크프레임/STATUS2/fault recovery) 전부 끝난 뒤 재검토.
