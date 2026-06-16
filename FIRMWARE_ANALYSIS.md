# Firmware Analysis — U13 (GD32F305) Main Board

## System Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│ Display Board (SNK_DISPLAY_CP_V11)                                   │
│  ┌──────────────────────────────────────────────────────────────┐    │
│  │  ESP32-WROOM-32UE                                            │    │
│  │  • Buttons matrix (S1-S14) → key scan                        │    │
│  │  • 4-digit 7-segment LED (GD5643CPG-1)                       │    │
│  │  • Buzzer                                                     │    │
│  │  • Rain sensor via J4                                         │    │
│  │  • WiFi/BT (stack present, unused in retail)                  │    │
│  │  • MQTT client (to server.sk-robot.com)                      │    │
│  └──────────┬───────────────────────────────────────────────────┘    │
└─────────────┼─────────────────────────────────────────────────────────┘
              │ J8 ribbon cable
              │ UART @115200, binary protocol (0xAA 0x55 + XOR CS)
              │
┌─────────────▼─────────────────────────────────────────────────────────┐
│ Main Board                                                            │
│                                                                       │
│  ┌──────────────────────────────────────────────────────────────┐    │
│  │  U16 (GD32F303CGT6) — Board MCU                               │    │
│  │  FreeRTOS: comm_task, init_bd                                 │    │
│  │  • Sensor processing (lift Hall, border coils, battery voltage)│    │
│  │  • Motor control (border following logic via factory vtable)  │    │
│  │  • Translates ESP32 binary protocol → JSON for U13           │    │
│  │  • IEC 60730 safety self-test (CPU, RAM, FLASH CRC, clock)   │    │
│  └──────────┬───────────────────────────────────────────────────┘    │
│             │ UART (internal PCB) @230400, JSON via cJSON             │
│             │                                                         │
│  ┌──────────▼───────────────────────────────────────────────────┐    │
│  │  U13 (GD32F305) — Main MCU                                    │    │
│  │  • Main loop FUN_0800e800 (bitmask dispatcher)                 │    │
│  │  • USB Host (pendrive FAT32) — IAP, FORMATFLASH.json          │    │
│  │  • OTA over UART from U16 (230400→921600 baud)                │    │
│  │  • Env system (key-value store on SPI flash)                  │    │
│  │  • PIN verification (reads stored PIN from EEPROM U22)        │    │
│  │                                                               │    │
│  │  USART0 @230400  ←→ U16                                       │    │
│  │  USART1 @115200  ←→ ??? (3rd onboard MCU?)                   │    │
│  │  USART2 @115200  ←→ ???                                       │    │
│  └───────────────────────────────────────────────────────────────┘    │
│                                                                       │
│  EEPROM U22 (I2C/SPI) — stores user PIN                              │
│  SPI Flash — env_read.json (firmware versions, config)               │
│  USB pendrive (FAT32)                                                 │
│    SNK_MB_*.bin    — Main Board firmware update                       │
│    SNK_BB_*.bin    — Blade Board firmware update                     │
│    SNK_DB_*.bin    — Drive Board firmware update                     │
│    SNK_LB_*.bin    — Lift Board firmware update                      │
│    SNK_DBL_*.bin   — Dual/Drive BTL update                           │
│    SNK_DBS_*.bin   — Drive Board (secondary?) update                 │
│    SNK_DBH_*.bin   — Drive Board (high?) update                      │
│    SNK_MBTL_*.bin  — Main Board Bootloader update                    │
│    log_yyyymmdd_hhmmss.html — saved logs                             │
│    FORMATFLASH.json — signal to format internal flash                │
└───────────────────────────────────────────────────────────────────────┘
```

## 1. MCU Communication — USART mapping

### U13 USARTs (initialized in FUN_08003fa8)

| USART | Baza | Baudrate | GPIO AF | DMA | Podłączony do |
|-------|------|----------|---------|-----|---------------|
| USART0 | `0x40013800` | `0x38400` = 230400 | `0x25` | kanał 4 | **U16** — JSON, OTA, komendy |
| USART1 | `0x40004400` | `0x1c200` = 115200 | `0x26` | kanał 5 | Nieznany (3rd MCU?) |
| USART2 | `0x40004800` | `0x1c200` = 115200 | `0x34` | kanał 2 | Nieznany |

### Protokół U13 ↔ U16 (USART0 @ 230400)

Struktura pakietu OTA (zweryfikowana przez `FUN_08008cb8`):
```
[2 bajty: length LE] [N bajtów: dane] [1 bajt: XOR checksum]
```
Checksum = XOR wszystkich bajtów danych.

Używany do:
- OTA firmware download (z możliwością zmiany baud na 460800/921600)
- Przekazywanie statusu (`FUN_08007c94` wysyła status do U16)
- Dane sensorowe w formacie JSON (przez bibliotekę cJSON na U16)

### Łańcuch komunikacji

```
Display Board (ESP32)
    │ UART @115200, binary protocol (0xAA 0x55, XOR CS)
    │ cmd 0x01-0x04 = button presses
    │ cmd 0x0B = PWD_VERIFY (4-digit PIN)
    │ cmd 0x0C = PWD_RESULT (response from mainboard)
    │ cmd 0x0D-0x15 = status, mow, charge, bat info etc.
    ▼
U16 (GD32F303) — board MCU
    │ Translates binary commands → JSON messages
    │ UART @230400, JSON via cJSON
    ▼
U13 (GD32F305) — main MCU
    │ Parses JSON, executes commands (motor, sensors, PIN verify)
    │ Sends JSON responses back
```

## 2. PIN Code — gdzie jest trzymany?

PIN jest **4-cyfrowy** i przechowywany w **EEPROM U22** na mainboardzie. Nie ma go w ESP32 ani w U13 firmware.

### Ścieżka PIN (rekonstrukcja z U16.md + ESP32.md)

```
1. Wyświetlacz pokazuje "0   " (miga)
   → Użytkownik ustawia cyfrę przyciskami +/-
   → "0 0  " → "0 0 0 " → "0 0 0 0"
   
2. ESP32 wysyła ramkę do U16:
   0xAA 0x55 0x0B "1234" <XOR_CS>
   (cmd 0x0B = PWD_VERIFY, payload = 4 znaki ASCII)

3. U16 przekazuje do U13 jako JSON:
   {"cmd":"verify_pin","pin":"1234"}

4. U13 odczytuje zapisany PIN z EEPROM U22
   → Jeśli zgadza się: zwraca OK
   → Jeśli nie: zwraca FAIL

5. U16 → ESP32: 0xAA 0x55 0x0C 0x00 <CS> (OK)
                      lub 0xAA 0x55 0x0C 0x01 <CS> (FAIL)

6. ESP32 wyświetla:
   "IdLE" (OK — odblokowany)
   "LoCK" (FAIL — pozostaje zablokowany)
```

### Co jest czym w ESP32?

| Klucz NVS ESP32 | Wartość | Znaczenie |
|-----------------|---------|-----------|
| `robot_password` | `88888888` | Hasło WiFi/MQTT, **NIE PIN** |
| `robot_ssid` | `cy-public` | Nazwa WiFi sieci |
| `wifi_passwd` | `88888888` | To samo hasło WiFi |
| `robot_name` | `MyMower` | Nazwa urządzenia |
| `robot_sn` | `2312CGF250600035167` | Numer seryjny |
| `iot_mqtt_uri` | `mqtt://server.sk-robot.com` | Broker MQTT |

ESP32 weryfikuje hasło dla WiFi/MQTT (`Check password succeeded/failed`), a PIN 4-cyfrowy jest wysyłany do mainboarda do weryfikacji na U22.

## 3. Jak ESP32 komunikuje się z resztą?

ESP32 **nie tylko przesyła wciskanie klawiszy** — ma pełny zestaw komend:

| ID | Kierunek | Nazwa | Opis |
|----|----------|-------|------|
| `0x01` | ESP→MB | `BTN_UP` | Przycisk + |
| `0x02` | ESP→MB | `BTN_DOWN` | Przycisk - |
| `0x04` | ESP→MB | `BTN_OK` | Przycisk OK |
| `0x0B` | ESP→MB | `PWD_VERIFY` | Wyślij PIN (4 bajty ASCII) |
| `0x0C` | MB→ESP | `PWD_RESULT` | Wynik weryfikacji (0=OK, 1=fail) |
| `0x0D` | ESP→MB | `STATUS_REQ` | Zapytaj o status |
| `0x0E` | MB→ESP | `STATUS_RSP` | Status (mow/charge/idle/lock/error) |
| `0x0F` | ESP→MB | `MOW_START` | Rozpocznij koszenie |
| `0x10` | ESP→MB | `CHARGE_RET` | Wracaj do stacji |
| `0x11` | ESP→MB | `DISPLAY_OFF` | Wyłącz wyświetlacz |
| `0x12` | MB→ESP | `ERROR_INFO` | Kod błędu |
| `0x14` | ESP→MB | `BAT_INFO_REQ` | Zapytaj o baterię |
| `0x15` | MB→ESP | `BAT_INFO_RSP` | Napięcie baterii (mV) |

Dodatkowo — jeśli WiFi jest skonfigurowane — ESP32 łączy się z MQTT brokerem
`server.sk-robot.com` i przekazuje komendy z chmury do mainboarda.

Kluczowe stringi z HTTP serverem (`Mower Log` strona HTML z JS) są częścią
ESP32 firmware, nie U13.

## 4. Obsługa Pendrive — FORMATFLASH.json

### Przepływ:

```
1. Użytkownik wkłada USB pendrive
2. FUN_08002f18 wykrywa USB ("USB disk Ready")
3. FUN_08003cdc tworzy plik log_yyyymmdd_hhmmss.html ("save log to usb disk")
4. FUN_0800303c sprawdza czy istnieją pliki:
   a) FORMATFLASH.json  → FORMAT FLASH
   b) SNK_MB_*.bin      → IAP aktualizacja firmware MB
   c) SNK_BB_*.bin      → IAP aktualizacja BB
   d) SNK_DB_*.bin      → IAP aktualizacja DB
   e) SNK_LB_*.bin      → IAP aktualizacja LB
   f) SNK_DBL_*.bin     → IAP aktualizacja DBL
   g) SNK_MBTL_*.bin    → IAP aktualizacja bootloadera
```

### Co uruchamia FORMATFLASH.json?

```
FUN_0800303c (USB IAP handler)
  │
  ├── Jeśli flag & 0x80 (MB IAP)
  │     → "MB IAP start" → flash write
  │
  ├── Jeśli flag & 0x100 (BB IAP)
  │     → "BB IAP start" → flash write
  │
  ├── Jeśli flag & 0x200 (DB IAP)
  │     → ...
  │
  └── FORMATFLASH.json → "ready to format flash" / "format flash\n"
        → wymazuje całą wewnętrzną flash przez FUN_080046fc
        → czyści env (wszystkie ustawienia)
```

Dokładny mechanizm: `FUN_0800303c` jest wywoływana z `param_1` jako maską bitową. Jeśli żaden z bitów IAP nie jest ustawiony, sprawdza czy na USB istnieje plik `FORMATFLASH.json`. Jeśli tak — formatuje flash i usuwa wszystkie dane konfiguracyjne.

## 5. Env System (Key-Value Store)

U13 używa własnego systemu przechowywania par klucz-wartość na SPI flash.

| Operacja | Funkcja | Opis |
|----------|---------|------|
| Odczyt | `FUN_0800aa6c(key, buf, size)` | Czyta parametr z env |
| Zapis | `FUN_0800ae28(key, buf, size)` | Zapisuje parametr do env |
| Usuń | `FUN_0800a84c(key, 0, 1)` | Usuwa parametr |

Format: `env_read.json` na wewnętrznym SPI flash (prawdopodobnie W25Q64 lub podobny).

**Przechowywane parametry:**
- `MB_VER`, `mb_sv`, `MB_BVER`, `MB_SIZE`, `MB_BRF` — Main Board firmware
- `BB_VER`, `bb_sv`, `BB_BVER`, `BB_SIZE`, `BB_BRF` — Blade Board firmware
- `DB_VER`, `db_sv`, `DB_BVER`, `DB_SIZE`, `DB_BRF` — Drive Board firmware
- `LB_VER`, `lb_sv`, `LB_BVER`, `LB_SIZE`, `LB_BRF` — Lift Board firmware
- `BTL_VER`, `BTL_BVER`, `BTL_SIZE`, `BTL_BRF` — Bootloader firmware
- `pdt_ver` — Product version
- `ota_date` — Data ostatniej aktualizacji OTA
- `cfgupdate`, `cfgstr` — Config update signal
- `cfg_rst` — Reset config flag (0xa5a5 / 0xaa55)
- `ult_cfg` — Ultra sonic config
- `led_cfg` — LED config

**Brak parametrów PIN/hasła** w env U13 — PIN jest przechowywany w zewnętrznym EEPROM U22.

## 6. Główna pętla (Main Loop)

`FUN_0800e800` — bitmask-driven dispatcher:

```
Init:
  FUN_08004238()      — init peryferiów (GPIO, timery)
  FUN_08008948()      — init USB OTG
  FUN_0800cf04()      — init GPIO dla klawiszy
  FUN_080024c4()      — init ADC (bateria, sensory)
  FUN_08007b78()      — wait for U16 ready
  FUN_08007c68(0)     — init komunikacji z U16
  FUN_080043ec()      — wybór trybu USB (host/device)

Loop:
  ┌──────────────────────────────────────────────────────────┐
  │ bit 0  → FUN_08012930()    — USB protocol handler        │
  │ bit 1  → FUN_08003e0c()    — env_read.json load          │
  │ bit 2  → FUN_08002fc0()    — USB flash IAP               │
  │ bit 13 → FUN_0800303c()    — IAP/format flash            │
  │ bit 5  → FUN_08003cdc()    — save log to USB             │
  │ bit 6  → FUN_0800f54c()    — log/assert flush            │
  │ bit 14 → FUN_08001370()    — OTA over UART (z U16)       │
  │ zawsze → FUN_0800bbf4()    — watchdog?                   │
  └──────────────────────────────────────────────────────────┘
```

## 7. OTA Update (dwie ścieżki)

### OTA przez USB (pendrive):
- Użytkownik kopiuje `SNK_MB_*.bin` itd. na FAT32 pendrive
- U13 wykrywa pendrive, znajduje pliki, programuje własną flash
- Obsługuje: MB, BB, DB, LB, BTL, DBL, DBS, DBH

### OTA przez UART (z U16):
- U16 przesyła firmware do U13 przez USART0
- 7 komend: GET OTA INFO, DOWNLOAD, SET OTA MODE, SET VER, RETURN, SET FIRMWARE NUMBER, SET BAUDRATE
- Baudrate zmienny: 230400 → 460800 → 921600
- XOR checksum na każdym pakiecie
- ESP32 nie ma bezpośredniego udziału w OTA U13

## 8. Podsumowanie — pliki analizy

| Plik | Zawartość |
|------|-----------|
| `U16.md` | Analiza U16 (GD32F303): FreeRTOS, sensory, silniki, JSON, IEC 60730 |
| `ESP32.md` | Analiza ESP32: wyświetlacz, klawisze, protokół binarny, WiFi/MQTT, PIN flow |
| `FIRMWARE_ANALYSIS.md` | (ten plik) Analiza U13 (GD32F305): główna pętla, USB, OTA, env |
| `decompilation.md` | Instrukcja konfiguracji Ghidra + ghidra-cli |
