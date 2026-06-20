# LCD Sweep Test — Flashing Instructions

## Pliki binarne

Gotowe do flashowania (zbudowane dla ESP32-WROOM-32UE, 2MB flash):

| Plik | Piny | Opis |
|------|------|------|
| `lcd_sweep_setA.bin` | CLK=33, MOSI=18, CS=32 | **Zalecany** — jedyna kombinacja która zapaliła wyświetlacz |
| `lcd_sweep_setA_factory.bin` | jw. | factory image (bootloader + partitions + app) |
| `lcd_sweep_setB.bin` | CLK=25, MOSI=32, CS=33 | Alternatywa z analizy cross-referencji |
| `lcd_sweep_setB_factory.bin` | jw. | factory image |

## Zasilanie

**UWAGA**: USB2TTL (3.3V) **NIE wystarczy** — wyświetlacz + ESP32 potrzebują +5V przez J8.

### Opcja 1: Przez J8 (zalecane)
```
J8 pin 1 → +5V (zasilacz laboratoryjny lub ATX)
J8 pin 5 → GND
```
Przy okazji można podejrzeć UART na J8 pin 3-4.

### Opcja 2: Przez ESP32 USB (jeśli płytka ma USB-UART bridge)
Niektóre warianty SNK_DISPLAY mają USB-UART na płytce — wtedy USB wystarczy.

### Opcja 3: Zewnętrzny stabilizowany 5V na VIN ESP32
Podpiąć 5V i GND do pinu VIN i GND na module ESP32.

## Sposób flashowania

ESP32 ma UART0 na GPIO1 (TX) / GPIO3 (RX) — **J1 na płytce**.

### Podłączenie USB2TTL

| USB2TTL | ESP32 (J1 header) |
|---------|-------------------|
| TX | GPIO3 / RXD0 (J1 pin 4) |
| RX | GPIO1 / TXD0 (J1 pin 5) |
| GND | GND (J1 pin 3 lub 2) |
| 3.3V | — **nie podłączać, zasilanie osobno** |

### Wejście w tryb download

```
1. Podłącz USB2TTL (tylko TX/RX/GND, bez zasilania!)
2. Włącz zasilanie +5V przez J8 (lub VIN)
3. Przytrzymaj BOOT (IO0 do GND)
4. Kliknij EN (RESET) i puść
5. Puść BOOT
6. ESP32 jest w trybie download
```

### Flashowanie

```bash
# Opcja A: esptool (factory image — wszystko w jednym)
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  write_flash --flash_mode dout --flash_size 2MB \
  0x0 lcd_sweep_setA_factory.bin

# Opcja B: esptool (aplikacja, jeśli już masz bootloader+partitions)
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  write_flash --flash_mode dout --flash_size 2MB \
  0x10000 lcd_sweep_setA.bin

# Opcja C: PlatformIO (dla wygody)
# Ustaw port w platformio.ini: upload_port = /dev/ttyUSB0
pio run --target upload -d .
```

Po flashowaniu: wciśnij EN (RESET), odczep BOOT.

## Czego się spodziewać

Program wykona sekwencję (w pętli):

1. **Phase 1**: Każdy z 24 bitów osobno, po ~2s każdy
   → Patrz na wyświetlacz: który bit zapala który segment
2. **Phase 2**: Wszystkie bity = 1 (0xFFFFFF) — 3s
3. **Phase 3**: Wszystkie bity = 0 (0x000000) — 3s
4. **Phase 4**: Po kolei bajt0=FF, bajt1=FF, bajt2=FF — po 1.5s każdy
5. Powrót do Phase 1

### Co zapisać

- Który bit (0-23) = który segment (a-g, dp) i która cyfra (1-4)
- Czy segmenty świecą się przy 1 czy 0 (active high vs active low)
- Czy kolumna (colon) jest bit 22 czy 23
- Czy wogóle coś się świeci (walidacja pinów)

## Wyjście z trybu monitor

Program wysyła logi na UART przez mostek USB2TTL:
```
pio device monitor -b 115200
```
lub
```
screen /dev/ttyUSB0 115200
```
Przerwij: `Ctrl+]` (screen) lub `Ctrl+C` (pio).

## Jeśli nie działa

1. Sprawdź czy piny CLK/MOSI/CS są poprawne — przełącz na SET B
2. Sprawdź oscyloskopem czy na CLK jest sygnał (ok. 30kHz)
3. Sprawdź czy 74HC595 ma zasilanie (VCC=5V, GND)
4. Sprawdź ciągłość multimetrem: ESP32 → 74HC595
5. Zwiększ delay w `main.c` (linia `esp_rom_delay_us(1)` → dłuższe)
