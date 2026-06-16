# EEPROM dumping & PIN extraction — pełna dokumentacja

## Cel
Odczytanie kodu PIN z płyty kosiarki Lux Tools A-RMR-300-24 (Landxcape) przez SWD na U13 (GD32F305).

## STATUS: ✅ PIN UZYSKANY = **9633**

PIN odczytany z pamięci RAM firmware'u — potwierdzony działaniem przez użytkownika.
EEPROM U22 ostatecznie NIE był potrzebny do zdobycia PINu (choć udało się nawiązać
łączność I2C i sczytać bajty 0x00–0x5F).

---

## TL;DR — jak znaleziono PIN

1. Zrzucono flash U13 (1 MB) → `firmware/u13_flash_1mb.bin`.
2. Zidentyfikowano system KV-store w firmware (EasyFlash).
3. Namierzono w firmware funkcje czytające klucz `"pwd"` (4 bajty) @ `0x08060850`.
4. Zlokalizowano tablicę definicji KV @ `0x08085458` (18 wpisów).
5. Tablica wskazała, że wartość klucza `"pwd"` jest cache'owana w RAM pod adresem **`0x2000027C`**.
6. Zrzucono RAM z żywego firmware (48 KB) → `firmware/ram_full.bin`.
7. Odczytano 4 bajty: `a1 25 00 00` = **`0x25A1` = 9633**.
8. PIN działa przy logowaniu do kosiarki.

---

## Hardware

| Element | Opis |
|---------|------|
| U13 | GD32F305 (Cortex-M4). Flash **1 MB** — `firmware/u13_flash_1mb.bin` |
| U22 | EEPROM 24C02 (256 B) na I2C2, dev addr **`0xD0`** (7-bit `0x68`) |
| I2C peryferium | **I2C2** @ `0x40005800` (APB1EN bit22). I2C1 @ `0x40005400` WYŁĄCZONY |
| Piny I2C2 | **PB10 = SCL, PB11 = SDA** (GPIOB CRH: PB10=0xF, PB11=0xF = AF open-drain) |
| GPIOB | STM32F1-style @ `0x40010C00` (NIE F3-style `0x48000400`) |
| RCC | `0x40021000` (GD32F30x) |
| U16 | Watchdog — reset płyty ~250–300 ms po halt U13 |
| P4 | SWD do debugprobe Pico (CMSIS-DAP v2) |

## Flashing, building, debugging

### Kompilacja stubu
```bash
arm-none-eabi-gcc -c -mcpu=cortex-m4 \
    -I . -mthumb -O2 -fomit-frame-pointer \
    -ffreestanding -nostartfiles -o eeprom_diag.o tools/eeprom_diag.c
arm-none-eabi-gcc -T tools/link.ld -nostartfiles -nostdlib -ffreestanding \
    -mcpu=cortex-m4 -mthumb -o tools/eeprom_diag.elf tools/eeprom_diag.o -lgcc
arm-none-eabi-objcopy -O binary eeprom_diag.elf tools/eeprom_diag.bin
```

### Uruchomienie OpenOCD
```bash
openocd -f tools/dump_chunks.cfg [opcje]
```

---

## Szczegółowe odkrycia

### 1. I2C2, nie I2C1 — piny PB10/PB11

Firmware używa peryferium @ `0x40005800` = **I2C2**. Piny PB6/PB7 (sprawdzane wcześniej)
to GPIO niezwiązane z EEPROM. Właściwe piny to **PB10/PB11** (AF open-drain).
Potwierdzone odczytem GPIOB CRH (`0x40010C0C` = `0x3333ff34`) i RCC APB1EN (bit22 I2C2EN=1).

### 2. Adres urządzenia = 0xD0

Funkcja firmware'owa `0x08053930` używa device control byte **`0xD0`** (7-bit `0x68`).
Różni się od typowego `0xA0` dla 24C02 — prawdopodobnie multipleksowany z innym
urządzeniem na tej samej magistrali, lub pin A0/A1/A2 podciągnięty inaczej.

### 3. Tryb Thread zamiast PendSV — koniec z faultami FPU/MPU

Poprzednie podejście (PendSV + VTOR) powodowało MemManage DACCVIOL na FPDSCR
(`0xE000EDF8`) przy lazy-stackingu FPU podczas wejścia w wyjątek.

**Rozwiązanie**: nie używać żadnego wyjątku. Ustawić bezpośrednio kontekst Thread:
```
reg control 0          ; privileged thread, MSP
reg primask 1          ; maska przerwań
reg faultmask 1
reg msp/sp 0x20000380
reg xPSR 0x01000000    ; T-bit
reg pc <stub_addr+1>
```

Plus w samym stubie na starcie: `CPACR=0`, `FPCCR=0`, `MPU_CTRL=0`.
Wtedy kod biegnie w Thread, brak wejścia w wyjątek → brak lazy FP stacking → brak faulta.

### 4. SWRST musi zachować rejestry zegara

SWRST (CTL0=`0x8000`) kasuje CTL1, CKCFG, RT. Trzeba je zapisać i odtworzyć:
| Rejestr | Wartość |
|---------|---------|
| CTL1 | `0x003C` |
| CKCFG (CCR) | `0x8032` (Fast Mode 400 kHz) |
| RT (TRISE) | `0x0013` |

Kolejność przywracania: CTL1 → PE=1 → CKCFG → RT → ACKEN.

### 5. Pojedyncze random-ready działają; sekwencyjne (BTC) — nie

Czytanie N bajtów jako N niezależnych transakcji (START → dev_w → word_addr →
restart → dev_r → NACK po ADDSEND → STOP → RBNE → DR) działa pewnie.
Sekwencyjny odczyt wielobajtowy zawieszał się na obsłudze BTC/ACK ostatnich bajtów.

**Ważna kolejność dla single-byte read**: najpierw poczekać na ADDSEND,
DOPIERO POTEM ustawić NACK i wyczyścić ADDSEND, potem STOP, potem czekać RBNE.

### 6. Watchdog ~250–300 ms; bus wedge po ~96 B

- Okno watchdog wystarcza na ~16–96 bajtów na jeden `resume`.
- Po ~96 bajtach SDA zostaje nisko (slave trzyma) — SWRST nie pomaga.
  Wymaga **power-cycle płyty** (odłącz/podłącz zasilanie) by zresetować slave.
- NIE robić `resume` firmware między próbami — pogarsza stan magistrali na stałe.

---

## KV Store w firmware — architektura

Firmware zawiera wbudowany system klucz-wartość (EasyFlash) z:

### Tablica definicji (flash `0x08085458`, 18 wpisów × 12 B)
```
struct { u32 key_ptr; u32 value_buf_ptr; u32 max_size; };
```

| # | Klucz | Bufor RAM | Rozmiar | Flagi |
|---|-------|-----------|---------|-------|
| 0 | `cfg_ver` | `0x20000168` | 4 | 0x04 |
| 1 | `mb_hv` | `0x2000017c` | 4 | 0x04 |
| 2 | `mb_sv` | `0x20000180` | 4 | 0x04 |
| 3 | `pdt_ver` | `0x20000178` | 4 | 0x04 |
| 4 | `sn` | `0x200001b0` | 21 | 0x15 |
| 5 | `type_param` | `0x20000188` | 40 | 0x28 |
| 6 | `rb_en_mag` | `0x200001f0` | 16 | 0x30 |
| 7 | `lboard_en` | `0x20000208` | 1 | 0x01 |
| 8 | `shape_param` | `0x200001c8` | 40 | 0x28 |
| 9 | `user_name` | `0x20000280` | 32 | 0x20 |
| 10 | `language` | `0x20000279` | 1 | 0x01 |
| **11** | **`pwd`** | **`0x2000027C`** | **4** | **0x04** |
| **12** | **`usr_pwd_en`** | **`0x20000278`** | **1** | **0x01** |
| 13 | `run_param` | `0x20000220` | 80 | 0x50 |
| 14 | `ota` | `0x20000270` | 4 | 0x04 |
| 15 | `ota_date` | `0x20000274` | 4 | 0x04 |
| 16 | `exhibition_cfg` | `0x200002a0` | 24 | 0x18 |
| 17 | `test_add` | `0x200002b8` | 4 | 0x04 |

### Indeks RAM (tablica lookup @ `0x200007a0`, 32 wpisy × 8 B)

Służy do szybkiego wyszukiwania klucza przez hash CRC-16:
```
struct { u16 hash; u16 _pad; u32 value_ptr; };
```

Hash funkcja (`0x47a20`): CRC-8 lookup-table z inicjacją `~0`, finalnym `~0`
i przesunięciem >> 16. Tablica CRC (256× u32) @ `0x08085E70`.

### Persystencja

Wartości zmienione przez użytkownika są zapisywane w flash @ `0x08028000`
(alias `0x00028000`). Wartość dla `pwd` (9633) znaleziona w RAM — niewykluczone,
że PIN był wpisany przez użytkownika wcześniej i załadowany z EEPROM lub flash.

---

## Zrzut cache'owanych wartości KV z RAM

Zrzucono 48 KB RAM (`firmware/ram_full.bin`, adresy `0x20000000–0x2000BFFF`).

### Kluczowe wartości:

| Klucz | RAM addr | Wartość (hex) | Znaczenie |
|-------|----------|---------------|-----------|
| `pwd` | `0x2000027C` | `a1 25 00 00` | PIN = **9633** |
| `usr_pwd_en` | `0x20000278` | `01` | PIN włączony |
| `language` | `0x20000279` | `00` | Język domyślny |
| `user_name` | `0x20000280` | `"MyMower\0"` | Nazwa urządzenia |
| `ota` | `0x20000270` | `00 00 00 00` | OTA nieaktywne |
| `run_param` | `0x20000220` | (80 B) | Parametry jazdy |
| `sn` | `0x200001b0` | `"2312CGF2"` | Serial number |

---

## EEPROM — stan odczytu

**Zweryfikowane**: bajty 0x00–0x5F. Reszta (0x60–0xFF) pozostaje nieodczytana
z powodu trwałego zawieszenia magistrali I2C (slave trzyma SDA nisko).

```
00: 00 00 00 00 00 a2 00 00 00 00 00 b6 00 00 00 00
10: 00 00 00 05 03 00 80 00 10 00 10 e0 ff ff 94 ff
20: c7 ff fa 10 3c 00 3c 00 0f 00 0d 00 00 0c 3b 00
30: 68 00 00 00 04 00 00 00 00 00 00 00 00 00 00 00
40: 00 00 00 00 00 00 00 00 00 00 00 00 50 91 0f 28
50: 28 16 11 0d 39 00 82 0c 10 00 00 81 00 00 00 2f
```

Dane wyglądają na stan operacyjny (godziny pracy, kalibracja, timestamp @ `0x4C`).
Brak oczywistego ASCII PINu — PIN jest przechowywany w formacie uint32 LE
w cache'u RAM, a w EEPROM może być zapisany tym samym formatem w nieodczytanym
zakresie 0x60–0xFF.

---

## Pliki projektu

| Plik | Opis |
|------|------|
| `firmware/u13_flash_1mb.bin` | Pełny zrzut flash U13 (1 MB) |
| `firmware/ram_full.bin` | Zrzut RAM `0x20000000–0x2000BFFF` (48 KB) |
| `firmware/ram_low.bin` | Wcześniejszy zrzut RAM `0x20000000–0x20003FFF` (16 KB) |
| `eeprom/eeprom_part.bin` | Zweryfikowane bajty EEPROM 0x00–0x5F |
| `tools/tools/eeprom_diag.c` | Stub diagnostyczny I2C (Thread mode) |
| `tools/eeprom_diag.bin` | Skompilowany stub |
| `tools/tools/link.ld` | Linker script: `0x20000400`, len `0xC00` |
| `tools/dump_chunks.cfg` | OpenOCD config do batch dumpu EEPROM |
| `tools/dump_full_ram.cfg` | OpenOCD config do dumpu RAM |

### Przydatne komendy

```bash
# Dump RAM (48 KB)
openocd -f tools/dump_full_ram.cfg

# Odczyt i parsowanie tablicy definicji KV z flash
python3 <<'EOF'
import struct
data = open('firmware/u13_flash_1mb.bin','rb').read()
for i in range(18):
    off = 0x85458 + i*12
    kp, vp, sz = struct.unpack('<III', data[off:off+12])
    kend = kp - 0x08000000
    while data[kend] != 0: kend += 1
    key = data[kp-0x08000000:kend].decode()
    print(f'{i:2d} {key:15s} @0x{vp:08x} sz={sz}')
EOF
```

---

## Lekcje na przyszłość

1. **KV-store > I2C**: Zamiast walczyć z I2C (watchdog, bus wedge, restart),
   warto od razu sprawdzić czy firmware ma cache'owane wartości w RAM.
2. **Hash CRC w KV**: Tablica indeksująca używa CRC-16 (nie hash klucza z biblioteki).
   Należy użyć właściwej tablicy lookup @ `0x08085E70`.
3. **Thread mode**: Gdy FPU/MPU powodują problemy z wyjątkami, Thread mode
   (ustawiany przez `reg pc` bez PendSV) jest prostszym rozwiązaniem.
4. **Flash alias**: GD32F305 aliasuje flash pod `0x00000000` i `0x08000000` —
   firmware używa obu wskaźników zamiennie.
5. **Definicje KV w flash vs runtime**: Tablica definicji (`0x08085458`) zawiera
   adresy buforów RAM dla każdego klucza oraz rozmiar. Wartości domyślne są
   inicjalizowane w tych buforach podczas bootu, a potem nadpisywane
   z persystentnego storage'u (flash/EEROM) jeśli zostały zmienione.
