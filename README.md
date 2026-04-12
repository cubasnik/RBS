# RBS — Radio Base Station

Симулятор многостандартной базовой станции (Multi-RAT RBS), реализующий протокольные стеки **GSM (2G)**, **UMTS (3G)**, **LTE (4G)** и **5G NR** в одном исполняемом файле на языке **C++17**.

---

## Содержание

1. [Общая архитектура](#общая-архитектура)
2. [Структура проекта](#структура-проекта)
3. [Принцип работы](#принцип-работы)
   - [Слой Common](#слой-common)
   - [HAL — Hardware Abstraction Layer](#hal--hardware-abstraction-layer)
   - [GSM стек (2G)](#gsm-стек-2g)
   - [UMTS стек (3G)](#umts-стек-3g)
   - [LTE стек (4G)](#lte-стек-4g)
   - [5G NR стек](#5g-nr-стек)
   - [OMS — Operations & Maintenance](#oms--operations--maintenance)
   - [Управление интерфейсами (Link Management)](#управление-интерфейсами-link-management)
   - [REST API — Web Dashboard](#rest-api--web-dashboard)
   - [Главный контроллер (main)](#главный-контроллер-main)
4. [Диаграмма потоков данных](#диаграмма-потоков-данных)
5. [Конфигурационный файл rbs.conf](#конфигурационный-файл-rbsconf)
6. [Сборка](#сборка)
7. [Запуск](#запуск)
8. [Тесты](#тесты)
9. [Руководство по использованию](#руководство-по-использованию)
10. [Разработка с GitHub Copilot](#разработка-с-github-copilot)
11. [Логирование](#логирование)
12. [Стандарты и спецификации](#стандарты-и-спецификации)
13. [История разработки](#история-разработки)
14. [Дорожная карта](#дорожная-карта-план-следующих-итераций)

---

## Общая архитектура

Программа построена по **многоуровневой (layered) архитектуре**, где каждый уровень зависит только от уровня ниже:

```
┌─────────────────────────────────────────────────┐
│               RadioBaseStation                  │  ← main.cpp
├──────────────┬──────────────┬───────────────────┤
│  GSMStack    │  UMTSStack   │    LTEStack        │  ← Стеки RAT
├──────────────┼──────────────┼───────────────────┤
│  GSM MAC     │  UMTS MAC    │  LTE MAC + PDCP    │  ← Протокол MAC/PDCP
├──────────────┼──────────────┼───────────────────┤
│  GSM PHY     │  UMTS PHY   │    LTE PHY         │  ← Физический уровень
├──────────────┴──────────────┴───────────────────┤
│           HAL — IRFHardware / RFHardware        │  ← Железо (симуляция)
├─────────────────────────────────────────────────┤
│           Common — types, logger, config        │  ← Общие утилиты
└─────────────────────────────────────────────────┘
         ↕ OMS (глобальный синглтон)
```

Каждый RAT работает в **собственном потоке (std::thread)**, управляя тактовыми циклами независимо:

| RAT  | Тактовый период | Единица времени      |
|------|-----------------|----------------------|
| GSM  | ~577 мкс        | Временной слот TDMA  |
| UMTS | 10 мс           | Радиофрейм WCDMA     |
| LTE  | 1 мс            | Субфрейм E-UTRA      |
| NR   | 1 мс            | Субфрейм NR (SSB burst) |

---

## Структура проекта

```
RBS/
├── CMakeLists.txt          # Система сборки CMake
├── rbs.conf                # Конфигурационный файл узла
├── rbs.log                 # Лог-файл (создаётся при запуске)
├── fix/                    # Скрипты пост-обработки ASN.1 (run_all.py + 7 fix_*.py)
├── src/
│   ├── main.cpp            # Точка входа, класс RadioBaseStation
│   ├── common/
│   │   ├── types.h             # Общие типы, константы, структуры данных
│   │   ├── logger.h            # Потокобезопасный синглтон-логгер
│   │   ├── config.cpp/.h       # Парсер INI-конфигурации
│   │   ├── link_controller.h   # Базовый класс-миксин: трасса сообщений + блокировка
│   │   ├── link_registry.cpp/.h # Глобальный реестр всех сетевых интерфейсов
│   │   └── pcap_writer.cpp/.h  # PCAP-экспорт: S1AP/X2AP/GTP-U → .pcap (Wireshark)
│   ├── hal/
│   │   ├── rf_interface.h  # Абстрактный интерфейс IRFHardware
│   │   └── rf_hardware.cpp/.h  # Симулированное RF-железо
│   ├── gsm/
│   │   ├── gsm_phy.cpp/.h  # Физический уровень GSM (TDMA, пакеты burst)
│   │   ├── gsm_mac.cpp/.h  # Уровень MAC (назначение каналов, SI)
│   │   └── gsm_stack.cpp/.h # Верхний контроллер GSM-ячейки
│   ├── umts/
│   │   ├── umts_phy.cpp/.h # Физический уровень UMTS (WCDMA, spreading)
│   │   ├── umts_mac.cpp/.h # MAC (DCH, планирование)
│   │   └── umts_stack.cpp/.h # Контроллер UMTS NodeB
│   ├── lte/
│   │   ├── lte_phy.cpp/.h  # Физический уровень LTE (OFDMA/SC-FDMA)
│   │   ├── lte_mac.cpp/.h  # MAC (PF-планировщик, HARQ, CQI)
│   │   ├── lte_pdcp.cpp/.h # PDCP (заголовки, шифрование AES-128 CTR)
│   │   └── lte_stack.cpp/.h # Контроллер LTE eNodeB
│   ├── nr/
│   │   ├── nr_phy.cpp/.h   # Физический уровень NR (SSB, PSS/SSS/PBCH, SFN)
│   │   ├── nr_stack.cpp/.h # Контроллер gNB-DU (NR stub)
│   │   └── f1ap_codec.cpp/.h # F1AP: F1 Setup Request/Response (TS 38.473)
│   ├── api/
│   │   └── rest_server.cpp/.h # HTTP/JSON REST API (cpp-httplib)
│   └── oms/
│       ├── oms.cpp/.h      # Fault management, счётчики производительности
└── tests/
    ├── CMakeLists.txt
    ├── test_config.cpp     # Тест парсера конфигурации
    ├── test_gsm_phy.cpp    # Тест GSM PHY
    ├── test_lte_mac.cpp    # Тест LTE MAC (CQI→MCS, планировщик)
    └── test_pdcp.cpp       # Тест PDCP (DL/UL loopback, AES KAT, SNOW3G/ZUC round-trip)
```

---

## Принцип работы

### Слой Common

#### `types.h`
Определяет все фундаментальные типы, используемые во всём проекте:
- **Идентификаторы**: `CellId`, `RNTI` (Radio Network Temporary Identifier), `IMSI`, `ARFCN`, `UARFCN`, `EARFCN`
- **Структуры ячеек**: `GSMCellConfig`, `UMTSCellConfig`, `LTECellConfig` — параметры каждой ячейки (частоты, мощность, коды)
- **Контекст UE**: `UEContext` — идентификаторы, RAT, время подключения
- **Пакеты данных**: `GSMBurst` (148 бит), `LTESubframe`, `ResourceBlock`
- **Константы протоколов**: длительности слотов, частоты чипов, размеры ресурсных блоков

#### `logger.h`
Потокобезопасный логгер-синглтон с шаблонным методом `log()`:
- Одновременная запись в `stdout` и файл `rbs.log`
- Уровни: `DEBUG`, `INFO`, `WARNING`, `ERROR`, `CRITICAL`
- Каждая запись содержит временну́ю метку ISO 8601, уровень и источник
- После каждой записи вызывается `flush()` — журнал не теряется даже при аварийной остановке

#### `config.h / config.cpp`
INI-парсер, читающий `rbs.conf`:
- Поддержка секций `[gsm]`, `[umts]`, `[lte]`, `[logging]`
- Типизированные геттеры: `getInt()`, `getDouble()`, `getString()`
- Строители конфигураций: `buildGSMConfig()`, `buildUMTSConfig()`, `buildLTEConfig()`

#### `link_controller.h`
Базовый класс-миксин, от которого наследуют `AbisOml`, `IubNbap` и `S1APLink`:
- **Трасса сообщений**: кольцевой буфер последних 100 PDU (тип, направление TX/RX, метка времени)
- **Блокировка**: набор строк-ключей (тип сообщения); если тип заблокирован — `sendXxx()` возвращает `false`
- Потокобезопасность: `std::mutex` на буфере и на множестве блокировок
- API: `pushTrace()`, `getTrace(limit)`, `clearTrace()`, `blockMsg()`, `unblockMsg()`, `isBlocked()`, `blockedTypes()`

#### `link_registry.h / link_registry.cpp`
Синглтон-реестр всех сетевых интерфейсов узла:
```cpp
struct LinkEntry {
    string name;           // "abis" | "iub" | "s1"
    string rat;            // "GSM" | "UMTS" | "LTE"
    string peerAddr;       // BSC / RNC / MME адрес
    uint16_t peerPort;
    LinkController* ctrl;  // трасса + блокировка
    function<bool()>            isConnected;
    function<void()>            reconnect;
    function<void()>            disconnect;
    function<vector<string>()>  injectableProcs;
    function<bool(string)>      injectProcedure;
};
```
- `registerLink(entry)` / `unregisterLink(name)` / `getLink(name)` / `allLinks()`
- Все операции потокобезопасны

---

### HAL — Hardware Abstraction Layer

#### `IRFHardware` (rf_interface.h)
Чистый виртуальный (pure-virtual) интерфейс, описывающий контракт с RF-железом:

```
IRFHardware
 ├── initialise() / selfTest() / shutdown()   ← жизненный цикл
 ├── setDlFrequency() / setUlFrequency()       ← настройка частот
 ├── setTxPower() / getTxPower()               ← управление мощностью
 ├── transmit(iqSamples) / receive(iqSamples)  ← передача/приём IQ
 ├── numTxAntennas() / numRxAntennas()         ← MIMO-конфигурация
 └── setAlarmCallback()                        ← аппаратные аварии → OMS
```

#### `RFHardware` (rf_hardware.cpp/.h)
Симулированная реализация (модель FPGA/BBU в реальной БС):
- `initialise()` — настраивает частоты DL/UL и мощность из конфигурации
- `selfTest()` — проверяет три подсистемы: loopback (IQ self-test), PLL lock, PA health
- `receive()` — генерирует случайные IQ-байты (имитация радиоканала)
- `transmit()` — принимает IQ-буфер, проверяет длину, «отправляет» в эфир
- При ошибке вызывает `alarmCallback_`, который пробрасывает аварию в OMS

В реальной системе класс `RFHardware` был бы заменён драйвером FPGA (например, через UIO или PCIe DMA).

---

### GSM стек (2G)

Соответствует стандарту **3GPP TS 45.002** (структура кадров и слотов).

#### Физический уровень — `GSMPhy`

```
clockLoop (thread) → tick() [каждые ~577 мкс]
                        │
                        ├── слот 0: BCCH (SI burst)
                        ├── слот 1: CCCH (paging/RACH)
                        └── слоты 2–7: TCH/SDCCH
```

**TDMA-структура:**
- 1 фрейм = **8 временны́х слотов** по 577 мкс → длительность фрейма ~4.615 мс
- 26 фреймов = 1 мультифрейм (TCH), 51 фрейм = 1 мультифрейм (управление)
- Три типа burst-пакетов:
  - **Normal Burst** — 148 бит: 57 бит данных + 26-битная обучающая последовательность + 57 бит данных + хвост
  - **Synchronisation Burst (SCH)** — BSIC + RFN для синхронизации UE
  - **Frequency Correction Burst (FCB)** — чистая несущая (все нули) на FCCH

Кодирование burst-а: 148 информационных бит упаковываются в 19 байт функцией `encodeBurst()`.

#### Уровень MAC — `GSMMAC`
- **`buildSIType1()`** / **`buildSIType3()`** — формирует системные информационные сообщения (System Information), транслируемые на BCCH. SI содержит MCC, MNC, LAC, BSIC, параметры Cell Selection.
- **`assignChannel()`** — выделяет UE временной слот и тип логического канала (SDCCH для сигнализации, TCH_F для голоса)
- **`scheduleDownlinkBursts()`** — помещает DL burst-ы в очередь для PHY
- **`onRxBurst()`** — обрабатывает принятые UL burst-ы (RACH, измерения)

#### Контроллер — `GSMStack`
- Запускает PHY и MAC, стартует поток `clockLoop`
- Предоставляет API: `admitUE(imsi)` → `RNTI`, `releaseUE(rnti)`, `printStats()`
- Счётчики OMS: `gsm.connectedUEs`
- При наличии `bsc_addr` в `rbs.conf` вызывает `abis_->connect()` при старте и регистрирует интерфейс в `LinkRegistry`

---

### UMTS стек (3G)

Соответствует **3GPP TS 25.211** (физические каналы) и **TS 25.213** (spreading/scrambling).

#### Физический уровень — `UMTSPhy`

```
clockLoop (thread) → tick() [каждые 10 мс]
                        │
                        ├── buildCPICH()   ← Common Pilot Channel
                        ├── buildSCH()     ← Synchronisation Channel
                        ├── buildPCCPCH()  ← P-Common Control Physical Channel
                        └── DCH frames     ← Dedicated Channel для UE
```

**WCDMA-параметры:**
- Частота чипов: **3.84 Мчип/с**
- 1 радиофрейм = 15 слотов = **10 мс**
- **Spreading** (расширение спектра): каждый бит данных повторяется SF раз (Spreading Factor)
  - SF = 4 (高скорость) … SF = 256 (низкая скорость, большее покрытие)
  - Ортогональные коды OVSF (Orthogonal Variable Spreading Factor)
- **Scrambling**: Gold-код на основе Primary Scrambling Code (PSC 0–511) накладывается поверх spread-сигнала для разделения ячеек

**CPICH** (Common Pilot Channel): 256 бит на фрейм, SF=256, используется UE для измерения Ec/No и RSCP.

#### Уровень MAC — `UMTSMAC`
- **DCH** (Dedicated Channel) — назначается каждому UE при входе в сеть
- `assignDCH(rnti)` — выделяет channelCode начиная с 4 (коды 0–3 зарезервированы для общих каналов), SF=16
- `scheduleDlTransmissions()` — перебирает активные DCH и передаёт данные

#### Контроллер — `UMTSStack`
- Запускает PHY + MAC, стартует поток с тактом 10 мс
- API: `admitUE(imsi, sf)`, `admitUEHSDPA(imsi)`, `reconfigureDCH(rnti, newSf)`, `releaseUE(rnti)`, `printStats()`
- Счётчики OMS: `umts.connectedUEs`
- При наличии `rnc_addr` в `rbs.conf` вызывает `iub_->connect()` при старте; убран хардкод `127.0.0.1:25412` из `softHandoverUpdate`; интерфейс регистрируется в `LinkRegistry`

#### NBAP — `nbap_codec`, `iub_link`

Протокол Node B Application Part (TS 25.433) — управляющий интерфейс NodeB ↔ RNC по Iub.
Кодирование — ручной APER-кодировщик (без asn1c: спека NBAP > 1 МБ).

Реализованные процедуры:

| Процедура | Направление | Ссылка |
|-----------|-------------|--------|
| Cell Setup Request FDD | NodeB → RNC | TS 25.433 §8.3.6 |
| Radio Link Setup Request FDD | NodeB → RNC | TS 25.433 §8.1.1 |
| Radio Link Addition Request FDD | NodeB → RNC | TS 25.433 §8.1.4 |
| Radio Link Deletion Request | NodeB → RNC | TS 25.433 §8.1.6 |
| Common Transport Channel Setup (FACH/PCH/RACH) | NodeB → RNC | TS 25.433 §8.3.2 |
| Radio Link Reconfigure Prepare (DCH SF change) | NodeB → RNC | TS 25.433 §8.1.5 |
| Radio Link Reconfigure Commit | NodeB → RNC | TS 25.433 §8.1.6 |
| Radio Link Setup Request FDD + HS-DSCH (HSDPA) | NodeB → RNC | TS 25.433 §8.3.15 |
| Reset Request | NodeB → RNC | TS 25.433 §8.7.1 |
| Audit Request | NodeB → RNC | TS 25.433 §8.6 |

Каналы HS-DSCH (HSDPA, TS 25.308):
- `UMTSMAC::assignHSDSCH()` — выделяет bearer с `UMTSChannelType::HS_DSCH`, фиксированный SF=16
- `UMTSMAC::hsdschUECount()` — число активных HSDPA UE
- `UMTSStack::admitUEHSDPA(imsi)` — полный путь: HS-DSCH MAC → RRC → RLC AM (DRB1)

---

### LTE стек (4G)

Соответствует **3GPP TS 36.211** (PHY), **TS 36.321** (MAC), **TS 36.323** (PDCP).

#### Физический уровень — `LTEPhy`

```
clockLoop (thread) → tick() [каждые 1 мс]
                        │
           ┌────────────┴───────────────┐
           SFN % 10:                     каждый субфрейм:
           ├── sf=0: PSS + SSS + PBCH   ← синхросигналы
           ├── sf=5: PSS                ← второй PSS
           └── sf=1–9: PDCCH + PDSCH    ← данные
```

**OFDMA (DL) / SC-FDMA (UL):**
- Ресурсный блок (RB) = 12 субнесущих × 7 OFDM-символов = 1 слот (0.5 мс)
- Полоса пропускания → количество RB:

| BW (МГц) | RB  | Субнесущих |
|----------|-----|------------|
| 1.4      | 6   | 72         |
| 5        | 25  | 300        |
| 10       | 50  | 600        |
| 15       | 75  | 900        |
| 20       | 100 | 1200       |

**Синхросигналы:**
- **PSS** (Primary Synchronization Signal) — последовательность Zadoff-Chu длиной 63, несёт `PCI mod 3`
- **SSS** (Secondary Synchronization Signal) — m-последовательность, несёт `PCI / 3` (группа ячейки)
- **PBCH** (Physical Broadcast Channel) — MIB (Master Information Block): полоса, номер фрейма SFN, число антенн

**PDCCH** — управляющий канал: содержит DCI (Downlink Control Information) — назначения RB для каждого UE.

**PDSCH** — канал данных: несёт TB (Transport Block) с пользовательскими данными в назначенных RB.

#### Уровень MAC — `LTEMAC`

**Планировщик (Proportional Fair, PF):**
```
для каждого субфрейма:
  metric(UE) = CQI(UE) / avgThroughput(UE)
  сортировать UE по убыванию metric
  назначить RB первым UE, пока есть свободные RB
```

- **CQI → MCS** (таблица 3GPP TS 36.213):

| CQI | MCS | Эффективность (бит/с/Гц) |
|-----|-----|--------------------------|
| 1   | 0   | 0.15                     |
| 4   | 4   | 0.60                     |
| 7   | 10  | 1.48                     |
| 10  | 17  | 3.37                     |
| 15  | 28  | 5.55                     |

- **HARQ** (Hybrid ARQ) — 8 процессов на UE; ретрансмиссия через 8 мс при NACK
- **BSR** (Buffer Status Report) — UE сообщает объём буфера; UL-планировщик выделяет гранты

#### PDCP — `lte_pdcp.cpp/.h`

Путь данных DL (нисходящий):
```
IP-пакет → addHeader() → cipher() → txQueue → MAC
```
Путь данных UL (восходящий):
```
MAC → rxQueue → decipher() → removeHeader() → IP-пакет
```

- **Заголовок PDCP PDU**: 2 байта — 1 бит D/C (данные/управление) + 15 бит SN (sequence number)
- **Шифрование**: AES-128 CTR (EEA2), SNOW 3G (EEA1), ZUC (EEA3) — полная реализация TS 33.401 §6.4. Ключ задаётся через `PDCPConfig.cipherKey`, алгоритм — через `cipherAlg`.
- **Алгоритмы**: `NULL_ALG`, `AES`, `SNOW3G`, `ZUC` (по TS 33.401)
- **Целостность (EIA2)**: AES-128-CMAC (RFC 4493) — `applyIntegrity()` / `verifyIntegrity()` добавляют/проверяют 4-байтовый MAC-I; ключ задаётся через `PDCPConfig.integrityKey` (TS 33.401 §6.4.2b)
- **PDCP COUNT / HFN wrap**: 32-бит COUNT = 20-бит HFN + 12-бит SN; HFN инкрементируется при переходе SN через 0xFFF→0; предупреждение LOG_ERR при HFN → 0xFFFFF (TS 33.401 §6.3.2)
- **ROHC**: точка подключения IP-компрессии заголовков (stub)

##### Контроллер — `LTEStack`
- `admitUE(imsi, cqi)` → RNTI: создаёт PDCP-bearer, добавляет UE в MAC-планировщик
- `sendIPPacket(rnti, data)` → PDCP→MAC→PHY (DL путь)
- `receiveIPPacket(rnti)` → PHY→MAC→PDCP (UL путь)
- `triggerCSFB(rnti)` → Circuit-Switched Fallback (LTE→GSM/UMTS)
- Счётчики OMS: `lte.connectedUEs`

#### CSFB — Circuit-Switched Fallback (TS 23.272)

CSFB позволяет LTE-терминалу совершать голосовые вызовы через GSM/UMTS, временно уходя с LTE.

Поток сигнализации:
```
LTEStack::triggerCSFB(rnti)
    │
    ├── LTERrc::triggerCSFB()    → RRC Release + перенаправление на UARFCN/ARFCN
    ├── S1APLink::sendCSFBInd()  → Extended Service Request (CM_SERVICE_REQUEST)
    └── MobilityManager::triggerCSFB(rnti, rat)
            ├── RAT::UMTS → UMTSStack::admitUE()   + NBAP RL Setup
            └── RAT::GSM  → GSMStack::admitUE()    + Abis channel setup
```

**Компоненты CSFB:**
- `LTERrc::triggerCSFB()` — формирует RRC Connection Release с `redirectedCarrierInfo` (UARFCN или ARFCN)
- `S1APLink::sendCSFBInd()` — кодирует Extended Service Request (APER S1AP)
- `MobilityManager::triggerCSFB()` — диспетчер возврата на целевой RAT; регистрирует событие в OMS

#### S1AP — `s1ap_codec`, `s1ap_link`

Протокольный уровень S1AP (TS 36.413) — управляющий интерфейс eNB ↔ MME поверх SCTP (TS 36.412).
Кодирование — APER (X.691) через сгенерированную библиотеку `rbs_asn1_s1ap` (asn1c).

Реализованные процедуры:

| Процедура | Направление | Ссылка |
|-----------|-------------|--------|
| S1 Setup | eNB → MME (req) / MME → eNB (resp) | TS 36.413 §8.7.3 |
| Reset | eNB → MME / MME → eNB | TS 36.413 §8.7.2 |
| Error Indication | eNB ↔ MME | TS 36.413 §8.7.4 |
| Paging | MME → eNB | TS 36.413 §8.7.1 |
| Initial UE Message | eNB → MME | TS 36.413 §8.6.2.1 |
| Downlink NAS Transport | MME → eNB | TS 36.413 §8.6.2.2 |
| Uplink NAS Transport | eNB → MME | TS 36.413 §8.6.2.3 |
| Initial Context Setup | MME → eNB (req) / eNB → MME (resp) | TS 36.413 §8.3.1 |
| UE Context Release Request | eNB → MME | TS 36.413 §8.3.3 |
| UE Context Release Command/Complete | MME → eNB / eNB → MME | TS 36.413 §8.3.4 |
| E-RAB Setup | MME → eNB | TS 36.413 §8.4.1 |
| E-RAB Release | MME → eNB | TS 36.413 §8.4.3 |
| Path Switch Request | eNB → MME | TS 36.413 §8.5.4 |
| Handover Required | eNB → MME | TS 36.413 §8.5.2 |
| Handover Request Acknowledge | target eNB → MME | TS 36.413 §8.5.2 |
| Handover Failure | target eNB → MME | TS 36.413 §8.5.2 |
| eNB Status Transfer | eNB → MME | TS 36.413 §8.5.3 |
| Handover Notify | eNB → MME | TS 36.413 §8.5.1 |

Транспорт:
- **Linux**: нативный SCTP kernel (`AF_SCTP`).
- **Windows**: usrsctp (userspace SCTP), автоматически подключается через CMake FetchContent.

`S1APLink` также наследует `rbs::LinkController`:
- `sendS1APMsg()` проверяет блокировку по типу процедуры (`"S1AP:18"` для S1_SETUP и т.д.) и пишет трассу
- `recvS1APMsg()` пишет трассу входящих PDU
- `reconnect()` — повторное подключение к MME; `injectProcedure("S1AP:S1_SETUP")` / `"S1AP:RESET"`
- `LTEStack` регистрирует `s1ap_` в `LinkRegistry` при создании

PCAP-трассировка (S1AP/X2AP/GTP-U):
```cpp
s1ap.enablePcap("s1ap_trace.pcap");   // S1AP → IPv4+SCTP PPID=18
x2ap.enablePcap("x2ap_trace.pcap");   // X2AP → IPv4+UDP  port=36422
s1u.enablePcap ("s1u_trace.pcap");    // GTP-U → IPv4+UDP port=2152
```
Файлы открываются Wireshark без дополнительных настроек (LINKTYPE_RAW = raw IPv4).

#### Просмотр трасс в Wireshark

**Открытие файла:**
```
File → Open → выберите s1ap_trace.pcap / x2ap_trace.pcap / s1u_trace.pcap
```

**S1AP (файл `s1ap_trace.pcap`):**
- Wireshark автоматически распознаёт S1AP по SCTP PPID = 18 (`nas-eps` / `s1ap`).
- Фильтр для просмотра только S1AP: `sctp && sctp.ppi == 18`
- Если декодирование AS PDU не срабатывает автоматически:
  `Analyze → Decode As → SCTP PP ID 18 → S1AP`
- Полезные колонки: *Frame*, *Time*, *Info* (название процедуры), *src IP / dst IP*.

**X2AP (файл `x2ap_trace.pcap`):**
- Транспорт UDP, порт 36422. Фильтр: `udp.port == 36422`
- Декодирование X2AP вручную, если не сработало автоматически:
  `Analyze → Decode As → UDP port 36422 → X2AP`

**GTP-U (файл `s1u_trace.pcap`):**
- Wireshark распознаёт GTP-U по UDP порту 2152 автоматически.
- Фильтр для GTP-U: `gtpv1`
- Просмотр инкапсулированного IP-пакета: раскройте дерево *GTP → IP → TCP/UDP*.
- Полезный фильтр Inner IP: `ip.addr == <адрес UE>` при включённой опции
  *Edit → Preferences → Protocols → GTP → Dissect GTP PDU as IP*.

**Отключение проверки контрольных сумм (рекомендуется):**

Симулятор устанавливает SCTP checksum = 0 и UDP checksum = 0.
Чтобы Wireshark не помечал пакеты как `[Bad checksum]`:
```
Edit → Preferences → Protocols → SCTP → uncheck "Verify checksum"
Edit → Preferences → Protocols → UDP  → uncheck "Validate the UDP checksum if possible"
```

**Полезные фильтры отображения:**

| Цель | Фильтр |
|------|--------|
| Все S1AP кадры | `sctp.ppi == 18` |
| Только Setup-процедуры | `s1ap` (если PPID распознан) |
| X2AP трафик | `udp.port == 36422` |
| GTP-U трафик | `udp.port == 2152` |
| По IP eNB (127.0.0.1) | `ip.src == 127.0.0.1` |
| По IP MME | `ip.dst == <mmeAddr>` |
| Временной диапазон | `frame.time_relative >= 0.1 && frame.time_relative <= 0.5` |

---

### OMS — Operations & Maintenance

Соответствует **3GPP TS 32.600** (Network Resource Model) и **TS 32.111** (Fault Management).

```
OMS (синглтон)
 ├── Fault Management
 │   ├── raiseAlarm(source, description, severity) → alarmId
 │   ├── clearAlarm(alarmId)
 │   └── getActiveAlarms() → []Alarm
 ├── Performance Management
 │   ├── updateCounter(name, value, unit)
 │   └── printPerformanceReport()          ← вызывается каждые 30 с
 ├── State Management
 │   └── setNodeState(UNLOCKED | LOCKED | SHUTTING_DOWN)
 └── KPI Threshold Alarms
     ├── setKpiThreshold(counterName, {threshold, belowIsAlarm, severity, description})
     └── removeKpiThreshold(counterName)    ← сбрасывает активную аварию
```

**Уровни аварий** (AlarmSeverity):
- `WARNING` — незначительная деградация (например, высокий CoS)
- `MINOR` / `MAJOR` — частичная потеря функциональности
- `CRITICAL` — полный отказ подсистемы (RF hardware FAULT)

**Счётчики производительности** (обновляются из стеков):

| Счётчик | Откуда | Описание |
|---------|--------|----------|
| `lte.connectedUEs` | `LTEStack` | Текущее число подключённых UE |
| `lte.rrc.attempts` | `LTEStack::admitUE()` | Всего попыток RRC Setup |
| `lte.rrc.successes` | `LTEStack::admitUE()` | Успешных RRC Setup |
| `lte.rrc.successRate.pct` | `LTEStack::admitUE()` | RRC success rate (%) |
| `lte.erab.setups` | `LTEStack::setupERAB()` | Успешных ERAB Setup |
| `lte.erab.drops` | `LTEStack::releaseUE()` | Сброшенных bearer (внезапный release) |
| `lte.erab.dropRate.pct` | оба выше | E-RAB drop rate (%) |
| `lte.ho.attempts` | `X2APLink::handoverRequest()` | Попыток Handover (X2) |
| `lte.ho.successes` | `X2APLink::onRxPacket()` (HO_ACK) | Успешных Handover |
| `lte.ho.successRate.pct` | оба выше | HO success rate (%) |
| `gsm.connectedUEs` | `GSMStack` | Текущее число подключённых UE |
| `umts.connectedUEs` | `UMTSStack` | Текущее число подключённых UE |

**Порог-триггер аварий (KPI Threshold Alarms):**

`setKpiThreshold()` регистрирует порог для счётчика. Каждый раз при вызове `updateCounter()` OMS автоматически:
- **поднимает** аварию (`raiseAlarm`), если значение пересекло порог (в любую из сторон, в зависимости от `belowIsAlarm`)
- **снимает** аварию (`clearAlarm`), если значение вернулось в норму

```cpp
// Пример: аварий при падении RRC success rate ниже 80 %
oms.setKpiThreshold("lte.rrc.successRate.pct", {
    80.0,                  // threshold
    true,                  // belowIsAlarm: низкое значение — авария
    AlarmSeverity::MAJOR,
    "RRC setup success rate below 80%"
});

// Рекомендуемые пороги
// lte.rrc.successRate.pct  < 80 %  → MAJOR
// lte.ho.successRate.pct   < 90 %  → MAJOR
// lte.erab.dropRate.pct    > 5  %  → MINOR
```

---

### 5G NR стек

Соответствует **3GPP TS 38.211** (физические каналы NR), **TS 38.213** (тайминг SSB), **TS 38.473** (F1AP gNB-DU ↗ gNB-CU).

#### Типы данных в `types.h`

**`NRScs`** — межносубнесущее расстояние (параметр µ, TS 38.211 §4.2 Table 4.2-1):

| Значение | МЖН | Полос | Диапазон | Слотов/фрейм |
|----------|-------|--------|---------|-------------|
| `SCS15`  | 15 кГц | 1 мс  | FR1     | 10          |
| `SCS30`  | 30 кГц | 0.5 мс | FR1     | 20          |
| `SCS60`  | 60 кГц | 0.25 мс| FR1/FR2 | 40          |
| `SCS120` | 120 кГц| 0.125 мс| FR2 (mmWave) | 80     |

**`NRCellConfig`** — полная конфигурация ячейки gNB-DU:

| Поле | Тип | Описание |
|-------|------|----------|
| `cellId` | `CellId` | Внутренний ID ячейки |
| `nrArfcn` | `uint32_t` | NR-ARFCN несущей DL (TS 38.101-1 §5.4.2) |
| `scs` | `NRScs` | Межносубнесущее расстояние |
| `band` | `uint8_t` | Операционный диапазон NR (1 = n1 FDD FR1 2100 МГц, 78 = n78 TDD FR1 3.5 ГГц) |
| `gnbDuId` | `uint64_t` | 36-бит gNB-DU ID (TS 38.473 §9.3.1.9) |
| `nrCellIdentity` | `uint64_t` | 36-бит NCI = gNB-ID \|\| Cell-ID |
| `nrPci` | `uint16_t` | Physical Cell Identity 0–1007 |
| `ssbPeriodMs` | `uint8_t` | Период SSB: 5/10/20/40/80/160 мс |
| `cuAddr` / `cuPort` | `string`/`uint16_t` | Адрес gNB-CU, F1AP SCTP-порт (38472) |

**`NRSSBlock`** — один блок SS/PBCH (TS 38.211 §7.4.3):
- `pss` (127 байт) — m-последовательность, `N_ID_2 = PCI % 3`
- `sss` (127 байт) — Gold-последовательность, `N_ID_1 = PCI / 3`
- `pbch` (7 байт) — MIB: SFN (10 бит), SCS, halfFrame, PCI, NR-ARFCN
- `ssbIdx` — индекс луча 0–3 (до FR2 — до 63)

#### Физический уровень — `NRPhy`

```
subframeThread (1×std::thread) → tick() [каждые 1 мс]
                                     │
                                     ├── измерение SS-RSRP (симул., -80 дБм)
                                     ├── isSSBSubframe()  → true при sfIdx_==0
                                     │   и (absMs % ssbPeriodMs)==0
                                     │       │
                                     │       ├── цикл ssbIdx 0–3 (FR1, 4 луча):
                                     │       │   buildPSS(pci)  → 127 BPSK-символов
                                     │       │   buildSSS(pci)  → 127 BPSK-символов
                                     │       │   buildPBCH(сфн, полуфрейм) → 7 б. MIB
                                     │       └── ssbCb_(ssBlock) → NRStack
                                     └── sfIdx_++ ; sfn_ = (sfn_+1) % 1024
```

**PSS** (§ 7.4.2.2.1): m-последовательность длиной 127, регистр `0x74`.
`d(n) = 1 − 2·x((n + 43·N_ID_2) % 127)`, `N_ID_2 = PCI % 3`.

**SSS** (§ 7.4.2.3.1): Gold-последовательность длиной 127 из двух MLS x₀ и x₁.
`m0 = 15·(N_ID_1/112) + 5·N_ID_2`, `m1 = N_ID_1 % 112`; `N_ID_1 = PCI / 3`.

**PBCH** (структура MIB §§TS 38.331 §6.2.2):
- Биты системного номера фрейма SFN[9:2], SFN[1:0] (заполняются из `sfn`)
- SCS, Half-frame bit, nrPci (9 бит), nrArfcn (20 бит), CRC-stub

**SFN-таймер**: 0–1023 (цикл = 10.24 с), разворачивается автоматически после 1024 фреймов.

Public API `NRPhy`:
```cpp
bool     start();                     // запуск
 void     stop();                      // остановка
void     tick();                      // +1 мс: SSB если пришло время
void     setSSBCallback(SSBCallback); // подписка на SSB-события
uint32_t currentSFN();               // SFN 0–1023
uint32_t ssbTxCount();               // счётчик переданных SSB
double   measuredSSRSRP();           // симул. SS-RSRP (дБм)
```

#### Контроллер ячейки — `NRStack`

```
NRStack(rf, NRCellConfig)
 │
 ├── start()  →  создаёт NRPhy, запускает subframeThread (1 мс/тик)
 ├── admitUE(imsi, cqi=9) → uint16_t C-RNTI
 ├── releaseUE(crnti)
 ├── connectedUECount()   → size_t
 ├── buildF1SetupRequest() → ByteBuffer  (собирает gNB-DU Name = "RBS-gNB-DU-<cellId>")
 ├── handleF1SetupResponse(pdu) → bool  (принимает Response или Failure)
 └── currentSFN() → uint32_t
```

#### F1AP кодек — `f1ap_codec`

Протокол F1 Application Protocol (TS 38.473 §8.7.1) — интерфейс gNB-DU ↗ gNB-CU по F1 (SCTP 38472).
Кодирование — компактный TLV-формат (магик `0x3847`, big-endian), самостоятельно без asn1c.

Реализованные процедуры:

| Сообщение | Направление | Ссылка |
|----------|-------------|--------|
| F1 Setup Request | gNB-DU → gNB-CU | TS 38.473 §8.7.1.2 |
| F1 Setup Response | gNB-CU → gNB-DU | TS 38.473 §8.7.1.3 |
| F1 Setup Failure | gNB-CU → gNB-DU | TS 38.473 §8.7.1.4 |

**F1 Setup Request** (`F1SetupRequest`) — отправляется gNB-DU при подключении к CU:
- `gnbDuId` (36 бит), `gnbDuName` (строка), `transactionId`, список `servedCells`
- Каждая `F1ServedCell`: NCI (36 бит), NR-ARFCN, SCS, PCI, TAC

**F1 Setup Response** (`F1SetupResponse`) — Ответ CU: имя CU + список активированных NCI.

**F1 Setup Failure** (`F1SetupFailure`) — Сбой: `causeType` + `causeValue` (radio/transport/protocol/misc).

```cpp
// Пример использования
F1SetupRequest req;
req.gnbDuId = 0x123456789ULL;
req.gnbDuName = "RBS-gNB-DU-4";
req.servedCells.push_back({0xABCDE01, 428000, NRScs::SCS15, 500, 1}); // n1 FDD DL 2140 МГц
ByteBuffer pdu = encodeF1SetupRequest(req);

F1SetupResponse rsp;
bool ok = decodeF1SetupResponse(pdu, rsp);
```

---

### Управление интерфейсами (Link Management)

Система управления сетевыми интерфейсами Abis/Iub/S1 — централизованный мониторинг, трассировка, блокировка и инжекция процедур.

#### Компоненты

| Компонент | Файл | Описание |
|-----------|------|---------|
| `LinkController` | `src/common/link_controller.h` | Миксин: ring-buffer трассы (100 PDU) + блок-список типов |
| `LinkRegistry` | `src/common/link_registry.h/.cpp` | Singleton-реестр всех интерфейсов |
| `AbisOml` | `src/gsm/abis_link.h/.cpp` | GSM Abis/OML: наследует `LinkController` |
| `IubNbap` | `src/umts/iub_link.h/.cpp` | UMTS Iub/NBAP: наследует `LinkController` |
| `S1APLink` | `src/lte/s1ap_link.h/.cpp` | LTE S1AP: наследует `LinkController` |

#### Конфигурация

```ini
[gsm]
bsc_addr =          # пусто = simulation mode (Abis не поднимается)
bsc_port = 3002

[umts]
rnc_addr =          # пусто = simulation mode (Iub симулируется)
rnc_port = 25412

[lte]
mme_addr = 127.0.0.1
mme_port = 36412
```

#### Провайдеры трассировки и инжекции

| Интерфейс | Имя в реестре | Инжектируемые процедуры |
|-----------|--------------|-------------------------|
| Abis (GSM↔BSC) | `abis` | `OML:OPSTART` |
| Iub (UMTS↔RNC) | `iub` | `NBAP:CELL_SETUP`, `NBAP:RESET` |
| S1 (LTE↔MME) | `s1` | `S1AP:S1_SETUP`, `S1AP:RESET` |

#### REST API (управление через HTTP)

См. раздел «REST API — Web Dashboard» ниже, EndPoints `/api/v1/links/*`.

---

### REST API — Web Dashboard

Встроенный HTTP-сервер на базе **cpp-httplib** v0.18.5 (без OpenSSL, header-only).
Класс `RestServer` (модуль `rbs_api`) запускается в фоновом потоке и предоставляет JSON-интерфейс к OMS.

```cpp
RestServer srv(8080);
srv.start();           // неблокирующий запуск
srv.stop();
```

**Endpoints:**

| Метод | URL | Описание |
|------|----|----------|
| `GET` | `/api/v1/status` | Версия, `nodeState` (UNLOCKED/LOCKED/SHUTTING_DOWN), список RAT |
| `GET` | `/api/v1/pm` | Все PM-счётчики OMS (`getAllCounters()`) |
| `GET` | `/api/v1/alarms` | Активные аварии с severity |
| `POST` | `/api/v1/admit` | Тело: `{"imsi":N,"rat":"LTE"}` → `{"status":"ok","crnti":N}` |
| `GET` | `/api/v1/links` | Список всех интерфейсов: имя, RAT, peer, connected, blocked |
| `GET` | `/api/v1/links/{name}/trace` | Трасса последних PDU (`?limit=N`, по умолчанию 50) |
| `POST` | `/api/v1/links/{name}/connect` | Поднять интерфейс (reconnect) |
| `POST` | `/api/v1/links/{name}/disconnect` | Опустить интерфейс |
| `GET` | `/api/v1/links/{name}/inject` | Список процедур для инжекции |
| `POST` | `/api/v1/links/{name}/inject` | Тело: `{"procedure":"S1AP:S1_SETUP"}` → инжектировать |
| `POST` | `/api/v1/links/{name}/block` | Тело: `{"type":"OML:OPSTART"}` → заблокировать тип сообщения |
| `POST` | `/api/v1/links/{name}/unblock` | Тело: `{"type":"OML:OPSTART"}` → снять блокировку |

Примеры ответов:

```json
// GET /api/v1/status
{
  "version": "1.0.0",
  "nodeState": "UNLOCKED",
  "rats": ["GSM","UMTS","LTE","NR"]
}

// GET /api/v1/pm
{
  "counters": [
    {"name": "lte.connectedUEs", "value": 3},
    {"name": "lte.rrc.successRate.pct", "value": 100}
  ]
}

// GET /api/v1/alarms
{
  "alarms": [
    {"id": 1, "source": "RFHardware", "description": "PA overheat", "severity": "MAJOR"}
  ]
}

// POST /api/v1/admit  body: {"imsi":123456789,"rat":"LTE"}
{"status": "ok", "crnti": 101}

// GET /api/v1/links
[
  {"name":"abis","rat":"GSM","peer":":3002","connected":false,"blocked":[]},
  {"name":"iub","rat":"UMTS","peer":":25412","connected":false,"blocked":[]},
  {"name":"s1","rat":"LTE","peer":"127.0.0.1:36412","connected":true,"blocked":[]}
]

// GET /api/v1/links/s1/trace?limit=5
{"messages":[
  {"tx":true,"type":"S1AP:1","summary":"proc=S1AP:1 len=42 OK","timestampMs":1744300800000}
]}

// POST /api/v1/links/s1/inject  body: {"procedure":"S1AP:S1_SETUP"}
{"status":"ok"}

// POST /api/v1/links/iub/block  body: {"type":"NBAP:RESET"}
{"status":"ok"}
```

**Особенности:**
- Биндинг на `127.0.0.1` (не `0.0.0.0`) — USB​безопасность в локальной сети
- `port=0` — автоматически выбирается свободный эфемерный порт (рекомендуется для тестов)
- Нет SSL/TLS зависимостей (`HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF`)
- Тесты используют `httplib::Client` для ОТП через те же заголовки cpp-httplib

---

### Главный контроллер (main)

#### Класс `RadioBaseStation`

```
RadioBaseStation(configPath, mode)         ← mode: GSM | UMTS | LTE | NR | ALL
 │
 ├── Config::instance().loadFile()         ← читает rbs.conf
 ├── if GSM/ALL:  RFHardware(2Tx,2Rx) + GSMStack
 ├── if UMTS/ALL: RFHardware(2Tx,2Rx) + UMTSStack
 ├── if LTE/ALL:  RFHardware(2Tx,4Rx) + LTEStack
 ├── if NR/ALL:   RFHardware(4Tx,4Rx) + NRStack    ← SCS 30 кГц, SSB период 20 мс
 ├── setAlarmCallback() → OMS              ← аппаратные аварии
 ├── RestServer(8080).start()              ← REST API / Web Dashboard (п.18)
 │
 ├── start()
 │   ├── OMS → UNLOCKED
 │   ├── rf.initialise() + selfTest()  (только активные RAT)
 │   ├── запуск выбранных стеков
 │   └── баннер ONLINE [GSM (2G) | UMTS (3G) | LTE (4G) | NR (5G)]
 │
 ├── runDemo()
 │   └── для каждого активного RAT:
 │       admitUE → отправить данные → sleep(2s) → printStats() → releaseUE
 │       OMS.printPerformanceReport()
 │
 └── mainLoop()
     └── каждые 30 с: OMS.printPerformanceReport()
         до SIGINT/SIGTERM → graceful shutdown
```

#### Точка входа `main()`
```cpp
int main(int argc, char* argv[]) {
    // 1. Установка обработчиков сигналов (SIGINT, SIGTERM)
    // 2. Парсинг argv[1]=configPath, argv[2]=rat (gsm|umts|lte)
    //    если argv[2] отсутствует или неизвестен — режим ALL
    // 3. Создание и запуск RadioBaseStation(configPath, rat)
    // 4. runDemo() — демонстрация работы выбранного RAT
    // 5. mainLoop() — периодические PM-отчёты до Ctrl+C
    // 6. stop() — остановка активных стеков и потоков
}
```

---

## Диаграмма потоков данных

### Нисходящий (DL) путь данных LTE

```
Приложение
    │  IP-пакет
    ▼
LTEStack::sendIPPacket()
    │
    ▼
PDCP::processDlPacket()
    ├── addHeader()      → PDCP SN + D/C бит
    └── cipher()         → AES-128 CTR (или NULL)
    │
    ▼
LTEMAC::runDlScheduler()
    ├── PF метрика       → выбор UE и назначение RB
    └── cqiToMcs()       → выбор схемы модуляции
    │
    ▼
LTEPhy::transmitSubframe()
    ├── buildPDCCH()     → DCI (управляющие данные)
    ├── buildPDSCH()     → TB (пользовательские данные)
    └── ofdmModulate()   → IQ-выборки
    │
    ▼
RFHardware::transmit()   → «эфир»
```

### Восходящий (UL) путь данных LTE

```
RFHardware::receive()    ← IQ из «эфира»
    │
    ▼
LTEPhy::receiveSubframe()
    │
    ▼
LTEMAC::handleSchedulingRequest() / handleBSR()
    └── UL grant → PHY
    │
    ▼
PDCP::processUlPDU()
    ├── decipher()
    └── removeHeader()
    │
    ▼
LTEStack::receiveIPPacket() → IP-пакет
```

---

## Конфигурационный файл rbs.conf

```ini
[gsm]
cell_id       = 1
arfcn         = 60       # P-GSM 900: ARFCN 1–124 → DL 935–960 МГц
tx_power_dbm  = 43.0     # 20 Вт
bsic          = 10       # ID ячейки (0–63)
lac           = 1000     # Location Area Code
mcc           = 250      # Россия
mnc           = 1        # МТС
bsc_addr      =          # адрес BSC для Abis (пусто = simulation mode)
bsc_port      = 3002

[umts]
cell_id       = 2
uarfcn        = 10700    # UMTS Band I DL: 10562–10838 → 2110–2170 МГц
tx_power_dbm  = 43.0
psc           = 64       # Primary Scrambling Code (0–511)
lac           = 1000
rac           = 1        # Routing Area Code
rnc_addr      =          # адрес RNC для Iub (пусто = simulation mode)
rnc_port      = 25412

[lte]
cell_id       = 3
earfcn        = 1800     # Band 3 → DL ~1865 МГц
tx_power_dbm  = 43.0
pci           = 300      # Physical Cell Identity (0–503)
tac           = 1        # Tracking Area Code
num_antennas  = 2        # Порты Tx: 1, 2 или 4
mme_addr      =          # адрес MME (пусто = simulation mode)
mme_port      = 36412

[logging]
level         = INFO     # DEBUG | INFO | WARNING | ERROR | CRITICAL
log_file      = rbs.log
```

---

## Сборка

### Требования

| Инструмент   | Минимальная версия |
|--------------|--------------------|
| CMake        | 3.16               |
| Компилятор   | MSVC 19.30+ (VS 2022) или GCC 11+ / Clang 14+ |
| C++ стандарт | C++17              |

### Под Windows (Visual Studio)

```powershell
# Из корня проекта:
cmake -S . -B build
cmake --build build --config Release

# Исполняемый файл:
.\build\Release\rbs_node.exe rbs.conf
```

### Под Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

./build/rbs_node rbs.conf gsm    # только GSM
./build/rbs_node rbs.conf umts   # только UMTS
./build/rbs_node rbs.conf lte    # только LTE
./build/rbs_node rbs.conf nr     # только 5G NR
./build/rbs_node rbs.conf        # все четыре RAT (ALL)
```

### Опции CMake

| Опция                | По умолчанию | Описание                              |
|----------------------|--------------|---------------------------------------|
| `RBS_ENABLE_TESTS`   | `ON`         | Сборка модульных тестов               |
| `RBS_ENABLE_ASAN`    | `OFF`        | AddressSanitizer (только GCC/Clang)   |

---

## Запуск

Второй аргумент задаёт стандарт радиодоступа. При отсутствии аргумента запускаются все четыре стека одновременно.

```powershell
# Только GSM (2G)
.\build\Release\rbs_node.exe rbs.conf gsm

# Только UMTS (3G)
.\build\Release\rbs_node.exe rbs.conf umts

# Только LTE (4G)
.\build\Release\rbs_node.exe rbs.conf lte

# Только NR (5G)
.\build\Release\rbs_node.exe rbs.conf nr

# Все четыре RAT одновременно (режим по умолчанию)
.\build\Release\rbs_node.exe rbs.conf

# Справка по аргументам
.\build\Release\rbs_node.exe --help

# Остановка — Ctrl+C (SIGINT)
# Узел переходит в состояние SHUTTING_DOWN и корректно
# останавливает все инициализированные потоки перед выходом.
```

**Пример вывода (режим `lte`):**
```
2026-04-08 00:32:03.829 [INFO ] [RBS] Radio Base Station v1.0.0 starting...
2026-04-08 00:32:03.830 [INFO ] [RBS] Config: rbs.conf  RAT: LTE (4G)
2026-04-08 00:32:03.831 [INFO ] [Config] Loaded configuration from rbs.conf
2026-04-08 00:32:03.831 [INFO ] [OMS] Node state → UNLOCKED
2026-04-08 00:32:03.832 [INFO ] [RFHardware] Self-test PASSED
2026-04-08 00:32:03.859 [INFO ] [LTEStack] LTE cell 3 started (PCI=300, BW=100 RBs)
2026-04-08 00:32:03.859 [INFO ] [RBS] ====================================================
2026-04-08 00:32:03.859 [INFO ] [RBS]   Radio Base Station ONLINE  [LTE (4G)]
2026-04-08 00:32:03.859 [INFO ] [RBS]   LTE  cell 3  EARFCN=1800  PCI=300  BW=20 MHz
2026-04-08 00:32:03.859 [INFO ] [RBS] ====================================================
2026-04-08 00:32:03.860 [INFO ] [RBS] [LTE ] UE RNTI=1 admitted on EARFCN=1800 PCI=300 CQI=12 → MCS=17
```

### Open5GS interop (Linux, SCTP)

Короткая проверка S1 Setup Request/Response с реальным MME (Open5GS):

Детальный пошаговый quickstart с точными Linux-командами: `OPEN5GS_LINUX_QUICKSTART.md`.

1. Поднимите Open5GS MME на Linux и убедитесь, что он слушает SCTP `36412` на нужном IP.
2. В `rbs.conf` укажите `lte.mme_addr=<IP Linux-хоста с Open5GS>` и `lte.mme_port=36412`.
3. Запустите узел в LTE-режиме:

```bash
./build/rbs_node rbs.conf lte
```

4. Убедитесь в логах RBS, что отправлен S1 Setup Request.
5. Убедитесь в логах RBS, что получен и декодирован S1 Setup Response:

```text
[INFO ] [S1AP] [<enb-id>] RX S1SetupResponse decoded from <mme-ip>:36412 (len=<n>)
```

6. Дополнительно проверьте в логах Open5GS успешную обработку S1 Setup.

Примечание: на Windows без нативного SCTP возможен fallback-транспорт, но для реального interop с Open5GS нужен нативный SCTP (рекомендуется запуск RBS на Linux).

---

## Руководство по использованию

### Содержание

- [GSM (2G)](#gsm-2g-использование)
- [UMTS (3G)](#umts-3g-использование)
- [LTE (4G)](#lte-4g-использование)
- [NR NSA (5G EN-DC)](#nr-nsa-5g-en-dc-использование)
- [REST API](#rest-api-использование)

---

### GSM (2G) использование

**Конфигурация `[gsm]` в rbs.conf:**
```ini
[gsm]
cell_id      = 1
arfcn        = 60        ; P-GSM 900, DL 935–960 МГц (ARFCN 1–124)
tx_power_dbm = 43.0
bsic         = 10        ; 0–63, Base Station Identity Code
lac          = 1000
mcc          = 250
mnc          = 1
```

**Программный API (`GSMStack`):**
```cpp
#include "gsm/gsm_stack.h"

gsm->start();

// Подключить UE (IMSI → RNTI):
RNTI rnti = gsm->admitUE(100000000000001ULL);

// Отправить/принять данные (TCH-фрейм, 13 байт GSM Voice):
ByteBuffer voice(13, 0xAA);
gsm->sendData(rnti, voice);

ByteBuffer rxBuf;
gsm->receiveData(rnti, rxBuf);

// Статистика → LOG_INFO:
gsm->printStats();

// Отключить:
gsm->releaseUE(rnti);
```

**OMS счётчик:** `gsm.connectedUEs`

---

### UMTS (3G) использование

**Конфигурация `[umts]` в rbs.conf:**
```ini
[umts]
cell_id      = 2
uarfcn       = 10700     ; Band I DL → 2110–2170 МГц
tx_power_dbm = 43.0
psc          = 64        ; Primary Scrambling Code, 0–511
lac          = 1000
rac          = 1
```

**Программный API (`UMTSStack`):**
```cpp
#include "umts/umts_stack.h"

// Обычный DCH bearer (по умолчанию SF=16 = ~384 кбит/с):
RNTI rnti = umts->admitUE(200000000000002ULL);

// С явным Spreading Factor (SF4=макс.скорость … SF256=макс.покрытие):
RNTI rnti = umts->admitUE(imsi, SF::SF32);

// HSDPA bearer (HS-DSCH, до 14.4 Мбит/с, TS 25.308):
RNTI rnti = umts->admitUEHSDPA(imsi);

// E-DCH bearer (Enhanced UL, HSUPA):
RNTI rnti = umts->admitUEEDCH(imsi);

// Переконфигурировать Spreading Factor:
umts->reconfigureDCH(rnti, SF::SF8);

// Soft Handover — обновление Active Set:
MeasurementReport report;
umts->softHandoverUpdate(report);
const auto& activeSet = umts->activeSet(rnti);

umts->sendData(rnti, data);
umts->releaseUE(rnti);
```

**Доступные Spreading Factor:** `SF4, SF8, SF16, SF32, SF64, SF128, SF256`

**OMS счётчик:** `umts.connectedUEs`

---

### LTE (4G) использование

**Конфигурация `[lte]` в rbs.conf:**
```ini
[lte]
cell_id      = 3
earfcn       = 1800      ; Band 3 DL ~1865 МГц
tx_power_dbm = 43.0
pci          = 300       ; Physical Cell Identity, 0–503
tac          = 1
num_antennas = 2         ; Tx-порты: 1, 2 или 4
mme_addr     = 127.0.0.1 ; адрес MME (Open5GS)
mme_port     = 36412
```

**Базовый сценарий:**
```cpp
#include "lte/lte_stack.h"

// Admit UE (IMSI, CQI=12 → MCS=17, ~20 МГц eMBB):
RNTI rnti = lte->admitUE(300000000000003ULL, /*CQI=*/12);

// Обновить CQI динамически:
lte->updateCQI(rnti, 9);

// Отправить IP-пакет (DL):
ByteBuffer ipPkt(100, 0x42);
lte->sendIPPacket(rnti, /*bearerId=*/1, ipPkt);

// Принять IP-пакет (UL):
ByteBuffer rxPkt;
lte->receiveIPPacket(rnti, 1, rxPkt);

lte->printStats();
lte->releaseUE(rnti);
```

**Carrier Aggregation (LTE-A, до 5 CC × 20 МГц = 100 МГц):**
```cpp
RNTI rnti = lte->admitUECA(imsi, /*ccCount=*/2, /*CQI=*/12);
```

**GTP-U bearer к SGW (S1-U интерфейс):**
```cpp
GTPUTunnel sgw;
sgw.teid       = 0xABCD1234;
sgw.remoteIPv4 = 0x7F000001;  // 127.0.0.1
sgw.udpPort    = 2152;

lte->setupERAB(rnti, /*erabId=*/1, sgw);
// ...
lte->teardownERAB(rnti, 1);
```

**CSFB — голос через LTE (LTE → GSM/UMTS, TS 23.272):**
```cpp
lte->triggerCSFB(rnti, /*gsmArfcn=*/60);
// → RRC Connection Release + перенаправление на ARFCN 60
```

**KPI пороги (автоматические аварии OMS):**
```cpp
auto& oms = OMS::instance();
oms.setKpiThreshold("lte.rrc.successRate.pct",
    {80.0, /*belowIsAlarm=*/true, AlarmSeverity::MAJOR, "RRC success rate below 80%"});
oms.setKpiThreshold("lte.ho.successRate.pct",
    {90.0, true, AlarmSeverity::MAJOR, "HO success rate below 90%"});
oms.setKpiThreshold("lte.erab.dropRate.pct",
    {5.0, false, AlarmSeverity::MINOR, "E-RAB drop rate above 5%"});
```

**OMS счётчики:** `lte.connectedUEs`, `lte.rrc.successRate.pct`, `lte.ho.successRate.pct`, `lte.erab.dropRate.pct`

---

### NR NSA (5G EN-DC) использование

**Конфигурация `[nr]` в rbs.conf:**
```ini
[nr]
cell_id        = 4
nr_arfcn       = 428000      # n1 FDD DL, центр 2140 МГц (TS 38.101-1 §5.4.2.1)
band           = 1           # n1 FDD FR1: DL 2110–2170 МГц / UL 1920–1980 МГц
pci            = 400         # NR PCI, 0–1007
ssb_period_ms  = 20
cu_addr        = 127.0.0.1
cu_port        = 38472
```

> **Примечание:** С переходом на n1 FDD используется SCS 15 кГц (µ=0, `NRScs::SCS15`) вместо SCS 30 кГц. Параметр `scs` задаётся автоматически через `Config::buildNRConfig()` на основе диапазона.

**Конфигурация `[endc]` в rbs.conf (EN-DC, TS 37.340):**
```ini
[endc]
enabled        = true        # активировать EN-DC при запуске в режиме all
option         = 3a          # 3 | 3a | 3x (см. таблицу вариантов ниже)
x2_addr        = 127.0.0.1   # X2 адрес Secondary Node (NR gNB)
x2_port        = 36422       # X2AP порт (TS 36.422)
enb_bearer_id  = 5           # E-RAB ID на Master Node (LTE), 1–15
scg_drb_id     = 1           # DRB ID на Secondary Node (NR), 1–11
```

> Все EN-DC параметры читаются через `Config::buildENDCConfig()`. При `enabled = false` X2-соединение не устанавливается даже при одновременном запуске LTE+NR.

**Чистый NR — F1AP (gNB-DU → gNB-CU):**
```cpp
#include "nr/nr_stack.h"

nr->start();  // запускает NRPhy: SSB (PSS/SSS/PBCH) каждые 20 мс

// F1 Setup Request — подключение к gNB-CU (TS 38.473 §8.7.1):
ByteBuffer req = nr->buildF1SetupRequest();
// ... отправить по SCTP на cu_addr:cu_port ...
// При получении ответа:
bool ok = nr->handleF1SetupResponse(responsePdu);

// Текущий SFN (0–1023, цикл 10.24 с):
uint32_t sfn = nr->currentSFN();

nr->printStats();
```

**EN-DC NSA — Option 3a (SCG bearer, основной вариант):**

UE одновременно подключено к LTE (Master Node) и NR (Secondary Node):

```cpp
// 1. LTE UE уже подключён:
RNTI lteCrnti = lte->admitUE(300000000000003ULL, 12);

// 2. Конфигурация SCG bearer:
rbs::DCBearerConfig bearer;
bearer.enbBearerId = 5;                          // E-RAB на LTE (MN)
bearer.type        = rbs::DCBearerType::SCG;     // NR-only leg
bearer.scgLegDrbId = 1;                          // DRB1 на NR
bearer.nrCellId    = nr->config().nrCellIdentity;

// 3. NR сторона принимает SCG bearer → возвращает NR C-RNTI:
uint16_t nrCrnti = nr->acceptSCGBearer(lteCrnti, bearer);

// 4. MN (LTE) инициирует X2 SgNB Addition Request:
x2endc->sgNBAdditionRequest(lteCrnti, rbs::ENDCOption::OPTION_3A, {bearer});

// 5. Проверить статус:
auto opt = nr->endcOption(lteCrnti);  // → ENDCOption::OPTION_3A
size_t endcUEs = nr->endcUECount();

// 6. Освобождение:
nr->releaseSCGBearer(lteCrnti);
nr->releaseUE(nrCrnti);
```

**Варианты EN-DC:**

| Option | `ENDCOption` | Описание |
|--------|-------------|----------|
| Option 3 | `OPTION_3` | Split-bearer: PDCP на MN (LTE) |
| Option 3a | `OPTION_3A` | SCG bearer: весь трафик через NR |
| Option 3x | `OPTION_3X` | Split-bearer: PDCP на SN (NR) |

**OMS счётчик:** `nr.connectedUEs`

---

### REST API использование

REST-сервер запускается автоматически на порту **8080** (локальный биндинг `127.0.0.1`).

#### Управление интерфейсами Abis/Iub/S1

```bash
# Список всех интерфейсов и их состояние:
curl http://127.0.0.1:8080/api/v1/links

# Трасса последних 10 PDU на S1:
curl 'http://127.0.0.1:8080/api/v1/links/s1/trace?limit=10'

# Поднять Iub вручную (если rnc_addr прописан в rbs.conf):
curl -X POST http://127.0.0.1:8080/api/v1/links/iub/connect

# Инжектировать S1 Setup Request:
curl -X POST http://127.0.0.1:8080/api/v1/links/s1/inject \
     -H 'Content-Type: application/json' \
     -d '{"procedure":"S1AP:S1_SETUP"}'

# Список доступных процедур для Abis:
curl http://127.0.0.1:8080/api/v1/links/abis/inject
# → {"procedures":["OML:OPSTART"]}

# Заблокировать NBAP Reset на Iub:
curl -X POST http://127.0.0.1:8080/api/v1/links/iub/block \
     -H 'Content-Type: application/json' \
     -d '{"type":"NBAP:RESET"}'

# Снять блокировку:
curl -X POST http://127.0.0.1:8080/api/v1/links/iub/unblock \
     -H 'Content-Type: application/json' \
     -d '{"type":"NBAP:RESET"}'
```

Имена интерфейсов: `abis` (GSM), `iub` (UMTS), `s1` (LTE).

> **Windows:** `curl` из PowerShell/cmd — это псевдоним `Invoke-WebRequest`, который не совместим с ключами `-X`, `-H`, `-d`.
> Для работы с REST API используйте **WSL** (Windows Subsystem for Linux):
> ```powershell
> # Активировать среду WSL и перейти в неё:
> wsl
> ```
> Все примеры ниже выполняются **внутри WSL-сессии** (bash/zsh).
> Если в вашей WSL2/NAT-конфигурации `curl http://127.0.0.1:8080/...` не проходит,
> используйте вызов через Windows-стек прямо из WSL:
> ```bash
> powershell.exe -NoProfile -Command "Invoke-RestMethod http://127.0.0.1:8080/api/v1/status | ConvertTo-Json -Compress"
> ```
> Альтернативно — установить `curl.exe` отдельно или использовать Postman / [Invoke-RestMethod](https://learn.microsoft.com/powershell/module/microsoft.powershell.utility/invoke-restmethod) в PowerShell:
> ```powershell
> Invoke-RestMethod http://127.0.0.1:8080/api/v1/status
> ```

```bash
# Статус узла:
curl http://127.0.0.1:8080/api/v1/status
# → {"version":"1.0.0","nodeState":"UNLOCKED","rats":["GSM","UMTS","LTE","NR"]}

# PM счётчики (все OMS):
curl http://127.0.0.1:8080/api/v1/pm
# → {"counters":[{"name":"lte.connectedUEs","value":3},{"name":"lte.rrc.successRate.pct","value":100},...]}

# Активные аварии:
curl http://127.0.0.1:8080/api/v1/alarms
# → {"alarms":[{"id":1,"source":"RFHardware","description":"PA overheat","severity":"MAJOR"}]}

# Подключить UE (rat: GSM | UMTS | LTE | NR):
curl -X POST http://127.0.0.1:8080/api/v1/admit \
     -H "Content-Type: application/json" \
     -d '{"imsi": 300000000000003, "rat": "LTE"}'
# → {"status":"ok","crnti":101}
```

**PM Export:**
```cpp
// Сохранить снимок счётчиков в CSV:
OMS::instance().exportCsv("pm_snapshot.csv");
// Формат: timestamp,name,value,unit

// Отправить в InfluxDB (UDP Line Protocol):
OMS::instance().pushInflux("127.0.0.1:8086", "rbs_pm");
```

---

## Тесты

```powershell
cd build
ctest -C Debug --output-on-failure
```

| Тест                    | Что проверяет                                                   |
|-------------------------|-----------------------------------------------------------------|
| `test_config`           | INI-парсер, buildXxxConfig()                                    |
| `test_gsm_phy`          | Счётчики фреймов/слотов PHY, burst-пакеты                      |
| `test_lte_mac`          | CQI→MCS, PF-планировщик                                        |
| `test_pdcp`             | DL/UL loopback, SN-счётчик, шифрование                         |
| `test_lte_phy`          | PSS/SSS/PBCH, PDCCH/PDSCH                                      |
| `test_lte_rlc`          | RLC AM/UM segmentation & reassembly                            |
| `test_umts_mac`         | DCH assign/release, планировщик                                |
| `test_s1ap_codec`       | APER encode/decode: S1Setup, NAS, Paging, Reset, ErrorInd, Handover (5 msg) |
| `test_x2ap_codec`       | X2AP encode/decode: X2Setup, HO                                |
| `test_nbap_codec`       | NBAP encode/decode: RadioLinkSetup                             |
| `test_gtp_u`            | GTP-U encode/decode, S1ULink UDP loopback (eNB↔simulated SGW) |
| `test_lte_stack`        | admitUE / DL-DU путь / releaseUE                               |
| `test_gsm_stack`        | GSM admit/release, счётчики OMS                                |
| `test_umts_stack`       | UMTS admit/release                                             |
| `test_oms`              | Аварии (raise/clear), PM-метрики, состояния узла, KPI-пороги   |
| `test_gsm_rlc`          | GSM RLC AM/UM                                                  |
| `test_gsm_rr`           | GSM Radio Resource: Channel Request, IA                        |
| `test_umts_phy`         | WCDMA spreading, CPICH/SCH                                     |
| `test_umts_rlc`         | UMTS RLC                                                       |
| `test_umts_rrc`         | RRC Connection Setup / Release (UMTS)                          |
| `test_lte_rrc`          | RRC Connection Setup / Release (LTE)                           |
| `test_s1ap_link`        | S1APLink connect / s1Setup / NAS transport                     |
| `test_pcap_writer`      | PcapWriter: глобальный заголовок, SCTP/UDP записи (S1AP/X2AP/GTP-U) |
| `test_x2ap_link`        | X2AP link setup / HO signalling                                |
| `test_abis_link`        | Abis (GSM) link                                                |
| `test_iub_link`         | Iub (UMTS) link — NBAP RL Setup/Addition/Deletion, измерения   |
| `test_integration`      | Полный E2E: все RAT + OMS + admit/release                      |
| `test_mobility`         | X2-хэндовер, Inter-RAT                                         |
| `test_son`              | SON: ANR, MLB, MRO                                             |
| `test_csfb`             | CSFB: LTE→GSM/UMTS trigger, RRC Release, S1AP Extended SvcReq |
| `test_nbap_dch`         | NBAP DCH/HSDPA: CommonTransChSetup, RLReconfigPrepare/Commit, HS-DSCH, UMTS HSDPA admit |
| `test_lte_ul_phy`       | PUCCH format 1/2, PUSCH scheduling grants, SRS периодический сигнал |
| `test_lte_measurement`  | A1/A2/A3/A5 event triggers, MeasurementReport, X2 HO trigger |
| `test_nbap_edch`        | E-DCH bearer assign, NBAP RadioLinkSetup с E-DCH MAC-d flow |
| `test_umts_soft_ho`     | Active Set Add/Drop (SHO), RL Addition/Deletion NBAP, macro diversity |
| `test_lte_ca`           | Carrier Aggregation: 2CC admit, PCell/SCell планировщик, DL throughput |
| `test_rohc`             | ROHC профиль 0x0001: IPv4/UDP/RTP header compress/decompress round-trip |
| `test_nr_phy`           | NR PHY: PSS/SSS/PBCH, SSB burst, SFN timing, PCI-dependent sequences |
| `test_f1ap_codec`       | F1AP: F1 Setup Request/Response/Failure encode/decode, NRStack F1 flow |
| `test_rest_api`         | HTTP REST API: GET /status /pm /alarms, POST /admit, JSON-схема, Content-Type |
| `test_endc`             | EN-DC NSA Option 3/3a/3x: SgNB Add/Ack/Reject/Mod/Release, SCG bearer, NR C-RNTI |

Все **43 теста** проходят за ~8 с.

---

## Разработка с GitHub Copilot

Репозиторий содержит преднастроенные файлы для GitHub Copilot в папке `.github/`:

### Конвенции кода — `copilot-instructions.md`

Файл `.github/copilot-instructions.md` автоматически применяется Copilot ко всем чатам в этом воркспейсе. Содержит:
- Соглашения по именованию (PascalCase / camelCase / UPPER_CASE)
- Правила заголовков (`#pragma once`, `extern "C"` для generated-кода)
- Использование `rbs::Logger` вместо `std::cout` / `printf`
- Правила CMake (один таргет на модуль, без GLOB)
- Таблицу fix-скриптов ASN.1 и порядок их запуска

### Агент разработчика — `rbs-dev.agent.md`

Специализированный агент `.github/agents/rbs-dev.agent.md` доступен через выпадающий список агентов в Copilot Chat. Он знает:
- Структуру модулей (`rbs_common`, `rbs_hal`, `rbs_gsm`, `rbs_umts`, `rbs_lte`, `rbs_oms`)
- Роли кодеков и link-слоёв S1AP/X2AP/NBAP
- Пайплайн кодогенерации ASN.1 и правила работы с fix-скриптами

Примеры запросов:
```
Добавь IE в S1AP Setup Request
Почему X2AP link не устанавливается?
Почини include-циклы в X2AP generated headers
```

### Промпт регенерации ASN.1 — `/gen-asn1`

Файл `.github/prompts/gen-asn1.prompt.md` вызывается командой `/gen-asn1` в Copilot Chat и проводит агента через все пять шагов пайплайна:

1. `fix_asn1_encoding.py` — очистка `.asn` спек от не-ASCII
2. `gen_asn1.sh` — запуск `asn1c` для S1AP / X2AP / NBAP
3. Fix-скрипты — в правильном порядке
4. Сборка `build-asn1/` — проверка `rbs_asn1_s1ap` и `rbs_asn1_x2ap`
5. Сборка основного проекта `build/`

Промпт включает таблицу диагностики типичных ошибок (дублирующиеся символы, include-циклы, пустые union).

---

## Логирование

Лог-файл `rbs.log` создаётся в рабочей директории. Формат строки:

```
YYYY-MM-DD HH:MM:SS.mmm [LEVEL] [Источник] Сообщение
```

Пример:
```
2026-04-07 16:40:05.691 [INFO ] [LTEMAC] UE admitted RNTI=1 CQI=12
2026-04-07 16:40:07.701 [INFO ] [OMS] === Performance Report ===
2026-04-07 16:40:07.701 [INFO ] [OMS]   Active alarms: 0
```

Уровень логирования задаётся в `rbs.conf` → секция `[logging]` → ключ `level`.

---

## История разработки

| Итерация | Реализовано |
|----------|-------------|
| п.1 | Базовая архитектура: HAL, Common (types/logger/config), CMake |
| п.2 | GSM PHY (TDMA, burst), GSM MAC (SI, TCH/SDCCH), GSM Stack |
| п.3 | UMTS PHY (WCDMA, spreading, CPICH/SCH), UMTS MAC, UMTS Stack |
| п.4 | LTE PHY (OFDMA, PSS/SSS/PBCH, PDCCH/PDSCH), LTE MAC (PF-планировщик, HARQ, CQI→MCS) |
| п.5 | PDCP (AES-128 CTR, SNOW3G, ZUC), OMS (Fault/PM/State management, KPI-пороги) |
| п.6 | S1AP (18 процедур, APER через asn1c), X2AP, NBAP-stub, GTP-U, PCAP-writer |
| п.7 | RLC AM/UM (LTE/UMTS/GSM), RRC (LTE/UMTS), Abis link, Iub link, интеграционные тесты |
| п.8 | Mobility Manager (X2-хэндовер, Inter-RAT), SON (ANR, MLB, MRO) |
| п.9 | CSFB (Circuit-Switched Fallback LTE→GSM/UMTS): LTERrc, S1APLink, MobilityManager |
| п.10 | NBAP DCH/HSDPA: CommonTransChSetup, RLReconfigurePrepare/Commit, HS-DSCH bearer, UMTS admitUEHSDPA |
| п.11 | LTE UL PHY: PUCCH format 1/1a/2, PUSCH scheduling grants, SRS (Sounding Reference Signal) |
| п.12 | LTE RRC Measurement Control: A1/A2/A3/A5 event triggers, MeasurementReport, A3→X2 HO |
| п.13 | HSUPA/E-DCH: UMTSChannelType::E_DCH, UMTSMAC::assignEDCH(), NBAP RadioLinkSetup E-DCH |
| п.14 | UMTS Soft Handover: Active Set Management, RL Addition/Deletion через NBAP, macro diversity |
| п.15 | Carrier Aggregation (LTE-A Rel-10): до 5 CC, PCell/SCell планировщики, cross-carrier scheduling |
| п.16 | ROHC (RFC 5225): профиль 0x0001 RTP/UDP/IP, rohcCompress/rohcDecompress в PDCP |
| п.17 | 5G NR Stub: модуль rbs_nr, NRPhy SSB/PSS/SSS/PBCH, F1AP gNB-DU Setup (TS 38.473) |
| п.18 | Web Dashboard: rbs_api (cpp-httplib), GET /status /pm /alarms, POST /admit |
| п.19 | EN-DC NSA: Option 3a (SCG bearer), Option 3 (split-MN PDCP), Option 3x (split-SN PDCP); X2AP SgNB Add/Mod/Release; NRStack::acceptSCGBearer |
| п.20 | PM Export: OMS::exportCsv (CSV с ISO-8601 timestamp), OMS::pushInflux (InfluxDB Line Protocol UDP), ротация rbs.log по размеру |
| п.21 | PDCP Security: EIA2 (AES-128-CMAC, RFC 4493), applyIntegrity/verifyIntegrity, COUNT = HFN<<12\|SN, HFN wrap-around detection (TS 33.401 §6.4.2b) |
| п.22 | Link Management: `LinkController` (трасса PDU + блокировка), `LinkRegistry` singleton; `AbisOml`/`IubNbap`/`S1APLink` наследуют `LinkController`; REST API `/api/v1/links/*` (статус, трасса, connect/disconnect, block/unblock, inject); `bsc_addr`/`rnc_addr` в `rbs.conf` |

---

## Стандарты и спецификации

| Документ            | Назначение                                           |
|---------------------|------------------------------------------------------|
| 3GPP TS 45.002      | GSM: структура фреймов и burst-пакетов TDMA          |
| 3GPP TS 45.004      | GSM: модуляция GMSK                                  |
| 3GPP TS 25.211      | UMTS: физические каналы и отображение                |
| 3GPP TS 25.213      | UMTS: spreading и модуляция (Gold-коды, OVSF)        |
| 3GPP TS 36.211      | LTE: физические каналы (PSS, SSS, PBCH, PDSCH)       |
| 3GPP TS 36.212      | LTE: мультиплексирование и кодирование каналов       |
| 3GPP TS 36.213      | LTE: физические процедуры (CQI, MCS, планировщик)   |
| 3GPP TS 36.321      | LTE: MAC (HARQ, BSR, планировщик)                    |
| 3GPP TS 36.323      | LTE: PDCP (шифрование, сжатие заголовков)            |
| 3GPP TS 33.401      | LTE: алгоритмы безопасности (AES, SNOW3G, ZUC)       |
| 3GPP TS 32.111      | OMS: управление авариями (Fault Management)          |
| 3GPP TS 32.600      | OMS: Network Resource Model                          |
| 3GPP TS 28.623      | OMS: IOC (Information Object Classes)                |
| 3GPP TS 36.410      | S1: общие аспекты и принципы                         |
| 3GPP TS 36.412      | S1: транспортный уровень (SCTP/IP)                   |
| 3GPP TS 36.413      | S1AP: протокол уровня приложений eNB–MME              |
| 3GPP TS 36.420      | X2: общие аспекты                                    |
| 3GPP TS 36.423      | X2AP: протокол уровня приложений eNB–eNB             |
| 3GPP TS 25.308      | UMTS: High Speed Downlink Packet Access (HSDPA)      |
| 3GPP TS 25.433      | NBAP: протокол Node B – RNC                          |
| 3GPP TS 38.211      | NR: физические каналы (PSS/SSS/PBCH, SSB, SCS, SFN)      |
| 3GPP TS 38.213      | NR: физические процедуры (тайминг SSB, периодичность)       |
| 3GPP TS 38.300      | NR: общая архитектура NG-RAN, цепочка gNB-CU/DU |
| 3GPP TS 38.321      | NR: MAC-уровень (C-RNTI, планировщик)                    |
| 3GPP TS 38.331      | NR: RRC, MIB (структура PBCH-полезной нагрузки)           |
| 3GPP TS 38.401      | NR: архитектура NG-RAN (gNB-DU, gNB-CU, разделение)       |
| 3GPP TS 38.473      | F1AP: протокол gNB-DU – gNB-CU (F1 Setup, F1AP SCTP 38472) |
| RFC 7540            | HTTP/2 (справочно; REST API работает по HTTP/1.1)        |

---

## Дорожная карта (план следующих итераций)

### п.23 — NG-AP (gNB ↔ AMF, 5G Core)
**Цель:** полный интерфейс между gNB и 5G Core (аналог S1AP для NR SA).
- `src/nr/ngap_codec.cpp/.h` — кодек NG-AP (TS 38.413), APER через asn1c
- `src/nr/ngap_link.cpp/.h` — SCTP-транспорт, NGSetupRequest/Response, PDU Session Establishment
- `NRStack`: вызов `ngap_link.ngSetup()` при старте
- Тест: `test_ngap_codec` (NGSetup, PDU Session, UE Context Release)
- Ссылки: TS 38.413, TS 38.401 §8.7

### п.24 — NR MAC Scheduler + SDAP
**Цель:** завершить DU data path для 5G NR (аналог LTE PDCP/MAC).
- `NRMac` — планировщик NR: numerology-aware, BWP switching, DCI format 1_1
- `SDAP` (TS 37.324) — QoS Flow → DRB mapping, QFI в заголовке SDAP
- `NRPDCP` — 18-бит SN (long), интеграция с EIA2/EEA2
- Тест: `test_nr_mac` (admit UE, DL scheduler, QoS flow → DRB)
- Ссылки: TS 37.324, TS 38.321, TS 38.323

### п.25 — VoLTE / IMS stub
**Цель:** моделирование голосовых вызовов поверх LTE (IMS + SIP).
- SIP REGISTER / INVITE / BYE — минимальный stub (строковый кодек)
- RTP/RTCP header (RFC 3550) в GTP-U bearer
- `LTEStack::setupVoLTEBearer(rnti)` — выделение QCI=1 bearer (GBR Voice)
- Тест: `test_volte` (SIP INVITE → GTP-U RTP burst)
- Ссылки: 3GPP TS 26.114, RFC 3261, RFC 3550

### п.26 — Xn-AP (NR inter-gNB)
**Цель:** inter-gNB хэндовер для 5G NR (аналог X2AP).
- `src/nr/xnap_codec.cpp/.h` — Xn Setup Request/Response + XnAP HO (TS 38.423 §8.3)
- `NRStack::handoverRequired()` → `XnAPLink::handoverRequest()`
- Тест: `test_xnap` (Xn Setup, HO Required → Request → Notify)
- Ссылки: TS 38.423

### п.27 — Multi-Cell Topology & PM Export (Prometheus)
**Цель:** запуск нескольких ячеек одновременно с агрегацией PM.
- `RadioBaseStation`: массив `LTECellConfig[]`, несколько `LTEStack` экземпляров
- `OMS::exportPrometheus(port)` — `/metrics` endpoint в формате OpenMetrics
- Модель интерференции между ячейками (упрощённая SINR по расстоянию)
- Тест: `test_multi_cell` (3 ячейки, admit UE в каждой, проверить OMS счётчики)
- Ссылки: TS 36.300 §10.1, OpenMetrics RFC

### п.28 — RAN Slicing (5G, TS 28.541)
**Цель:** изоляция ресурсов для сетевых слайсов (eMBB / URLLC / mMTC).
- `SliceConfig`: S-NSSAI (SST + SD), MAX_PRB per slice
- `LTEMAC` / `NRMac`: slice-aware PF планировщик (PRB quota по NSSAI)
- `OMS`: счётчики per-slice (`slice.eMBB.connectedUEs`, `slice.URLLC.prb_used`)
- REST API: `GET /api/v1/slices` — статус слайсов
- Тест: `test_slicing` (3 слайса, quota enforcement, OMS counters)
- Ссылки: TS 28.541, TS 38.300 §5.7.1

### п.29 — CI/CD Pipeline (GitHub Actions)
**Цель:** автоматическая сборка и тестирование на каждый push.
- `.github/workflows/build.yml` — Ubuntu 22.04 + GCC 12, cmake + ctest
- Matrix: `{Debug, Release}` × `{ubuntu-22.04, ubuntu-24.04}`
- Badge в README (`![CI](https://github.com/.../actions/...badge.svg)`)
- Опционально: ASAN run в Debug matrix
- Ссылки: GitHub Actions docs, CMake/CTest CI best practices


