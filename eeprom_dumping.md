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
