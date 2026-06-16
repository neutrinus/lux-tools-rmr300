# EEPROM dumping — postępy i wnioski

## Cel
Odczytanie kodu PIN (i reszty danych) z EEPROM U22 (24C02/04, I2C) na płycie kosiarki Lux Tools A-RMR-300-24 przez SWD na U13 (GD32F305).

## Hardware

| Element | Opis |
|---------|------|
| U13 | GD32F305 (Cortex-M4, clone STM32F305) |
| U22 | EEPROM 24C02/04 na I2C (SCL=PB6, SDA=PB7) |
| U16 | Watchdog (musi być karmiony, inaczej reset po ~1-2 s od zatrzymania U13) |
| P4 | SWD (SWCLK, SWDIO, GND) do debugprobe Pico (CMSIS-DAP v2) |
| I2C peryferium | I2C1 na adresie `0x40005800` (NIE standardowe `0x40005400` jak STM32F303!) |
| CR1 | `0x0401` (PE=1, ACK=1) |
| CR2 | `0x003C` (FREQ=60 — APB1=60 MHz) |
| CCR | `0x8032` (F/S=1, Fast 400 kHz) |

## Podejścia

### A. ARM thumb2 binary w SRAM + SWD (obecne, działa częściowo)
Ładujemy mały (232 B) program napisany w C do SRAM (`0x20000400`), przerywnik PendSV kieruje do naszego kodu (przez podmianę VTOR).

### B. TCL-only (porzucone)
Próby manipulacji I2C z TCL OpenOCD były zbyt wolne — watchdog resetował CPU.

### C. CH341A + odsysacz/stripper (nie preferowane)
Usunięcie conformal coating z U22 by podpiąć CH341A — ostateczność.

## Kluczowe odkrycia

### 1. Watchdog U16
Po halt U13, watchdog U16 resetuje cały układ po ~1-2 s. Rozwiązanie: kod w SRAM działa w tym oknie, po 200 ms sleep halt robimy dump.

### 2. PendSV + VTOR trick
Bez podmiany VTOR, PendSV (RTOS context switch) natychmiast przejmuje CPU po resume. Rozwiązanie: ustawiamy VTOR na `0x20000000` (SRAM), a wektor PendSV na adres naszego kodu.

### 3. MPU + FPU lazy stacking
Firmware ma włączony MPU. Gdy FPU lazy stacking próbuje odczytać FPDSCR (`0xE000EDF8`) przy wejściu w PendSV, MPU blokuje dostęp → MemManage fault (CFSR=0x00020000, DACCVIOL).

**Rozwiązanie**: wyłączyć FPU przed resume:
```
CPACR (0xE000ED88) = 0    — deny FPU access
FPCCR (0xE000EDF0) = 0    — disable lazy/auto stacking
```

### 4. Adres I2C1
`0x40005400` zwraca `0x00000000` — I2C1 NIE jest na standardowym adresie STM32.
`0x40005800` zwraca `0x0401` (CR1), `0x003C` (CR2) itd.
To może być I2C2 na standardowym STM32F3, ale firmware używa PB6/PB7 które są I2C1_SCL/SDA.

### 5. SR1/SR2 po resecie I2C
Po wyłączeniu FPU — kod w SRAM uruchamia się w handlerze PendSV bez faulta.
CR1 = `0x1401` (START=1), ale SR1=0 (SB nie gotowy), SR2=0 (BUSY=0).
I2C START został wysłany ale nie zakończył się sukcesem w ciągu 200 ms.

## Proces — jak użyć

### Wymagania
- `arm-none-eabi-gcc` (z `arm-none-eabi-gcc-cs` na Fedorze)
- `openocd` ≥ 0.12 z obsługą CMSIS-DAP
- Raspberry Pi Pico z debugprobe firmware (CMSIS-DAP v2)
- Połączenie SWD: P4 na płycie → Pico (SWCLK, SWDIO, GND)

### Krok po kroku

#### 1. Kompilacja

```bash
arm-none-eabi-gcc -c -mcpu=cortex-m4 -mthumb -O2 \
    -fomit-frame-pointer -ffreestanding -nostartfiles \
    -o eeprom_read.o eeprom_read.c

arm-none-eabi-gcc -T link.ld -nostartfiles -ffreestanding \
    -o eeprom_read.elf eeprom_read.o -lgcc

arm-none-eabi-objcopy -O binary eeprom_read.elf eeprom_read.bin
```

Plik wynikowy: `eeprom_read.bin` (232 B). Ładowany do SRAM pod `0x20000400`.

#### 2. Uruchomienie

```bash
openocd -f load_run.cfg 2>&1
```

Skrypt `load_run.cfg` wykonuje sekwencyjnie:

| Krok | Opis |
|------|------|
| init + halt | Zatrzymuje CPU (watchdog zaczyna odliczać ~1-2 s) |
| load_image | Ładuje `eeprom_read.bin` do SRAM pod `0x20000400` |
| Zero buffer | Zeruje 256 B bufora pod `0x20001000` |
| Vector table | Wypełnia wektory 0-15 (wszystkie → nasz kod) pod `0x20000000` |
| VTOR = `0x20000000` | Przekierowuje tablicę wektorów do SRAM |
| Disable FPU | `CPACR=0`, `FPCCR=0` — blokada MPU na FPDSCR |
| Mask NVIC | Wyłącza wszystkie przerwania zewnętrzne |
| Pend PendSV | Ustawia bit PENDSVSET w ICSR |
| Resume (200 ms) | Wznawia CPU — PendSV odpala nasz kod |
| Halt | Zatrzymuje CPU po 200 ms |
| Read buffer | Zapisuje 256 B do `eeprom_dump.txt` |
| Restore VTOR | Przywraca oryginalny VTOR = `0x08000000` |
| Resume | Wznawia firmware (watchdog restart) |

### load_run.cfg

```tcl
source [find interface/cmsis-dap.cfg]
adapter driver cmsis-dap
cmsis_dap_vid_pid 0x2e8a 0x000c
transport select swd
adapter speed 1000
source [find target/stm32f3x.cfg]

set VEC_ADDR  0x20000000
set CODE_ADDR 0x20000400
set BUF_ADDR  0x20001000

init
halt

echo "=== Load binary ==="
load_image /home/marek/tmp/kosiarka/eeprom_read.bin $CODE_ADDR

echo "=== Zero buffer ==="
for {set i 0} {$i < 256} {incr i 4} {
    mww [expr {$BUF_ADDR + $i}] 0
}

echo "=== Set up full vector table (exceptions 0-15) ==="
mww [expr {$VEC_ADDR + 0x00}] 0x20000300
for {set i 1} {$i < 16} {incr i} {
    mww [expr {$VEC_ADDR + $i*4}] [expr {$CODE_ADDR | 1}]
}

echo "=== VTOR to SRAM, disable FPU + exceptions ==="
mww 0xE000ED08 $VEC_ADDR
mww 0xE000E010 0x00000000
mww 0xE000ED04 0x0A000000
mww 0xE000ED24 0x00000000
mww 0xE000ED88 0x00000000
mww 0xE000EDF0 0x00000000
mww 0xE000E280 0xFFFFFFFF
mww 0xE000E284 0xFFFFFFFF
mww 0xE000E288 0xFFFFFFFF
mww 0xE000E28C 0xFFFFFFFF
mww 0xE000E180 0xFFFFFFFF
mww 0xE000E184 0xFFFFFFFF
mww 0xE000E188 0xFFFFFFFF
mww 0xE000E18C 0xFFFFFFFF

echo "=== Pend PendSV ==="
mww 0xE000ED04 0x10000000

echo "=== Resume (200ms) ==="
reg sp 0x20000300
reg msp 0x20000300
resume
sleep 200
halt

echo "=== Fault Debug ==="
set cfsr [capture { mdw 0xE000ED28 1 }]
echo "CFSR: $cfsr"
# ... (CFSR, HFSR, I2C rejestry itp.)

echo "=== Restore VTOR ==="
mww 0xE000ED08 0x08000000

echo "=== Read buffer ==="
set fd [open /home/marek/tmp/kosiarka/eeprom_dump.txt w]
for {set i 0} {$i < 256} {incr i 4} {
    set cap [capture { mdw [expr {$BUF_ADDR + $i}] 1 }]
    # ... parsowanie i zapis do pliku
}
close $fd

echo "=== Done ==="
resume
shutdown
```

#### 3. Cykl debugowania

Po każdej próbie (nawet nieudanej) watchdog resetuje płytę. Przed ponownym uruchomieniem:

```bash
# power-cycle płyty (odłącz zasilanie, podłącz)
pkill -9 openocd 2>/dev/null; sleep 1
openocd -f load_run.cfg 2>&1
```

Skrypt sam robi `init + halt`, więc nie trzeba nic więcej.

Jeśli OpenOCD raportuje `target was in unknown state when halt was requested` — to normalne (CPU przed haltem był w stanie nieznanym dla debuggera), skrypt dalej działa.

#### 4. Interpretacja wyniku

Po uruchomieniu w logu OpenOCD widać:

```
current mode: Handler PendSV   ← kod działa!
xPSR: 0x4100000e pc: 0x2000041c
CFSR: 0x00000000               ← brak błędów
0x40005800: 00001401           ← CR1: PE=1, ACK=1, START=1
SR1: 0x00000000                ← SB=0 — START się nie zakończył
SR2: 0x00000000                ← BUSY=0 — magistrala nie zajęta
```

Jeśli CFSR ≠ 0 — był fault; dekodowanie:
- `0x00020000` = MemManage DACCVIOL (FPU lazy stacking — wyłącz FPU)
- `0x00010000` = MemManage IACCVIOL
- `0x00000200` = BusFault PRECISERR
- `0x00000400` = BusFault IMPRECISERR

Plik `eeprom_dump.txt` zawiera 256 bajtów z bufora (hex, little-endian).

#### 4. Debugowanie

Aby dodać odczyt rejestrów w skrypcie:

```tcl
set val [capture { mdw 0xE000ED28 1 }]
echo "CFSR: $val"
```

Jeśli po resecie skrypt sypie błędami — power-cycle (odłącz i podłącz zasilanie płyty).

## Status obecny
- kod C ładuje się i startuje poprawnie
- I2C START jest wysyłany (CR1.START=1)
- SB bit się nie ustawia — I2C nie generuje warunku START
- magistrala nie jest zajęta (BUSY=0)
- podejrzenie: SCL/SDA są w złym stanie (pull-up? zablokowane?)
- albo konformał blokuje komunikację I2C

## Co dalej
1. Zmierzyć SCL/SDA oscyloskopem po resecie i po próbie START
2. Spróbować dłuższy timeout — może START potrzebuje resetu peryferium
3. Wysłać STOP przed START żeby zresetować stan magistrali
4. Sprawdzić pull-upy na PB6/PB7 (może brak rezystorów?)
5. Spróbować wysłać warunek START zezwalając na bus error (ignore BERR)
6. Alternatywnie: przerzucić się na CH341A po usunięciu konformalu z U22
