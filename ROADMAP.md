# RBS Roadmap

План реализации следующих итераций проекта RBS.

## План реализации

## Реализовано (п.1–п.29)

- п.1-п.8: базовая Multi-RAT архитектура, HAL/Common, GSM/UMTS/LTE стеки, Mobility/SON.
- п.9-п.16: CSFB, HSDPA/HSUPA, LTE UL/measurement, UMTS soft HO, CA, ROHC.
- п.17-п.22: NR/F1AP stub, REST dashboard, EN-DC NSA, PM export, PDCP integrity, Link Management API.
- п.23: LTE operations layer (multi-cell services, VoLTE helpers, handover control, per-cell PM).
- п.24: NR MAC Scheduler + SDAP/PDCP (queue-aware scheduling, HARQ loop, QFI->DRB mapping).
- п.25: Xn-AP для inter-gNB handover сценариев (in-memory transport + e2e tests).
- п.26: NG-AP для gNB<->AMF сценариев (in-memory transport + PDU session/release tests).
- п.27: RAN Slicing в NR (eMBB/URLLC/mMTC quotas, OMS slice counters, `/api/v1/slices`).
- п.28: CI/CD GitHub Actions (Linux matrix Debug/Release, ASAN в Debug, обязательный ctest).
- п.29: Real NG/Xn Transport (SCTP): dual-mode transport для `NgapLink`/`XnAPLink` (in-memory + SCTP backend), API биндинга/подключения peer, тест `test_ng_xn_transport`.

## Что еще реализовать (следующий этап)

### п.30 - Interop E2E (Open5GS + Osmocom)

Цель:
проверить межвендорную совместимость и работоспособность сценариев вне in-memory стенда.

Артефакты:
- e2e smoke-скрипты для Open5GS (NGAP/PDU session) и Osmocom (Abis IPA).
- Набор golden traces/pcap для регрессионной проверки.

Критерий готовности:
- воспроизводимые e2e сценарии с документированными командами запуска.

### п.31 - CI Maturity (Windows + артефакты)

Цель:
расширить CI до полной кросс-платформенной валидации.

Артефакты:
- Matrix jobs для Windows (MSVC) + Linux.
- Публикация тестовых логов/артефактов при падении.
- Опционально: clang-tidy/format/lint pipeline.

Критерий готовности:
- стабильный green CI для Linux/Windows, удобная диагностика падений.

### п.32 - Performance & Observability

Цель:
формализовать профиль производительности и телеметрию.

Артефакты:
- Benchmark/smoke latency tests для scheduler/throughput path.
- Расширенные Prometheus метрики (latency buckets, per-interface error counters).

Критерий готовности:
- измеряемые KPI и автоматическая проверка регрессий производительности.

### п.33 - Security Hardening

Цель:
укрепить безопасность управляющего и API-путей.

Артефакты:
- Валидация и лимиты REST payload (size/rate).
- Threat-model notes + negative/fuzz tests для codec/parser путей.

Критерий готовности:
- покрытие негативных сценариев и отсутствие критичных уязвимостей в базовом threat model.
