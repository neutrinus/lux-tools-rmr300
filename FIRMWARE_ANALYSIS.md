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

## 4. Obsługa Pendrive — USB Mass Storage

### Co faktycznie działa z pendrive:

```
1. Użytkownik wkłada USB pendrive
2. FUN_08002f18 wykrywa USB ("USB disk Ready")
3. FUN_08003cdc tworzy plik log_yyyymmdd_hhmmss.html ("save log to usb disk")
4. FUN_0800303c (IAP handler) szuka plików SNK_*.bin:
   → "MB IAP start ..."  gdy SNK_MB_*.bin  — firmware Main Board
   → "BB IAP start ..."  gdy SNK_BB_*.bin  — firmware Blade Board
   → "DB IAP start ..."  gdy SNK_DB_*.bin  — firmware Drive Board
   → "LB IAP start ..."  gdy SNK_LB_*.bin  — firmware Lift Board
   → "BTL IAP start ..." gdy SNK_MBTL_*.bin — firmware Bootloader
   → "DBL IAP start ..." gdy SNK_DBL_*.bin — firmware DuaL?
```

### FORMATFLASH.json — NIEDOSTĘPNY

**String "FORMATFLASH.json" istnieje w firmware pod `0x08003990`, ale nie ma do niego ŻADNEJ referencji z kodu.** Potwierdzone przez:

| Metoda wyszukania | Wynik |
|--------------------|-------|
| LDR literal (PC-rel) | Nie znaleziono (0 trafień w 512KB) |
| MOVW/MOVT (Thumb2) | Nie znaleziono (0 trafień w 512KB) |
| LE32 0x08003990 | Nie znaleziono (0 trafień w całym binary) |
| LE16 0x3990 | Nie znaleziono (0 trafień w code area) |
| Xref Ghidra | 0 xrefów |

**Wniosek: FORMATFLASH.json to dead code.** Mechanizm formatowania przez pendrive nie istnieje w tym firmware. To są pozostałości po wcześniejszej wersji lub firmware dla innego modelu.

To samo dotyczy stringów "ready to format flash" (0x080039a4) i "format flash\n" (0x080039bc) — też zero referencji.

### Jak faktycznie wygląda format?

Format flash jest wywoływany **przez komendę z aplikacji/display board**:
1. Użytkownik wysyła komendę (przez przyciski lub aplikację przez WiFi)
2. ESP32 → U16 (binary protocol)
3. U16 → U13 (JSON command)
4. U13 kasuje SPI flash (env) + EEPROM U22 (PIN)

## 5. EEPROM U22 — System PIN / User Settings

### Sprzęt

| Parametr | Wartość |
|----------|---------|
| Układ | U22, SOIC-8 (oznaczony w HARDWARE.md jako 24C02/04) |
| Interfejs | I2C1 (`0x40005800`) |
| Pojemność | 256-512 bajtów (24C02 = 2Kbit = 256B, 24C04 = 4Kbit = 512B) |
| Zasilanie | 3.3V (z mainboarda) |
| Adres I2C | Standard 0x50 (7-bit) / 0xA0 (8-bit) dla 24Cxx |

I2C1 znaleziony w kodzie U13 pod adresami: `0x08023550`, `0x080236C4`, `0x080237C0`.

### Przechowywane dane

Z stringów w U13 firmware (moduł `service_user_set.c`):

| Kategoria | Klucze / Stringi | Opis |
|-----------|------------------|------|
| **PIN** | `pwd`, `pwd_en`, `pwd_rst`, `usr_pwd_en` | Hasło, enable flag, reset flag |
| **Schedule** | `schedule`, `simple schedule` | Harmonogram koszenia |
| **Kalibracja** | `battery calibrate`, `rtc calibration` | Kalibracja baterii i RTC |
| **Użytkownik** | `user_name`, `language`, `cut area` | Nazwa, język, obszar koszenia |
| **Liczniki** | `work length`, `power on minutes` | Przepracowane godziny |

### Funkcje PIN (z stringów)

| Funkcja | String w kodzie |
|---------|-----------------|
| Sprawdź PIN | `compare pwd correct` / `compare pwd uncorrect=%d` |
| Ustaw PIN | `set pwd success` / `set pwd failed` |
| Zmień PIN | `set pwd old failed, because input old password error` |
| Reset PIN | `reset pwd success` / `reset pwd failed` |
| Blokada po błędach | `compare pwd uncorrect=%d overtimes, reset and lock` |
| Enable/disable | `set user enable password failed=%d` |
| Wczytaj PIN | `load user password failed` |

### Rate limiting

System posiada zabezpieczenie przed brute-forcem:
```
"compare pwd uncorrect=%d overtimes, reset and lock"
```
Po przekroczeniu limitu błędnych prób PIN, system **blokuje się** ("reset and lock").
Ilość prób przed blokadą (parametr `%d`) nie została zidentyfikowana, ale typowo w takich
systemach jest to 3-5 prób.

### Tryb resetu PIN

PIN można zresetować przez:
1. **Stary PIN** → `set pwd old failed, because input old password error` (wymaga znajomości starego PINu)
2. **pwd_rst** → zdalny reset przez komendę (prawdopodobnie z aplikacji)

## 6. Env System (Key-Value Store)

U13 używa własnego systemu przechowywania par klucz-wartość na SPI flash (U12, nie mylić z EEPROM U22).

| Operacja | Funkcja | Opis |
|----------|---------|------|
| Odczyt | `FUN_0800aa6c(key, buf, size)` | Czyta parametr z env |
| Zapis | `FUN_0800ae28(key, buf, size)` | Zapisuje parametr do env |
| Usuń | `FUN_0800a84c(key, 0, 1)` | Usuwa parametr |

**Przechowywane parametry (wersje firmware, konfiguracja):**
- `MB_VER`, `mb_sv`, `MB_BVER`, `MB_SIZE`, `MB_BRF` — Main Board
- `BB_VER`, `bb_sv`, `BB_BVER`, `BB_SIZE`, `BB_BRF` — Blade Board
- `DB_VER`, `db_sv`, `DB_BVER`, `DB_SIZE`, `DB_BRF` — Drive Board
- `LB_VER`, `lb_sv`, `LB_BVER`, `LB_SIZE`, `LB_BRF` — Lift Board
- `BTL_VER`, `BTL_BVER`, `BTL_SIZE`, `BTL_BRF` — Bootloader
- `pdt_ver`, `ota_date` — Wersja produktu, data OTA
- `cfgupdate`, `cfgstr` — Config update
- `cfg_rst` — Reset config (0xa5a5 dwuetapowy reset)
- `ult_cfg`, `led_cfg` — Konfiguracja czujników i LED

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

## 8. Security Assessment — PIN Recovery

### Czy da się odzyskać PIN?

| Metoda | Możliwe? | Uwagi |
|--------|----------|-------|
| **Z firmware U13** | ❌ NIE | PIN w EEPROM U22, nie w flash U13 |
| **Z ESP32** | ❌ NIE | ESP32 przechowuje tylko WiFi hasło |
| **Przez USB pendrive (FORMATFLASH.json)** | ❌ NIE | Dead code — format nie istnieje |
| **Brute-force przez display** | ❌ NIE | Rate limiting — blokada po X próbach |
| **Odczyt EEPROM U22 (CH341A + SOIC clip)** | ✅ TAK | Wymaga fizycznego dostępu do mainboarda |
| **Reset przez aplikację (pwd_rst)** | ❓ NIEZNANE | Jeśli aplikacja ma komendę resetu PIN |
| **Nowy firmware (IAP z SNK_*.bin)** | ❌ NIE | IAP nie kasuje EEPROM U22 |
| **Format flash (komenda przez app)** | ✅ TAK | Jeśli app ma taką opcję — kasuje wszystko |

### Rekomendowany sposób na reset PIN (jeśli zapomniałeś):

1. **Fizyczny odczyt EEPROM** — CH341A + SOIC-8 clip na U22, odczyt przez `flashrom` lub `i2cget`
2. **Zgranie zawartości** — 256-512 bajtów, PIN przechowywany jako plaintext lub prosty encoding
3. **Modyfikacja + zapis** — zmiana PINu na znany, zapis z powrotem

### Uwagi bezpieczeństwa

- PIN jest przechowywany w zewnętrznym EEPROM — to **słabe** zabezpieczenie:
  - EEPROM (24C02/04) nie ma żadnych zabezpieczeń sprzętowych
  - Nie ma szyfrowania ani maskowania PINu (plaintext?)
  - Dostęp przez I2C, łatwy do odczytania z zewnątrz
- Rate limiting jest tylko programowe (w U13) — można go obejść odczytując EEPROM
- Format flash przez pendrive nie działa → jedyna droga do resetu to app lub fizyczny dostęp

## 9. Podsumowanie — pliki analizy

| Plik | Zawartość |
|------|-----------|
| `U16.md` | Analiza U16 (GD32F303): FreeRTOS, sensory, silniki, JSON, IEC 60730 |
| `ESP32.md` | Analiza ESP32: wyświetlacz, klawisze, protokół binarny, WiFi/MQTT, PIN flow |
| `FIRMWARE_ANALYSIS.md` | (ten plik) Analiza U13 (GD32F305): główna pętla, USB, OTA, env, PIN, security |
| `decompilation.md` | Instrukcja konfiguracji Ghidra + ghidra-cli |
