# RBS — Radio Base Station

Симулятор многостандартной базовой станции (Multi-RAT RBS), реализующий протокольные стеки **GSM (2G)**, **UMTS (3G)** и **LTE (4G)** в одном исполняемом файле на языке **C++17**.

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
   - [OMS — Operations & Maintenance](#oms--operations--maintenance)
   - [Главный контроллер (main)](#главный-контроллер-main)
4. [Диаграмма потоков данных](#диаграмма-потоков-данных)
5. [Конфигурационный файл rbs.conf](#конфигурационный-файл-rbsconf)
6. [Сборка](#сборка)
7. [Запуск](#запуск)
8. [Тесты](#тесты)
9. [Логирование](#логирование)
10. [Стандарты и спецификации](#стандарты-и-спецификации)

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

---

## Структура проекта

```
RBS/
├── CMakeLists.txt          # Система сборки CMake
├── rbs.conf                # Конфигурационный файл узла
├── rbs.log                 # Лог-файл (создаётся при запуске)
├── src/
│   ├── main.cpp            # Точка входа, класс RadioBaseStation
│   ├── common/
│   │   ├── types.h         # Общие типы, константы, структуры данных
│   │   ├── logger.h        # Потокобезопасный синглтон-логгер
│   │   └── config.cpp/.h   # Парсер INI-конфигурации
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
│   └── oms/
│       ├── oms.cpp/.h      # Fault management, счётчики производительности
└── tests/
    ├── CMakeLists.txt
    ├── test_config.cpp     # Тест парсера конфигурации
    ├── test_gsm_phy.cpp    # Тест GSM PHY
    ├── test_lte_mac.cpp    # Тест LTE MAC (CQI→MCS, планировщик)
    └── test_pdcp.cpp       # Тест PDCP (DL/UL loopback)
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
- API: `admitUE(imsi)`, `releaseUE(rnti)`, `printStats()`
- Счётчики OMS: `umts.connectedUEs`

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
- **Шифрование**: AES-128 CTR (заглушка XOR в симуляции; ключ задаётся через `PDCPConfig`)
- **Алгоритмы**: `NULL_ALG`, `AES`, `SNOW3G`, `ZUC` (по TS 33.401)
- **ROHC**: точка подключения IP-компрессии заголовков (stub)

#### Контроллер — `LTEStack`
- `admitUE(imsi, cqi)` → RNTI: создаёт PDCP-bearer, добавляет UE в MAC-планировщик
- `sendIPPacket(rnti, data)` → PDCP→MAC→PHY (DL путь)
- `receiveIPPacket(rnti)` → PHY→MAC→PDCP (UL путь)
- Счётчики OMS: `lte.connectedUEs`

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
 └── State Management
     └── setNodeState(UNLOCKED | LOCKED | SHUTTING_DOWN)
```

**Уровни аварий** (AlarmSeverity):
- `WARNING` — незначительная деградация (например, высокий CoS)
- `MINOR` / `MAJOR` — частичная потеря функциональности
- `CRITICAL` — полный отказ подсистемы (RF hardware FAULT)

**Счётчики производительности** (обновляются из стеков):
- `gsm.connectedUEs`, `umts.connectedUEs`, `lte.connectedUEs`
- Расширяемы: достаточно вызвать `OMS::instance().updateCounter("имя", значение, "ед")`

---

### Главный контроллер (main)

#### Класс `RadioBaseStation`

```
RadioBaseStation(configPath)
 │
 ├── Config::instance().loadFile()        ← читает rbs.conf
 ├── RFHardware(2Tx,2Rx) × 3             ← три RF-фронтенда
 ├── setAlarmCallback() → OMS            ← аппаратные аварии
 ├── GSMStack / UMTSStack / LTEStack
 │
 ├── start()
 │   ├── OMS → UNLOCKED
 │   ├── rf.initialise() + selfTest() × 3
 │   └── gsmStack/umtsStack/lteStack → start()
 │
 ├── runDemo()
 │   ├── admitUE GSM  (IMSI=100000000000001)
 │   ├── admitUE UMTS (IMSI=200000000000002)
 │   ├── admitUE LTE  (IMSI=300000000000003, CQI=12)
 │   ├── sleep(2s)
 │   ├── printStats() × 3
 │   ├── OMS.printPerformanceReport()
 │   └── releaseUE × 3
 │
 └── mainLoop()
     └── каждые 30 с: OMS.printPerformanceReport()
         до SIGINT/SIGTERM → graceful shutdown
```

#### Точка входа `main()`
```cpp
int main(int argc, char* argv[]) {
    // 1. Установка обработчиков сигналов (SIGINT, SIGTERM)
    // 2. Инициализация логгера (уровень + имя файла из конфига)
    // 3. Создание и запуск RadioBaseStation
    // 4. runDemo() — демонстрация работы всех трёх RAT
    // 5. mainLoop() — периодические PM-отчёты до Ctrl+C
    // 6. stop() — остановка всех стеков и потоков
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

[umts]
cell_id       = 2
uarfcn        = 10700    # UMTS Band I DL: 10562–10838 → 2110–2170 МГц
tx_power_dbm  = 43.0
psc           = 64       # Primary Scrambling Code (0–511)
lac           = 1000
rac           = 1        # Routing Area Code

[lte]
cell_id       = 3
earfcn        = 1800     # Band 3 → DL ~1865 МГц
tx_power_dbm  = 43.0
pci           = 300      # Physical Cell Identity (0–503)
tac           = 1        # Tracking Area Code
num_antennas  = 2        # Порты Tx: 1, 2 или 4

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

./build/rbs_node rbs.conf
```

### Опции CMake

| Опция                | По умолчанию | Описание                              |
|----------------------|--------------|---------------------------------------|
| `RBS_ENABLE_TESTS`   | `ON`         | Сборка модульных тестов               |
| `RBS_ENABLE_ASAN`    | `OFF`        | AddressSanitizer (только GCC/Clang)   |

---

## Запуск

```powershell
# Windows
.\build\Release\rbs_node.exe rbs.conf

# Остановка — Ctrl+C (SIGINT)
# Узел переходит в состояние SHUTTING_DOWN и корректно
# останавливает все потоки перед выходом.
```

**Первые строки вывода:**
```
2026-04-07 16:40:05 [INFO ] [RBS] Radio Base Station v1.0.0 starting...
2026-04-07 16:40:05 [INFO ] [Config] Loaded configuration from rbs.conf
2026-04-07 16:40:05 [INFO ] [OMS] Node state → UNLOCKED
2026-04-07 16:40:05 [INFO ] [RFHardware] Self-test PASSED        (× 3)
2026-04-07 16:40:05 [INFO ] [GSMStack] GSM cell 1 started
2026-04-07 16:40:05 [INFO ] [UMTSStack] UMTS cell 2 started
2026-04-07 16:40:05 [INFO ] [LTEStack] LTE cell 3 started
```

---

## Тесты

```powershell
cd build
ctest -C Release --output-on-failure
```

| Тест              | Что проверяет                                           |
|-------------------|---------------------------------------------------------|
| `test_config`     | Чтение INI-файла, типи́зированные геттеры, buildXxxConfig() |
| `test_gsm_phy`    | Счётчики фреймов/слотов PHY, формирование burst-пакетов |
| `test_lte_mac`    | Вход UE, таблица CQI→MCS, обновление метрики PF        |
| `test_pdcp`       | DL/UL loopback, добавление/удаление bearer, SN-счётчик  |

Все 4 теста проходят за ~0.53 с.

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

---

*Проект выполнен в учебных и исследовательских целях. Не предназначен для использования в реальной радиосети.*
