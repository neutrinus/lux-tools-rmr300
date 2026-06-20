# Report Qwen-1836: LCD Display Control — Analiza stanu i plan działania

**Data:** 19 czerwca 2026  
**Autor:** opencode (Qwen 3.7 Plus)

---

## 1. Gdzie jesteśmy — podsumowanie stanu wiedzy

### Co wiemy na pewno

| Fakt | Dowód | Status |
|------|-------|:------:|
| Wyświetlacz to GD5643CPG-1, 4-cyfrowy 7-segmentowy + colon | napis na LCD, obserwacja | ✅ |
| 3× 74HC595 (U1, U3, U4) SOP-16 w kaskadzie = 24 bity | wizualnie na PCB, HARDWARE.md | ✅ |
| Sterowanie bit-bang (3 GPIO: CLK, MOSI, CS/latch) | shift24 w FW, testy emp. | ✅ |
| Multipleksowanie 4 cyfry × 4ms = 16ms cykl | implementacja ESPHome | ✅ |
| **Jedyna kombinacja, która zapaliła wyświetlacz: CLK=33, MOSI=18, CS=32** | lcd_find combo #4/6 z {18,33,32} | ✅ |
| Przy 0xFF,0xFF,0xFF wyświetlacz pokazał coś jak "88:88" | obserwacja użytkownika | ✅ |
| UART 230400 JSON+CRC działa, handshake OK | ha.md | ✅ |
| GPIO27 = buzzer, GPIO19 = OK button, GPIO36 = rain | potwierdzone emp. | ✅ |

### Co wiemy z dekompilacji FW (ale NIE działa empirycznie)

| Twierdzenie | Dowód FW | Problem |
|-------------|----------|---------|
| CLK = GPIO5 | 18 funkcji IROM z GPIO5 | Nigdy nie zapalił wyświetlacza |
| MOSI = GPIO32 | 4 funkcje IROM z GPIO32 | Działa tylko w combo z GPIO33 jako CLK |
| CS = GPIO25 | 1 funkcja IROM z GPIO5+25 (0x401908b4) | Nigdy nie zapalił wyświetlacza |
| DROM SPI: MOSI=12, SCLK=10 | struktura spi_bus_config_t w DROM | Sprzeczne z IROM i testami |

### Kluczowe sprzeczności

1. **FW mówi GPIO5/32/25, testy mówią GPIO33/18/32** — kompletnie różne piny
2. **GPIO5 → R28 → U6** — idzie do innego układu niż U1/U3/U4 (nie do wyświetlacza?)
3. **DROM SPI (12/10) ≠ IROM bit-bang (5/32)** — dwie różne konfiguracje w tym samym FW
4. **HARDWARE.md (5/18/23) ≠ FW (5/32/25) ≠ empiryka (33/18/32)** — trzy różne wersje

---

## 2. Analiza hipotez

### Hipoteza A: FW decompilation (GPIO5/32/25)

**Za:**
- GPIO5 w 18 funkcjach IROM — najsilniejszy dowód
- GPIO32 w 4 funkcjach IROM
- GPIO25 w 1 funkcji z GPIO5 (0x401908b4)
- Original FW działał na tym samym ESP32

**Przeciw:**
- Nigdy nie zapalił wyświetlacza w testach
- GPIO5 → R28 → U6 (inny 74HC595, nie wyświetlacz?)
- Testowane wielokrotnie: lcd_find {5,25,32} × 6 permutacji = 0 reakcji

**Werdykt:** ⭐⭐⭐ silne dowody z kodu, ale 0 potwierdzenia emp. Możliwe że FW używa dodatkowych pinów (OE/MR) których nie znamy.

### Hipoteza B: Empiryczna (GPIO33/18/32)

**Za:**
- **Jedyna kombinacja, która kiedykolwiek zapaliła wyświetlacz**
- GPIO33, GPIO18, GPIO32 — wszystkie output-capable
- "88:88" przy 0xFF,0xFF,0xFF = wszystkie segmenty świecą = komunikacja działa

**Przeciw:**
- Sprzeczna z analizą FW
- Wyświetlacz pokazał "bzdury" (garbage) — format danych nieprawidłowy
- GPIO33 wg FW analysis: 6 funkcji IROM, żadna z GPIO5/32 — brak korelacji z wyświetlaczem w kodzie

**Werdykt:** ⭐⭐⭐⭐ jedyny emp. sukces. Piny prawdopodobnie poprawne, ale format danych (bit order, byte order, segment map) wymaga odkrycia.

### Hipoteza C: GPIO5 = OE (Output Enable)

**Za:**
- 74HC595 ma pin OE (pin 13, active LOW) — bez LOW wyjścia są wyłączone
- GPIO5 → R28 → "U6" — może nie jest to CLK ale OE?
- Wyjaśniałoby dlaczego GPIO5/32/25 nie działa: GPIO5 nie jest CLK ale OE
- FW ma GPIO5 w 18 funkcjach — może OE jest ustawiane w każdej funkcji display?

**Przeciw:**
- OE nie wymaga tyle funkcji — wystarczy ustawić raz na LOW
- GPIO5 w funkcjach z GPIO32 — wygląda na parę CLK/MOSI, nie OE

**Werdykt:** ⭐⭐ ciekawa hipoteza, wymaga weryfikacji.

### Hipoteza D: Inna rewizja PCB

**Za:**
- HARDWARE.md (5/18/23) ≠ FW (5/32/25) — może HARDWARE.md opisuje inną rewizję?
- Ścieżki na PCB są polakierowane, trudno zweryfikować
- SNK platform jest współdzielony z Adano RM5 — mogą być różne warianty

**Przeciw:**
- ESP32, UART, buzzer, przyciski — wszystko inne działa na tych samych pinach
- Trudno wierzyć że tylko wyświetlacz jest na innej rewizji

**Werdykt:** ⭐⭐ możliwe ale mało prawdopodobne.

---

## 3. Dlaczego GPIO33/18/32 pokazał "bzdury"?

Skoro komunikacja działa (wyświetlacz się zapalił), ale dane są śmieciem, to możliwe przyczyny:

1. **Bit order: MSB-first vs LSB-first**
   - Nasz shift24 wysyła MSB-first (bit 7 → bit 0)
   - Jeśli hardware oczekuje LSB-first, wszystkie bity są odwrócone

2. **Byte order: kolejność bajtów w kaskadzie 74HC595**
   - Wysyłamy: `[0x00, colon|digit, segments]`
   - Może być: `[segments, colon|digit, 0x00]` lub inna permutacja
   - 74HC595 #1 w kaskadzie dostaje dane jako ostatni (po przesunięciu przez #2 i #3)

3. **Segment mapping: który bit = który segment**
   - Nasz map: a=bit0, b=bit1, c=bit2, d=bit3, e=bit4, f=bit5, g=bit6, dp=bit7
   - Hardware może mieć inne mapowanie

4. **Digit select: który bit = która cyfra**
   - Nasz map: digit0=bit0, digit1=bit1, digit2=bit2, digit3=bit3 w bajcie 1
   - Hardware może mieć odwrotnie lub inaczej

5. **Colon bits: bity 4-5 w bajcie 1**
   - Mogą być na innych pozycjach

6. **Common anode vs common cathode**
   - Jeśli wyświetlacz jest common anode, to LOW = segment ON (odwrócona logika)

---

## 4. Plan działania — eksperymenty

### Eksperyment 1: Pattern Generator z (33,18,32) — PRIORYTET 🥇

**Cel:** Jednoznacznie określić mapowanie bitów na segmenty/cyfry.

**Metoda:**
- Wyłączyć normalny display refresh
- Ręcznie wysyłać znane wzorce przez shift24 z logowaniem
- Obserwować co się świeci

**Sekwencja testowa (24 kroki × 3s = 72s):**
```
Krok 0:  0x01, 0x00, 0x00  → bit 15 (MSB bajtu 0)
Krok 1:  0x02, 0x00, 0x00  → bit 14
...
Krok 7:  0x80, 0x00, 0x00  → bit 8 (LSB bajtu 0)
Krok 8:  0x00, 0x01, 0x00  → bit 7 (MSB bajtu 1)
...
Krok 15: 0x00, 0x80, 0x00  → bit 0 (LSB bajtu 1)
Krok 16: 0x00, 0x00, 0x01  → bit 23 (MSB bajtu 2)
...
Krok 23: 0x00, 0x00, 0x80  → bit 16 (LSB bajtu 2)
```

**Oczekiwany wynik:**
- Przy trafieniu bitu cyfry → cała cyfra rozbłyśnie (wszystkie segmenty)
- Przy trafieniu bitu segmentu → jeden segment na jednej cyfrze
- Przy trafieniu bitu colon → dwukropek

**Wariant A (common cathode):** pojedynczy bit HIGH, reszta LOW
**Wariant B (common anode):** pojedynczy bit LOW, reszta HIGH

**Implementacja:**
```cpp
void run_pattern_test() {
  static int step = 0;
  static uint32_t last_step_ms = 0;
  uint32_t now = millis();
  if (now - last_step_ms < 3000) return;
  last_step_ms = now;
  
  uint8_t b0 = 0, b1 = 0, b2 = 0;
  int bit_idx = step % 24;
  bool phase_a = (step < 24);
  
  if (phase_a) {
    // Phase A: single bit HIGH, rest LOW
    if (bit_idx < 8) b0 = (1 << (7 - bit_idx));
    else if (bit_idx < 16) b1 = (1 << (15 - bit_idx));
    else b2 = (1 << (23 - bit_idx));
  } else {
    // Phase B: single bit LOW, rest HIGH
    uint32_t val = ~(1 << (23 - bit_idx)) & 0xFFFFFF;
    b0 = (val >> 16) & 0xFF;
    b1 = (val >> 8) & 0xFF;
    b2 = val & 0xFF;
  }
  
  shift24(display_clk_, display_mosi_, display_cs_, b0, b1, b2);
  ESP_LOGI(TAG, "PATTERN[%d/%d] Phase %c: [%02X %02X %02X] = [%s]",
           step + 1, 48, phase_a ? 'A' : 'B', b0, b1, b2,
           format_binary(b0, b1, b2).c_str());
  
  step = (step + 1) % 48;
}
```

**Czas:** ~1 flash (~6 min) + 2 min obserwacji = ~8 min

### Eksperyment 2: GPIO5 jako OE — weryfikacja hipotezy C 🥈

**Cel:** Sprawdzić czy GPIO5 steruje OE (Output Enable) 74HC595.

**Metoda:**
- Ustawić piny display na (33,18,32)
- GPIO5 ustawić jako OUTPUT, stan LOW
- Uruchomić normalny display refresh
- Obserwować czy wyświetlacz zaczyna pokazywać poprawne znaki

**Warianty:**
- 2a: GPIO5 = LOW (OE active) + display na (33,18,32)
- 2b: GPIO5 = HIGH (OE inactive) + display na (33,18,32)
- 2c: GPIO5 = LOW + pattern test z Eksperymentu 1

**Czas:** ~6 min (jeden flash)

### Eksperyment 3: GPIO25 jako dodatkowy pin kontrolny 🥉

**Cel:** Sprawdzić czy GPIO25 steruje MR (Master Reset) lub inną funkcją.

**Metoda:**
- Ustawić piny display na (33,18,32)
- GPIO25 ustawić jako OUTPUT
- Testować stany HIGH/LOW
- Obserwować wpływ na wyświetlacz

**Czas:** ~6 min (można połączyć z Eksperymentem 2)

### Eksperyment 4: Wszystkie permutacje {25,32,33} z pattern testem

**Cel:** Na wypadek gdyby (33,18,32) był false positive.

**Metoda:**
- 6 permutacji {25,32,33} × pattern test (24 bity × 3s = 72s każda)
- Łącznie: 6 × 72s = ~7 min testu

**Uwaga:** GPIO18 NIE jest w tym zbiorze — jeśli (33,18,32) działał, to GPIO18 jest kluczowy.

**Czas:** ~6 min flash + 7 min testu = ~13 min

### Eksperyment 5: LSB-first bit order

**Cel:** Sprawdzić czy wyświetlacz oczekuje LSB-first zamiast MSB-first.

**Metoda:**
- Zmienić shift24 na LSB-first
- Użyć pinów (33,18,32)
- Wysłać znane znaki ("8888", "0000", "IdLE")
- Obserwować wynik

**Implementacja:**
```cpp
static void shift24_lsb(gpio_num_t clk, gpio_num_t mosi, gpio_num_t cs,
                        uint8_t b0, uint8_t b1, uint8_t b2) {
  gpio_set_level(cs, 0);
  delayMicroseconds(2);
  uint8_t bytes[] = {b0, b1, b2};
  for (int b = 0; b < 3; b++) {
    for (int i = 0; i < 8; i++) {  // LSB first!
      gpio_set_level(mosi, (bytes[b] >> i) & 1);
      delayMicroseconds(1);
      gpio_set_level(clk, 1);
      delayMicroseconds(1);
      gpio_set_level(clk, 0);
      delayMicroseconds(1);
    }
  }
  delayMicroseconds(2);
  gpio_set_level(cs, 1);
}
```

**Czas:** ~6 min

### Eksperyment 6: Permutacje kolejności bajtów

**Cel:** Sprawdzić czy bajty są w innej kolejności w kaskadzie 74HC595.

**Metoda:**
- Zamiast `[0x00, colon|digit, segments]` testować:
  - `[segments, colon|digit, 0x00]`
  - `[colon|digit, 0x00, segments]`
  - `[segments, 0x00, colon|digit]`
  - `[0x00, segments, colon|digit]`
  - `[colon|digit, segments, 0x00]`
- Użyć pinów (33,18,32)
- Wysłać znany znak np. "8" (0x7F) na pierwszej cyfrze

**Czas:** ~6 min (można zrobić w jednym flashu, przełączając co 10s)

### Eksperyment 7: Fizyczna weryfikacja ciągłości (mimo lakieru)

**Cel:** Ostateczne potwierdzenie ścieżek PCB.

**Metoda:**
- Delikatnie zarysować lakier na padach testowych cienką igłą
- Zmierzyć ciągłość multimetrem:
  - GPIO25 → R34 → TP29 → przelotka → 74HC595 pin 12 (ST_CP)?
  - GPIO33 → ? → 74HC595 pin 11 (SH_CP)?
  - GPIO18 → ? → 74HC595 pin 14 (DS)?
  - GPIO5 → R28 → U6 → ? (dokąd?)

**Uwaga:** Wymaga dostępu do płytki i multimetru. Ryzyko uszkodzenia lakieru.

**Czas:** ~30 min (ostrożne skrobanie + pomiary)

### Eksperyment 8: Logic Analyzer / Oscilloscope

**Cel:** Zmierzyć rzeczywiste sygnały na pinach display.

**Metoda:**
- Jeśli mamy dostęp do logic analyzera (Saleae itp.)
- Probe na GPIO33, GPIO18, GPIO32 podczas odświeżania display
- Zmierzyć: częstotliwość CLK, timing CS, kolejność bitów
- Porównać z oczekiwaniami shift24

**Uwaga:** Wymaga sprzętu (logic analyzer).

**Czas:** ~15 min konfiguracja + pomiar

### Eksperyment 9: Test 5-pinowy (CLK+DATA+LATCH+OE+MR) — NOWY 🥇

**Cel:** Sprawdzić czy wyświetlacz wymaga 5 pinów zamiast 3.

**Hipoteza:** GPIO5 = OE (Output Enable), GPIO25 = MR (Master Reset)

**Metoda:**
1. Ustawić piny display: CLK=33, DATA=18, LATCH=32
2. GPIO5 ustawić jako OUTPUT, stan LOW (OE active)
3. GPIO25 ustawić jako OUTPUT, stan HIGH (MR inactive)
4. Uruchomić pattern test (Eksperyment 1)
5. Obserwować wynik

**Warianty:**
- 9a: GPIO5=LOW, GPIO25=HIGH (OE active, MR inactive) — oczekiwane: działa
- 9b: GPIO5=LOW, GPIO25=LOW (OE active, MR active — reset) — oczekiwane: ciemno
- 9c: GPIO5=HIGH, GPIO25=HIGH (OE inactive, MR inactive) — oczekiwane: ciemno
- 9d: GPIO5=HIGH, GPIO25=LOW (OE inactive, MR active) — oczekiwane: ciemno

**Implementacja:**
```cpp
void setup_5pin_test() {
  // Podstawowe piny display
  gpio_set_direction((gpio_num_t)33, GPIO_MODE_OUTPUT);  // CLK
  gpio_set_direction((gpio_num_t)18, GPIO_MODE_OUTPUT);  // DATA
  gpio_set_direction((gpio_num_t)32, GPIO_MODE_OUTPUT);  // LATCH
  
  // Dodatkowe piny
  gpio_set_direction((gpio_num_t)5, GPIO_MODE_OUTPUT);   // OE?
  gpio_set_direction((gpio_num_t)25, GPIO_MODE_OUTPUT);  // MR?
  
  // Ustawienia
  gpio_set_level((gpio_num_t)5, 0);   // OE = LOW (active)
  gpio_set_level((gpio_num_t)25, 1);  // MR = HIGH (inactive)
}
```

**Czas:** ~6 min (jeden flash)

**Kluczowe pytanie:** Czy po ustawieniu GPIO5=LOW wyświetlacz zaczyna pokazywać poprawne wzorce z pattern testu?

---

## 5. Rekomendowana kolejność eksperymentów

Biorąc pod uwagę:
- Czas iteracji (~6 min na flash)
- Ryzyko watchdog (potrzeba pin_diag lub boot_delay)
- Wartość informacyjna każdego eksperymentu

**Faza 1: Test 5-pinowy (Eksperyment 9)** — NAJWAŻNIEJSZY, ROZSTRZYGAJĄCY
- Flash z pinami (33,18,32) + GPIO5=LOW + GPIO25=HIGH + pattern generator
- Jeśli wyświetlacz zaczyna pokazywać poprawne wzorce → hipoteza 5-pinowa potwierdzona
- Jeśli nadal "bzdury" → problem z formatem danych (bit order, byte order)
- Jeśli ciemno → GPIO5 nie jest OE, trzeba szukać dalej
- **Koszt:** 1 flash (~6 min) + 3 min obserwacji
- **Wartość informacyjna:** OGROMNA — rozstrzyga czy to 3 piny czy 5 pinów

**Faza 2: Pattern test bez GPIO5/25 (Eksperyment 1)**
- Tylko jeśli Eksperyment 9 nie dał jednoznacznych wyników
- Flash z pinami (33,18,32) + pattern generator (bez GPIO5/25)
- **Koszt:** 1 flash (~6 min) + 3 min obserwacji

**Faza 3: LSB-first + byte order (Eksperyment 5 + 6)**
- Jeśli pattern test potwierdził piny ale znaki są przekłamane
- **Koszt:** 1-2 flashe (~6-12 min)

**Faza 4: Permutacje {25,32,33} (Eksperyment 4)**
- Tylko jeśli (33,18,32) kompletnie nie daje wyników
- **Koszt:** 1 flash + 7 min testu (~13 min)

**Faza 5: Fizyczna weryfikacja (Eksperyment 7)**
- Ostateczność, jeśli wszystkie testy elektryczne zawiodą
- **Koszt:** ~30 min + ryzyko uszkodzenia

---

## 6. Proponowana implementacja — tryb `lcd_patgen` z obsługą 5 pinów

Proponuję dodać nowy tryb diagnostyczny do komponentu: `lcd_patgen: true`.

**Zachowanie:**
1. Setup: piny display na (33,18,32) lub z konfiguracji
2. Opcjonalnie: GPIO5 (OE) i GPIO25 (MR) jeśli skonfigurowane
3. Wyłączyć normalny display refresh
4. Co 3 sekundy wysłać kolejny wzorzec z logowaniem
5. 48 kroków (24 bity × 2 fazy: common cathode + common anode)
6. Po 48 krokach → powtórzyć w pętli (lub przejść do normalnego trybu)

**Nowe opcje YAML:**
```yaml
snk_mower:
  display_clk: 33
  display_mosi: 18
  display_cs: 32
  display_oe: 5      # Output Enable (optional, active LOW)
  display_mr: 25     # Master Reset (optional, active LOW)
  lcd_patgen: true
```

**Logi:**
```
[PATGEN 01/48] Phase A: [80 00 00] = [10000000 00000000 00000000] → bit 15
[PATGEN 02/48] Phase A: [40 00 00] = [01000000 00000000 00000000] → bit 14
...
[PATGEN 25/48] Phase B: [7F FF FF] = [01111111 11111111 11111111] → bit 15 LOW
...
```

**Opcjonalnie:**
- Jednoczesne ustawienie GPIO5 LOW (test hipotezy OE)
- Jednoczesne ustawienie GPIO25 HIGH/LOW (test MR)

---

## 7. Hipoteza: 5-7 pinów zamiast 3-pinowego SPI

### 7.1. Dlaczego 3 piny mogą nie wystarczyć?

Obserwacje sugerują że konfiguracja 3-pinowa (CLK+MOSI+CS) nie wyjaśnia wszystkiego:

1. **GPIO5 jest w 18 funkcjach IROM** — to zbyt dużo jak na zwykły CLK. OE (Output Enable) jest ustawiane częściej (przed każdą operacją display).
2. **GPIO5 → R28 → U6** — idzie do innego układu niż U1/U3/U4. Jeśli U6 to 74HC595 sterujący OE/MR głównych rejestrów, to GPIO5 nie jest CLK ale OE.
3. **"88:88" przy 0xFF,0xFF,0xFF** — nie wszystkie segmenty świecą. Może część bitów steruje OE/MR, nie segmentami.
4. **FW decompilation: GPIO5+GPIO32+GPIO25** — 3 piny, ale może są to: CLK+DATA+LATCH+OE+MR (5 pinów)?

### 7.2. Możliwe konfiguracje 5-7 pinów

#### Konfiguracja A: 3-pinowe SPI + OE + MR (5 pinów) — NAJBARDZIEJ PRAWDOPODOBNA

```
ESP32                74HC595 #1        74HC595 #2        74HC595 #3
GPIO33 (CLK)  ────── SH_CP ─────────── SH_CP ─────────── SH_CP
GPIO18 (DATA) ────── DS ────────────── Q7' ───────────── Q7'
GPIO32 (LATCH) ───── ST_CP ─────────── ST_CP ─────────── ST_CP
GPIO5 (OE) ───────── OE ────────────── OE ────────────── OE
GPIO25 (MR) ──────── MR ────────────── MR ────────────── MR
```

**Dlaczego to może działać:**
- GPIO5 jako OE wyjaśnia 18 funkcji w FW (OE jest ustawiane przed każdą operacją)
- GPIO25 jako MR wyjaśnia 1 funkcję z GPIO5 (0x401908b4) — inicjalizacja OE+MR
- Bez OE=LOW wyjścia są w stanie wysokim (high-Z) — wyświetlacz ciemny
- Bez MR=HIGH rejestry się resetują — wyświetlacz ciemny lub losowy

**Test:** Ustawić GPIO5=LOW, GPIO25=HIGH + piny display (33,18,32) + pattern test

#### Konfiguracja B: Wspólny CLK + osobne DATA (5 pinów)

```
ESP32                74HC595 #1        74HC595 #2        74HC595 #3
GPIO33 (CLK)  ────── SH_CP ─────────── SH_CP ─────────── SH_CP
GPIO18 (DATA1) ───── DS                Q7' ───────────── Q7'
GPIO? (DATA2) ────── Q7' ───────────── DS                Q7'
GPIO? (DATA3) ────── Q7' ───────────── Q7' ───────────── DS
GPIO32 (LATCH) ───── ST_CP ─────────── ST_CP ─────────── ST_CP
```

**Problem:** Wymaga 5 pinów, ale mamy tylko 3 pewne (33,18,32). Gdzie są DATA2 i DATA3?

#### Konfiguracja C: Osobne LATCH dla każdego rejestru (7 pinów)

```
ESP32                74HC595 #1        74HC595 #2        74HC595 #3
GPIO33 (CLK)  ────── SH_CP ─────────── SH_CP ─────────── SH_CP
GPIO18 (DATA) ────── DS ────────────── Q7' ───────────── Q7'
GPIO32 (LATCH1) ──── ST_CP
GPIO? (LATCH2) ─────────────────────── ST_CP
GPIO? (LATCH3) ─────────────────────────────────────────── ST_CP
```

**Problem:** Wymaga 7 pinów, ale mamy tylko 3 pewne. Gdzie są LATCH2 i LATCH3?

### 7.3. Kandydaci na dodatkowe piny

Z analizy PCB i FW:

| GPIO | Dowody | Możliwa funkcja |
|------|--------|-----------------|
| **GPIO5** | 18 funkcji IROM, R28→U6 | **OE** (Output Enable) |
| **GPIO25** | 1 funkcja z GPIO5, R34→TP29 | **MR** (Master Reset) lub LATCH |
| **GPIO34** | Input-only, R26→chipy | ❌ Nie może być output |
| **GPIO39** | Input-only (SENSOR_VN) | ❌ Nie może być output |
| **GPIO21** | C10→znika | Nieznane, możliwe |
| **GPIO22** | Nieznane | Nieznane, możliwe |
| **GPIO23** | HARDWARE.md: MOSI | Możliwe (ale nie działało) |
| **GPIO27** | Buzzer | ❌ Zajęte |

### 7.4. Eksperyment: Test 5-pinowy

**Cel:** Sprawdzić czy wyświetlacz wymaga 5 pinów (CLK+DATA+LATCH+OE+MR).

**Metoda:**
1. Ustawić piny display: CLK=33, DATA=18, LATCH=32
2. GPIO5 ustawić jako OUTPUT, stan LOW (OE active)
3. GPIO25 ustawić jako OUTPUT, stan HIGH (MR inactive)
4. Uruchomić pattern test (Eksperyment 1)
5. Obserwować wynik

**Warianty:**
- 5a: GPIO5=LOW, GPIO25=HIGH (OE active, MR inactive)
- 5b: GPIO5=LOW, GPIO25=LOW (OE active, MR active — reset)
- 5c: GPIO5=HIGH, GPIO25=HIGH (OE inactive, MR inactive — wszystko wyłączone)
- 5d: GPIO5=HIGH, GPIO25=LOW (OE inactive, MR active)

**Oczekiwany wynik:**
- Jeśli 5a działa → konfiguracja A potwierdzona (OE+MR)
- Jeśli żaden nie działa → może inna konfiguracja (B lub C)
- Jeśli 5b/5c/5d dają inne wyniki → możemy zmapować funkcje

**Implementacja:**
```cpp
void setup_5pin_test() {
  // Podstawowe piny display
  gpio_set_direction((gpio_num_t)33, GPIO_MODE_OUTPUT);  // CLK
  gpio_set_direction((gpio_num_t)18, GPIO_MODE_OUTPUT);  // DATA
  gpio_set_direction((gpio_num_t)32, GPIO_MODE_OUTPUT);  // LATCH
  
  // Dodatkowe piny
  gpio_set_direction((gpio_num_t)5, GPIO_MODE_OUTPUT);   // OE?
  gpio_set_direction((gpio_num_t)25, GPIO_MODE_OUTPUT);  // MR?
  
  // Ustawienia
  gpio_set_level((gpio_num_t)5, 0);   // OE = LOW (active)
  gpio_set_level((gpio_num_t)25, 1);  // MR = HIGH (inactive)
}
```

**Czas:** ~6 min (jeden flash)

### 7.5. Dlaczego FW ma GPIO5 w 18 funkcjach?

Jeśli GPIO5 to OE (Output Enable), to:
- Przed każdą operacją display FW ustawia OE=LOW (enable outputs)
- Po operacji FW może ustawiać OE=HIGH (disable outputs, oszczędzanie energii)
- To wyjaśnia 18 funkcji — każda funkcja display ustawia OE

Jeśli GPIO5 to CLK, to:
- 18 funkcji z GPIO5 to zbyt dużo jak na zwykły clock
- CLK jest używany tylko podczas shift24 (1 funkcja)
- Chyba że FW używa innego protokołu (np. osobne CLK dla każdego 74HC595?)

### 7.6. Podsumowanie hipotezy 5-7 pinów

**Najbardziej prawdopodobna konfiguracja:**
- GPIO33 = CLK (SH_CP)
- GPIO18 = DATA (DS)
- GPIO32 = LATCH (ST_CP)
- GPIO5 = OE (Output Enable, active LOW)
- GPIO25 = MR (Master Reset, active LOW)

**Test rozstrzygający:** Ustawić GPIO5=LOW, GPIO25=HIGH + piny (33,18,32) + pattern test. Jeśli wyświetlacz zaczyna pokazywać poprawne wzorce → hipoteza potwierdzona.

### 8.1. Dlaczego "88:88" a nie wszystkie segmenty?

Jeśli 0xFF,0xFF,0xFF dało "88:88" (a nie wszystkie możliwe segmenty włącznie z niewidocznymi), to może oznaczać że:
- Nie wszystkie 24 bity sterują segmentami
- Niektóre bity 74HC595 mogą nie być podłączone (NC)
- Albo niektóre bity sterują czymś innym (np. enable driver)

### 8.2. LCD ma 13 pinów (6 dół + 7 góra)

GD5643CPG-1 ma 13 pinów:
- 4 cyfry × 7 segmentów = 28 segmentów + 4 cyfry (common) = 32 połączenia
- Ale z multipleksowaniem: 7 segmentów + 4 cyfry = 11 pinów minimum
- 13 pinów sugeruje: 7 segmentów (a-g) + 4 cyfry (D0-D3) + colon (1 pin) + może DP = 13

### 8.3. Trzy 74HC595 = 24 bity

24 bity:
- 8 bitów → segmenty (a-g + dp) jednej cyfry
- 4 bity → wybór cyfry (D0-D3)
- 2 bity → colon
- Razem: 14 bitów użytecznych z 24

Gdzie pozostałe 10 bitów?
- Mogą być nieużywane (74HC595 ma 8 wyjść, ale nie wszystkie podłączone)
- Albo 3× 74HC595 = 24 bity, ale tylko część steruje wyświetlaczem
- Albo dodatkowy 74HC595 (U6?) steruje czymś innym

### 8.4. U6 — czwarty 74HC595?

report-ds-1836.md wspomina: "GPIO5 → R28 → U6 (74HC595)". Ale HARDWARE.md wymienia tylko U1, U3, U4 jako 74HC595.

Możliwości:
1. U6 to inny układ (nie 74HC595), błędnie zidentyfikowany
2. U6 to czwarty 74HC595, nieudokumentowany w HARDWARE.md
3. U6 to jeden z U1/U3/U4, błędnie oznaczony

Jeśli U6 to dodatkowy 74HC595, to może sterować czymś innym (np. przyciskami, buzzerem, albo OE/MR głównych 74HC595).

---

## 9. Podsumowanie i rekomendacja

### Najbardziej prawdopodobny scenariusz (zaktualizowany)

1. **Piny display: CLK=GPIO33, DATA=GPIO18, LATCH=GPIO32** — potwierdzone emp. (jedyna kombinacja, która zapaliła wyświetlacz)
2. **GPIO5 = OE (Output Enable)** — silna hipoteza, wyjaśnia 18 funkcji w FW, wymaga weryfikacji
3. **GPIO25 = MR (Master Reset)** — silna hipoteza, wyjaśnia 1 funkcję z GPIO5, wymaga weryfikacji
4. **Format danych może być poprawny** — jeśli OE/MR są nieaktywne, wyświetlacz pokazuje "bzdury"

### Natychmiastowy plan działania

1. **Zaimplementować `lcd_patgen` z obsługą 5 pinów** — pattern generator w C++
2. **Flash z pinami (33,18,32) + GPIO5=LOW + GPIO25=HIGH + lcd_patgen: true**
3. **Nagrać telefonem co się świeci przy każdym kroku**
4. **Jeśli wzorce są poprawne** → zmapować bity, zaktualizować `char_to_segments_` i `refresh_display`
5. **Jeśli nadal "bzdury"** → problem z bit order / byte order / segment mapping
6. **Jeśli ciemno** → GPIO5 nie jest OE, szukać dalej

### Co można zrobić BEZ flashowania

1. **Fizyczna inspekcja PCB** — zarysować lakier na TP29, zmierzyć ciągłość GPIO25→ST_CP
2. **Zdjęcia PCB pod mikroskopem** — śledzić ścieżki z GPIO33/18/32
3. **Analiza termiczna** — czy któryś pin 74HC595 się grzeje (oznacza że jest aktywny)
4. **Zmierzyć stan GPIO5 i GPIO25** — czy są pull-up/down, czy floating

---

## 10. Otwarte pytania

1. **Czy GPIO5 to OE?** → Eksperyment 9 (test 5-pinowy)
2. **Czy GPIO25 to MR?** → Eksperyment 9 (test 5-pinowy)
3. **Czy to 3 piny czy 5 pinów?** → Eksperyment 9 rozstrzyga
4. Jaki jest bit order (MSB vs LSB)? → Eksperyment 1 (pattern test)
5. Jaki jest byte order w kaskadzie 74HC595? → Eksperyment 6
6. Jaki jest segment mapping (który bit = który segment)? → Eksperyment 1
7. Czy wyświetlacz jest common cathode czy common anode? → Eksperyment 1 (fazy A i B)
8. Co to jest U6? → Fizyczna inspekcja lub Eksperyment 9 (GPIO5 jako OE)
9. Czy DROM SPI config (12/10) jest dla innego peryferalu? → Prawdopodobnie tak
10. **Czy 74HC595 mają osobne OE/MR czy wspólne?** → Eksperyment 9 + fizyczna inspekcja

---

## 11. Załączniki

### A. Tabela prawdy 74HC595

| Pin | Nazwa | Funkcja |
|:---:|:-----:|---------|
| 1 | Q1 | Output 1 |
| 2 | Q2 | Output 2 |
| ... | ... | ... |
| 8 | Q7 | Output 7 |
| 9 | Q7' | Serial output (cascade to next 595) |
| 10 | MR | Master Reset (active LOW) |
| 11 | SH_CP | Shift Register Clock (rising edge shifts data) |
| 12 | ST_CP | Storage Register Clock (rising edge latches data) |
| 13 | OE | Output Enable (active LOW, HIGH = high-Z outputs) |
| 14 | DS | Serial Data Input |
| 15 | Q0 | Output 0 |
| 16 | VCC | Power |

### B. Kaskada 3× 74HC595 — konfiguracja 3-pinowa (obecna hipoteza)

```
ESP32                74HC595 #1        74HC595 #2        74HC595 #3
GPIO33 (CLK)  ────── SH_CP ─────────── SH_CP ─────────── SH_CP
GPIO18 (MOSI) ────── DS ────────────── Q7' ───────────── Q7'
GPIO32 (CS)   ────── ST_CP ─────────── ST_CP ─────────── ST_CP
                     MR (HIGH)         MR (HIGH)         MR (HIGH)
                     OE (LOW)          OE (LOW)          OE (LOW)
                     
                     Q0-Q7 →           Q0-Q7 →           Q0-Q7 →
                     segmenty?         cyfry?            colon?
```

### C. Kaskada 3× 74HC595 — konfiguracja 5-pinowa (NOWA hipoteza)

```
ESP32                74HC595 #1        74HC595 #2        74HC595 #3
GPIO33 (CLK)  ────── SH_CP ─────────── SH_CP ─────────── SH_CP
GPIO18 (DATA) ────── DS ────────────── Q7' ───────────── Q7'
GPIO32 (LATCH) ───── ST_CP ─────────── ST_CP ─────────── ST_CP
GPIO5 (OE) ───────── OE ────────────── OE ────────────── OE
GPIO25 (MR) ──────── MR ────────────── MR ────────────── MR

                     Q0-Q7 →           Q0-Q7 →           Q0-Q7 →
                     segmenty?         cyfry?            colon?
```

**Kluczowa różnica:** OE i MR są sterowane przez ESP32, nie są na stałe podciągnięte.

### D. Aktualna konfiguracja YAML

```yaml
snk_mower:
  display_clk: 5
  display_mosi: 32
  display_cs: 25
  lcd_find: true
```

**Proponowana konfiguracja do testów (3-pinowa):**
```yaml
snk_mower:
  display_clk: 33
  display_mosi: 18
  display_cs: 32
  lcd_patgen: true
```

**Proponowana konfiguracja do testów (5-pinowa):**
```yaml
snk_mower:
  display_clk: 33
  display_mosi: 18
  display_cs: 32
  display_oe: 5      # Output Enable (optional, active LOW)
  display_mr: 25     # Master Reset (optional, active LOW)
  lcd_patgen: true
```
