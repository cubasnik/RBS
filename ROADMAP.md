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
- п.29: Real NG/Xn Transport (SCTP): dual-mode transport для `NgapLink`/`XnAPLink` (in-memory + SCTP backend), API биндинга/подключения peer, согласованный transport framing, активный RX-поток и тест доставки `test_ng_xn_transport`.
- п.30a: NR MAC HARQ retransmit counters + CQI/RI feedback loop: `HarqStats` (totalRetx/failures), `HARQ_MAX_RETX=3` (TS 38.321), `CsiReport` struct, `reportCsiRi()`/`reportCsi()`, RI-scaled TBS, тесты `test_nr_mac_harq_max_retx`/`test_nr_mac_csi_ri_tbs_scaling`/`test_nr_mac_csi_combined_report`.
- п.30b: GSM BSSGP/NS layer поверх Abis (GPRS tracing): `GprsNs` (TS 48.016 — NS-RESET/ALIVE/UNITDATA state machine), `GprsBssgp` (TS 48.018 — UL/DL-UNITDATA, BVC-RESET, RADIO-STATUS, трассировка `GprsBssgpTrace` с BCD Cell ID), 9 тестов в `test_gprs_bssgp`.
- п.30c: Динамический reload `rbs.conf` без перезапуска (SIGHUP / REST PATCH): потокобезопасный `Config`, endpoint `PATCH /api/v1/config` (reload from disk + key patch + batch `updates[]`), whitelist валидация изменяемых ключей, `dryRun` (валидация без применения), `all-or-nothing` транзакция с явной ошибкой `updateIndex`, runtime apply callback (logging + GSM Abis tunables), сигнал `SIGHUP` (и `SIGBREAK` на Windows), тесты в `test_rest_api`.
- п.30d: **SCTP Multi-homing + Performance Tuning + Notifications** *(новое)*:
  - **Multi-homing support**: `bindMulti(localAddrs[])`, `connectMulti(remoteAddrs[], primaryIdx)`, `setPrimaryPath(idx)` для failover; Linux использует `sctp_bindx()/sctp_connectx()`, Windows graceful fallback к first address.
  - **Performance Tuning**: `SctpTuning` struct (heartbeat interval, RTO bounds, init retransmit params, buffer sizes), `applyTuning()` для Linux native SCTP/usrsctp.
  - **SCTP Notifications**: `SctpNotificationType` enum (ASSOC_CHANGE, PEER_ADDR_CHANGE, SEND_FAILED), `SctpNotification` struct, `NotificationCallback`, `setNotificationCallback()`, real event parsing + logging.
  - **RX Enhancement**: `recvmsg()` с MSG_NOTIFICATION flag для разделения данные/события, асинхронный dispatch к приложению.
  - **Tests**: `test_sctp_multihoming.cpp` (9 unit tests), `test_sctp_tuning_notifications.cpp` (5 unit tests), все pass на Windows UDP fallback.
  - **Windows Backend Plan**: Документировано в `WINDOWS_USRSCTP_BACKEND.md` (deferred; полный userland I/O backend с callback bridge, буферизацией, синхронизацией).
- п.30e: **Infrastructure & UX improvements**:
  - **REST bind fix**: httplib `set_address_family(AF_INET)` перед bind — устраняет выбор IPv6-сокета на Windows при `AF_UNSPEC`; REST-сервер стабильно слушает `0.0.0.0:8181`.
  - **Port change 8080 → 8181**: порт 8080 занят `svchost` (WSL2 Hyper-V bridge service); новый порт по умолчанию — **8181**.
  - **Address fields in status API**: `GET /api/v1/status` возвращает `nodeAddr`, `restAddr`, `promAddr` для самодиагностики.
  - **WSL2 mirrored networking**: рекомендуемый режим `networkingMode=mirrored` в `~/.wslconfig`; `127.0.0.1` становится доступен из WSL напрямую без NAT-обходов.
  - **Logger color scheme**: ANSI 256-color на консоли — per-RAT цвет тега `[component]`, фоновый цвет строки для WARNING/ERR/CRITICAL, уровень логирования с цветом (`DBG`=серый, `INFO`=зелёный, `WARNING`=жёлтый, `ERR`/`CRITICAL`=красный).
  - **`tools/rbs_api.sh` rewrite**: заменён `powershell.exe Invoke-RestMethod` на `curl`, добавлен rich Python renderer (цветные badges методов, семантическая окраска RAT/IP/version, состояния UNLOCKED/LOCKED, цветные числа и булевы).

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
