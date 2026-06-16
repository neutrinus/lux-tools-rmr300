# Firmware Analysis — U13 (GD32F305) Main Board

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        U13 (GD32F305) — Main Board                  │
│                                                                     │
│  ┌──────────────┐   USART0 @230400   ┌──────────────────────────┐   │
│  │              │ ◄─────────────────► │  U16 (GD32F303)          │   │
│  │  Main Loop   │   OTA, status, key  │  Blade/Drive/Lift Ctrl   │   │
│  │  FUN_0800e800│                     │                          │   │
│  │              │   USART1 @115200   ┌──────────────────────────┐ │   │
│  │  Bitmask     │ ◄─────────────────► │  ESP32 (WiFi/BT)        │ │   │
│  │  Dispatcher  │   commands, status  │  - App interface        │ │   │
│  │              │                     │  - HTTP server          │ │   │
│  │  USB Host    │   USART2 @115200   │  - MQTT(?)              │ │   │
│  │  (pendrive)  │ ◄─────────────────► │  - PIN: 88888888       │ │   │
│  │              │   3rd MCU (?)       └──────────────────────────┘ │   │
│  │  SPI Flash   │                     └──────────────────────────┘ │
│  │  (env/KV)    │                                                 │
│  └──────┬───────┘                                                 │
└─────────┼───────────────────────────────────────────────────────────┘
          │
          ▼
    USB pendrive (FAT32)
    ─────────────────
    SNK_MB_*.bin        — Main Board firmware update
    SNK_BB_*.bin        — Blade Board firmware update
    SNK_DB_*.bin        — Drive Board firmware update
    SNK_LB_*.bin        — Lift Board firmware update
    SNK_DBL_*.bin       — Dual/Drive BTL update
    SNK_DBS_*.bin       — Drive Board (secondary?) update
    SNK_DBH_*.bin       — Drive Board (high?) update
    SNK_MBTL_*.bin      — Main Board Bootloader update
    log_yyyymmdd_hhmmss.html  — saved logs
    FORMATFLASH.json    — signal to format internal flash
```

## 1. MCU Communication — 3 USARTs

### USART0 — U13 ↔ U16 (GD32F303) @ 230400 baud

| Właściwość | Wartość |
|------------|---------|
| Baza | `0x40013800` |
| Baudrate | `0x38400` = 230400 (zmienny dla OTA: 460800, 921600) |
| GPIO AF | `0x25` (AF3) |
| DMA | kanał 4 |
| Funkcja | OTA firmware update, status, komendy |

**Protokół:** Struktura pakietu (zweryfikowana przez `FUN_08008cb8`):
```
[2 bajty: length LE] [N bajtów: dane] [1 bajt: XOR checksum]
```
Checksum to XOR wszystkich bajtów danych. Używany do:
- OTA firmware download (do 921600 baud)
- Przekazywanie statusu operacji (np. `FUN_08007c94` wysyła status do U16)
- Komendy i konfiguracja

### USART1 — U13 ↔ ESP32 @ 115200 baud

| Właściwość | Wartość |
|------------|---------|
| Baza | `0x40004400` |
| Baudrate | `0x1c200` = 115200 |
| GPIO AF | `0x26` (AF3) |
| DMA | kanał 5 |
| Funkcja | WiFi/BT, komendy z aplikacji |

ESP32 przekazuje komendy i wciskanie klawiszy przez ten UART. Protokół nie został do końca odtworzony, ale ESP32 nasłuchuje na komendy HTTP/BLE i tłumaczy je na komendy UART.

### USART2 — U13 ↔ trzeci MCU @ 115200 baud

| Właściwość | Wartość |
|------------|---------|
| Baza | `0x40004800` |
| Baudrate | `0x1c200` = 115200 |
| GPIO AF | `0x34` |
| DMA | kanał 2 |
| Funkcja | Nieznany peryferia (może wyświetlacz/sensory) |

## 2. PIN Code — Gdzie jest trzymany?

**PIN NIE jest przechowywany w U13.** Jest zarządzany przez ESP32:

| Klucz ESP32 | Wartość |
|-------------|---------|
| `robot_password` | `88888888` (domyślny PIN) |
| `robot_ssid` | Nazwa WiFi |
| `robot_name` | Nazwa kosiarki |
| `robot_sn` | Numer seryjny |
| `snk_mower` | Typ/wersja kosiarki |
| `snk_mqtt` | Konfiguracja MQTT |

**Mechanizm:**
1. Aplikacja (przez WiFi/BLE) wysyła PIN do ESP32
2. ESP32 weryfikuje: `Check password succeeded` / `Check password failed`
3. ESP32 używa HTTP Basic Auth (`HTTP_AUTH`) do uwierzytelniania
4. Po poprawnym PINie, ESP32 przekazuje komendy do U13 przez USART1

U13 nie ma żadnej logiki PIN — tylko odbiera komendy z ESP32 i wykonuje.

## 3. Jak ESP32 komunikuje się z U13?

ESP32 **nie tylko przesyła wciskanie klawiszy**. Prawdopodobnie:
- Odbiera komendy z aplikacji (przez BLE lub WiFi/HTTP)
- Przekazuje skonwertowane komendy do U13 przez USART1 @ 115200
- U13 wykonuje fizyczne operacje (jazda, koszenie, powrót do stacji)
- Status jest odczytywany z U13 i przesyłany z powrotem do aplikacji

Szczegóły protokołu na USART1 nie zostały w pełni odtworzone z U13 firmware, ale na podstawie struktury OTA wiadomo że używa tego samego formatu pakietów z XOR checksum.

Kluczowe stringi wskazujące na HTTP server na ESP32:
- HTML/CSS/JS strony `Mower Log` z filtrowaniem po typie zdarzeń
- `HTTP_AUTH` — Basic Authentication z hasłem

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

**Brak parametrów PIN/hasła** w env U13 — PIN jest wyłączenie na ESP32.

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

## 8. ESP32 — Podsumowanie z dumpa

Z dumpa ESP32 (binarny) odczytano:

| Funkcja | Znalezione stringi |
|---------|-------------------|
| App name | `9MyMower` |
| PIN | `robot_password` = `88888888` |
| WiFi | `robot_ssid`, `wifi_passwd` |
| MQTT | `snk_mqtt` |
| Serial | `robot_sn` |
| Auth | `HTTP_AUTH` (Basic Auth) |
| BLE | klucze IRK, IR, DHK, ER |
| HTTP | serwer z stroną Mower Log |
| TLS | krzywe brainpool (ECC) |
| SPI | `spi_tube_drive.c` (LED tube display?) |
