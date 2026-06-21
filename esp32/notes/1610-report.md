# ESP32 Display Controller Reverse Engineering Report v2

## Cel analizy

Ustalenie dokładnego mapowania pinów ESP32 do wyświetlacza 7-segmentowego
(GD5643CPG-1, 3× 74HC595) oraz protokołu sterowania na podstawie oryginalnego
firmware'u `esp32_dump.bin` (wersja 3.02.02, SNK_DISPLAY_CP_V11).

## Metodologia

1. **Analiza DROM** — skanowanie stałych struktur danych (`spi_bus_config_t`,
   tablice GPIO) w DROM (flash 0x10018–0x362CC).
2. **Cross-referencje IROM** — wyszukiwanie wartości GPIO jako 32-bitowych
   literałów w sekcji IROM (flash 0x40018–0x12401C, 3102 funkcji).
3. **Grupowanie po funkcjach** — mapowanie: która funkcja zawiera które GPIO.
4. **Śledzenie historii git** — rekonstrukcja jak doszło do obecnego stanu wiedzy.
5. **Ograniczenie**: brak działającego Ghidry — czysta statyczna analiza
   asemblera Xtensa z objdump, bez możliwości dekompilacji do C.

## Historia empiricalna (z git log)

```
31ef84c display confirmed: CLK=5, CS=34, MOSI=32 via lcd_find #39/343
  → lcd_find testował 7 kandydatów × 3 role = 210 permutacji
  → combo #39 dał CLK=5, MOSI=32, CS=34 — ale CS=34 to input-only!

88a26fa lcd_find: exclude input-only GPIO34/39, fix CLK=5 MOSI=32,
          scan CS through {18,25,33}
  → 5 kandydatów {5,18,25,32,33} × 3 role = 60 permutacji
  → CLK=5 i MOSI=32 potwierdzone, CS szukany w {18,25,33}

e7654ba lcd_find: quick 6-permutation scan of pins {18,33,32}
  → Zwężono do {18,33,32} bo "coś pokazały" — GPIO5 i GPIO25 DROPPED
  → CLK=5 NIE był testowany w tej gałęzi

18344e1 lcd_find: explicit 6-permutation test, no index math
  → Zakodowane na stałe 6 permutacji {18,33,32}
  → GPIO5 nadal nie testowany jako CLK w lcd_find

b7e4f62 fix: add delays in shift24 bit-banging
  → Działa z domyślnymi pinami (obecnie 18/23/5 w yaml, ale runtime wie 5/32/?)
```

**Wniosek**: CLK=5 został potwierdzony w pełnym skanie (commit 31ef84c, combo
#39/343), ale późniejsze `lcd_find` testuje tylko {18,33,32}. GPIO5 jako CLK
jest poprawny, ale nie był weryfikowany przez `shift24` w obecnym `lcd_find`.

## Wyniki analizy firmware

### Piny wyświetlacza — stan wiedzy

| Pin | HARDWARE.md | DROM (HW SPI) | IROM (bit-bang) | Testy emp. | Werdykt |
|-----|:-----------:|:-------------:|:----------------:|:----------:|:-------:|
| **CLK** | 18 ❌ | 10 | **5** (18 funkcji) | **5** | **GPIO5** |
| **MOSI** | 23 ❌ | 12 | **32** (4 funkcje) | **32** | **GPIO32** |
| **CS** | 5 ❌ | 15 | 25 (1 funkcja z GPIO5) | ? | **GPIO25** ⚠️ |

### Dowód: GPIO25 > GPIO33 jako CS

**GPIO25** — występuje w DOKŁADNIE JEDNEJ funkcji IROM: **0x401908b4**.
Jest to JEDYNA funkcja, w której GPIO25 pojawia się z GPIO5 (CLK).

```
0x401908b4: GPIO5, GPIO25   ← jedyne wystąpienie GPIO25 w całym IROM!
```

**GPIO33** — występuje w 6 funkcjach, ale ŻADNA z nich nie zawiera GPIO5
ani GPIO32. GPIO33 współwystępuje z GPIO12 (SPI MOSI) i GPIO10 (SPI SCLK)
oraz GPIO34 — nie ma związku z wyświetlaczem.

```
0x4011d1d0: GPIO33
0x40138124: GPIO33
0x401562c0: GPIO33
0x4016ca34: GPIO33
0x40178e34: GPIO12, GPIO33
0x4019e9fc: GPIO10, GPIO33, GPIO34
```

| Kryterium | GPIO25 | GPIO33 |
|-----------|:------:|:------:|
| Występuje z GPIO5 (CLK) | ✅ TAK (0x401908b4) | ❌ NIE |
| Występuje z GPIO32 (MOSI) | ❌ NIE | ❌ NIE |
| Ścieżka PCB do 74HC595 | ✅ R34→TP29→przelotka | ❌ tylko do ADC? |
| Normalny I/O (output) | ✅ TAK | ✅ TAK |

GPIO33 jest prawdopodobnie używane do ADC/czujników — pojawia się w
DROM w tablicy ADC1 (obok GPIO32,34,35,37,38,39 w DROM 0x3f4172FC).

### Funkcje sterownika wyświetlacza

| Adres | Stack | GPIO | Opis |
|-------|:-----:|:----:|------|
| **0x40199138** | 0x310 | 5(×2),32 | **TubeInit** — konfiguracja pinów, alokacja, dużo kodu |
| **0x40197afc** | 0xC0 | 5,32 | **Init display** — wywołuje callx8, konfiguruje struktury |
| **0x401908b4** | 0x40 | **5,25** | **CS/Latch init** — zapis do rejestru GPIO (MEMW+store) |
| 0x4014af70 | — | 27,32,34 | Init buzzera + MOSI? |
| 0x40198c78 | 0x40 | 5 | Parsowanie/tworzenie stringów |
| 0x401736a4 | — | 5 | Timer |
| 0x4016f848 | — | 5 | Timer |
| 0x40185414 | — | 5 | Timer |

### Struktury DROM

```
SPI bus config (NIE dla wyświetlacza):
  DROM 0x3f4177E0: MOSI=12, MISO=-1, SCLK=10, quadwp=-1, ...

Obszar GPIO config (wyświetlacz?):
  DROM 0x3f41760C: 0x10000, GPIO3, GPIO25, GPIO32, 0x3FF48494
                    (flaga, pin?, CS?, MOSI?, register?)
```

### Timing — analiza shift24

**Nie udało się** zidentyfikować funkcji bit-banging loop (shift24)
w disassembly. Powody:

1. Funkcja może być inlined lub zoptymalizowana przez kompilator
2. Wywołania `gpio_set_level()` są przez `callx8` z rejestru — adres
   funkcji jest ładowany z literału, a GPIO numer przekazywany w
   rejestrze a2/a10 (NIE jako stała 5/32 w każdej iteracji)
3. Opóźnienia (delayMicroseconds) są realizowane przez pustą pętlę
   lub NOP — nie można określić czasu bez wykonania kodu

**Aby ustalić timing** potrzebne jest:
- Uruchomienie kodu na ESP32 z oscyloskopem na GPIO5
- Albo dekompilacja przez Ghidrę (pełna analiza przepływu)
- Albo emulacja/resymulacja Xtensa

### Protokół sterowania (z implementacji ESPHome)

```
shift24(clk=GPIO5, mosi=GPIO32, cs=GPIO25?, byte0, byte1, byte2):
  gpio_set_level(cs, 0)
  delayMicroseconds(2)
  for 3 bytes:
    for 8 bits MSB-first:
      gpio_set_level(mosi, bit)
      delayMicroseconds(1)
      gpio_set_level(clk, 1)
      delayMicroseconds(1)
      gpio_set_level(clk, 0)
      delayMicroseconds(1)
  delayMicroseconds(2)
  gpio_set_level(cs, 1)
```

Multipleksowanie: 4 cyfry, każdej ramka co 4ms (16ms pełny cykl).

## Rekomendacje

1. **Przetestować GPIO25 jako CS** — ustawić w yaml:
   ```yaml
   display_clk: 5
   display_mosi: 32
   display_cs: 25
   ```
   (obecny `lcd_find` został zaktualizowany do {5,25,32})

2. **Zweryfikować ciągłość** — multimetrem: ESP32 pad 10 (GPIO25) → ST_CP
   (pin 12) 74HC595 U1/U3/U4. Jeśli tak, GPIO25 to CS.

3. **Zmierzyć timing oscyloskopem** — podpiąć probe na GPIO5 (CLK),
   zmierzyć czas trwania zbocza i okres impulsu. To da dokładny timing
   dla implementacji ESPHome.

4. **Zbadać funkcję 0x401908b4 pod Ghidrą** — jeśli dostępny jest Ghidra,
   dekompilacja tej funkcji może ujawnić:
   - Czy zapisuje do GPIO_OUT_W1TS/W1TC (latch CS)
   - Czy inicjalizuje pin direction
   - Jaka jest sekwencja

5. **Aktualizacja HARDWARE.md** — poprawić błędne mapowanie pinów:
   - CLK=5, MOSI=32, CS=? (25 lub TBD)
   - Usunąć błędne CLK=18, MOSI=23, CS=5

## Podsumowanie

| Fakt | Status | Uwagi |
|------|--------|-------|
| CLK = GPIO5 | ✅ FW + emp. | 18 funkcji IROM, potwierdzony combo #39/343 |
| MOSI = GPIO32 | ✅ FW + emp. | 4 funkcje IROM, potwierdzony combo #39/343 |
| CS = GPIO25 | ⚠️ silny kandydat | Jedyna funkcja z GPIO5+25: 0x401908b4 |
| CS = GPIO33 | ❌ brak dowodów | 6 funkcji IROM, żadna z GPIO5/32 |
| CS = GPIO34 | ❌ input-only | Nie może być CS |
| Wyświetlacz bit-bangany | ✅ | 3 piny GPIO, nie sprzętowe SPI |
| HARDWARE.md błędne | ✅ | CLK=18, MOSI=23, CS=5 — wszystko nieprawda |
| Timing shift24 | ❓ Nieznany | Wymaga oscyloskopu lub Ghidry |
