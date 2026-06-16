# SNK Mower PIN Recovery — Lux Tools A-RMR-300-24

**PIN odzyskany: 9633** ✅

Procedura odzyskania PINu z firmware kosiarki przez SWD — krok po kroku.
Kosiarka to OEM platforma SNK, sprzedawana również jako **Adano RM5** (Harald Nyborg / Schou).

---

## Szybki start (działało)

Czego potrzebujesz:
- Raspberry Pi Pico z [debugprobe](https://github.com/raspberrypi/debugprobe) UF2
- 3x przewody Dupont (GND, SWCLK, SWDIO)
- Dostęp do pola P4 na płycie głównej (przez otwór wentylacyjny)
- OpenOCD na komputerze

### 1. Podłącz SWD do U13 (GD32F305)

P4 na głównej płycie (od góry do dołu): `3V3 DIO CLK JTDO RES GND`

| Pico pin | Pico GPIO | SWD | P4 pin |
|----------|-----------|-----|--------|
| Pin 3 | GND | GND | 6 (dół) — GND (TP81) |
| Pin 4 | GP2 | SWCLK | 3 — CLK (TP77) |
| Pin 5 | GP3 | SWDIO | 2 — DIO (TP76) |

> Nie podłączaj 3.3V z Pico — kosiarka zasila się sama.

### 2. Zrzuć RAM z żywego firmware

```bash
openocd -f tools/dump_full_ram.cfg
```

Plik `firmware/ram_full.bin` (48 KB, adresy `0x20000000–0x2000BFFF`).

### 3. Odczytaj PIN

```bash
# PIN = 4 bajty pod adresem 0x2000027C (little-endian uint32)
python3 -c "
import struct
ram = open('firmware/ram_full.bin', 'rb').read()
val = struct.unpack('<I', ram[0x27c:0x280])[0]
print(f'PIN = {val:04d}')
"
```

PIN to 4-cyfrowa liczba zapisana jako **uint32 LE** w cache'u KV-store firmware'u.

### Dlaczego to działa?

Firmware U13 (GD32F305) zawiera wbudowany system **KV-store** (klucz-wartość).
Klucz `"pwd"` przechowuje 4-bajtowy PIN. Wartość jest cache'owana w RAM
pod adresem `0x2000027C` — wystarczy zrzucić RAM i odczytać.

Szczegóły architektury KV-store: [notes/GD32F305.md](notes/GD32F305.md).

---

## Dlaczego NIE wybraliśmy innych metod

### ❌ Odczyt EEPROM U22 przez I2C

Próbowaliśmy — udało się nawiązać łączność I2C2 (`0x40005800`) z EEPROM
(adres `0xD0`), sczytać bajty 0x00–0x5F. Niestety po ~96 bajtach magistrala
I2C się zawiesza (slave trzyma SDA nisko) i wymaga power-cycle'a.
PIN nie był w sczytanym zakresie, a dalsza walka z I2C była niepotrzebna.

Szczegóły: [notes/eeprom_dumping.md](notes/eeprom_dumping.md).

### ❌ FORMATFLASH.json przez USB (nie działa)

W firmware istnieje string `"FORMATFLASH.json"` sugerujący możliwość
fabrycznego resetu przez pendrive. **Dead code** — nie ma żadnej referencji
do tego stringa w kodzie. Mechanizm nie istnieje w tym firmware.

### ❌ Ghidra headless decompilation

Próba pełnej dekompilacji U13 firmware przez Ghidra 12.1.2 headless
(z Java 21/25) nie powiodła się z powodu problemów z OSGi bundlingiem
wtyczki Python/Java. Częściowa dekompilacja wykonana przez ghidra-cli
(wrapper `tools/ghidra-cli`). Wyniki w `decomp_*.c` i `decompilation.md`.

### ❌ Szukanie PINu we flash

Plaintext PIN (jako 4 cyfry ASCII lub BCD) nie występuje nigdzie we
flashu U13 (1 MB), U16 (256 KB) ani ESP32 (4 MB). PIN jest przechowywany
binarnie jako uint32, a nie jako string.

### ❌ Odczyt z ESP32

ESP32 na płytce wyświetlacza obsługuje tylko interfejs użytkownika
(przyciski, wyświetlacz, brzęczyk). PIN jest wysyłany z ESP32 do
płyty głównej w celu weryfikacji — ESP32 go nie przechowuje.

---

## Architektura systemu

```
┌──────────────────────────────────────────────────────────────┐
│ Display Board (SNK_DISPLAY_CP_V11)                           │
│  ESP32-WROOM-32UE — UI, WiFi/BT (niewykorzystane), buzzer   │
│  4-digit 7-segment LED + 4 przyciski                         │
└──────────┬───────────────────────────────────────────────────┘
           │ UART @115200, binarny protokół (0xAA 0x55 + XOR CS)
           │ Komendy: 0x0B = PWD_VERIFY, 0x0C = PWD_RESULT
┌──────────▼───────────────────────────────────────────────────┐
│ Main Board (SNK_MAINBOARD_CP_V11)                            │
│                                                               │
│  U16 (GD32F303) — Board MCU                                  │
│    • Sensory (lift, border, voltage)                          │
│    • Sterowanie silnikami                                     │
│    • Tłumaczy protokół ESP32 → JSON dla U13                   │
│                                                               │
│  U13 (GD32F305) — Main MCU — ★ zawiera PIN                  │
│    • KV-store w RAM: klucz "pwd" @ 0x2000027C                │
│    • I2C2 EEPROM U22 (24C02) — persystencja                  │
│    • USB Host (pendrive, IAP firmware update)                │
│    • OTA przez UART z U16                                    │
└──────────────────────────────────────────────────────────────┘
```

---

## Pliki projektu

```
kosiarka/
├── README.md                         ← ten plik
├── HARDWARE.md                       ← analiza PCB, piny, SWD
├── firmware/
│   ├── u13_flash_1mb.bin             ← pełny zrzut flash U13 (1 MB)
│   ├── u16_flash.bin                 ← zrzut flash U16 (256 KB)
│   ├── esp32_dump.bin                ← zrzut flash ESP32 (4 MB)
│   ├── ram_full.bin                  ← zrzut RAM U13 (48 KB)
│   └── ota_0.bin                     ← fragment OTA
├── notes/
│   ├── GD32F305.md                   ← analiza firmware U13 + KV-store
│   ├── U16.md                        ← analiza firmware U16
│   ├── ESP32.md                      ← analiza firmware ESP32
│   └── eeprom_dumping.md             ← epopeja I2C (dead end)
├── img/
│   ├── mainboard_top.jpg
│   ├── mainboard_bottom.jpg
│   ├── display_front.jpg
│   └── display_back.jpg
├── tools/
│   ├── dump_full_ram.cfg             ← OpenOCD config do dumpu RAM
│   ├── dump_flash.cfg                ← OpenOCD config do dumpu flash
│   ├── eeprom_diag.c                 ← stub I2C (Thread mode)
│   └── link.ld                       ← linker script dla stubów
├── eeprom/
│   └── eeprom_part.bin               ← częściowy zrzut EEPROM (0x00-0x5F)
├── decomp/
│   ├── decompilation.md              ← notatki z dekompilacji
│   └── decomp_*.c                    ← wybrane dekompilacje Ghidra
```

---

## Sprzęt

Szczegółowa dokumentacja sprzętu: [HARDWARE.md](HARDWARE.md).

| Element | Opis |
|---------|------|
| U13 | GD32F305 AGT6 — Cortex-M4, 1 MB flash, 48 KB RAM |
| U16 | GD32F303 CGT6 — Cortex-M4, 256 KB flash |
| ESP32 | ESP32-WROOM-32UE — na płytce wyświetlacza |
| U22 | 24C02 — I2C EEPROM (256 B) na I2C2 @ `0xD0` |
| SWD P4 | U13 — pola przelotkowe przy USB |
| SWD P5 | U16 — czarna listwa z lewej strony |

### Udev rule

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", MODE="0666"' | \
  sudo tee /etc/udev/rules.d/99-pico-debugprobe.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

---

## Notatki o niepotwierdzonych metodach

### FORMATFLASH.json (pendrive, reset fabryczny)

String `"FORMATFLASH.json"` istnieje w firmware pod `0x08003990`, ale:
- **Zero referencji** z kodu do tego stringa — potwierdzone przez skanowanie
  całego 1 MB flasha w poszukiwaniu wskaźników
- Stringi `"ready to format flash"` i `"format flash"` też bez referencji
- Prawdopodobnie pozostałość po firmware dla innego modelu lub wersji

Wniosek: **mechanizm nie istnieje w tym firmware**. Jedyna droga do resetu
PINu to fizyczny dostęp (SWD → RAM → odczyt PINu, jak wyżej).

---

## Powiązane produkty

- **Adano RM5** — Harald Nyborg (Dania), Schou (Skandynawia)
- Numery części: `80102372-01` (mainboard), `80102373-01` (display)

---

*Dokumentacja powstała w wyniku reverse engineeringu dla celów edukacyjnych.*
