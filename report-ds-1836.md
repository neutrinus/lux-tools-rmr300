# Report DS-1836: LCD Display Control Reverse Engineering

## Status obecny

### Hardware
- **LCD:** GD5643CPG-1 — 4-digit 7-segment LED + colon, 13 pinów (6 dół + 7 góra)
- **Driver:** 3× 74HC595 (SOP-16, U1/U3/U4) — 24 bity shift register
- **PCB:** `SNK_DISPLAY_CP_V11`, zalakierowana (brak dostępu miernikiem)
- **MCU:** ESP32-WROOM-32UE

### Sterowanie (wg kodu)
- Bit-bang przez 3 GPIO: CLK, MOSI, CS (latch/ST_CP)
- `shift24(clk, mosi, cs, b0, b1, b2)` — MSB-first, 24 bity, CS active-low
- Multipleksowanie: 4 digit × 4ms = 16ms pełny cykl
- 3 bajty: `[0x00, colon|digit_select, segments]`
  - bajt0 = zawsze 0x00 (niewykorzystany? trzeci 74HC595?)
  - bajt1 = colon (bity 4-5) + digit select (bity 0-3)
  - bajt2 = segment bits (a-g + dp)

### Trzy konkurencyjne teorie pinów

| Teoria | CLK | MOSI | CS | Dowody |
|--------|:---:|:----:|:--:|--------|
| **A: FW (dekompilacja)** | **5** ✅ 18 func. IROM | **32** ✅ 4 func. IROM | **25** ⚠️ 1 func. z GPIO5 | GPIO5+25 w func. 0x401908b4; testy emp. — **nigdy nie zapalił** |
| **B: Empiryczna #1** | **5** | **32** | **34** ❌ | Combo #39/343 z 210 permutacji — false positive, GPIO34 input-only |
| **C: Empiryczna #2** ⭐ | **33** | **18** | **32** | Jedyna kombinacja, która **zapaliła wyświetlacz** (combo #4/6 z {18,33,32}) |

### Status weryfikacji
- Teoria A (FW) testowana wielokrotnie — **zero reakcji** wyświetlacza
- Teoria C (empiryczna) — **wyświetlacz zapalił się** ale pokazywał bzdury
- Segment map (`char_to_segments_`) — standardowy, niezweryfikowany dla tego LCD
- Bit order — MSB-first (niezweryfikowany)
- `LCD_COMBOS` w kodzie = 6 permutacji {5,25,32} — testuje tylko teorię A

### Co jest pewne
- Wyświetlacz jest bit-bangany (nie sprzętowe SPI) — GPIO
- 24 bity na ramkę (3× 74HC595)
- Multipleksowany (4 digit, refresh ~60Hz)
- GPIO18 i GPIO33 są output-capable i zaangażowane (wg testu C)

### Co jest niepewne
- Które piny są CLK/MOSI/CS — 3 sprzeczne teorie, żadna nie działa w pełni
- Czy to na pewno 3-pinowa kaskada (CLK+DATA+LATCH) czy każdy 74HC595 ma osobną linię danych (wymaga 5+ pinów: wspólny CLK, wspólny LATCH, 3× DATA, albo wspólny CLK, 3× DATA, 3× LATCH)
- Który bit odpowiada któremu segmentowi (a-g, dp)
- Który bit odpowiada której cyfrze (D0-D3)
- MSB-first vs LSB-first
- Kolejność bajtów w łańcuchu 74HC595
- Czy wszystkie 3 74HC595 są faktycznie użyte (może tylko 2 z 3?)

### Kluczowa nierozwiązana kwestia
GPIO5 (CLK wg FW) idzie przez R28 do U6 (też 74HC595), **NIE do U1/U3/U4 przy wyświetlaczu**. To może tłumaczyć czemu FW combo (5,32,25) nie działa — GPIO5 steruje innym 74HC595 na płytce, nie tym od LCD.

## Możliwe dalsze kroki

### 📊 Krok 1: Structured pattern test z (33,18,32) — najważniejszy
Skoro combo C (33,18,32) JEDYNE zapaliło ekran, to trzeba go rozwijać. Potrzebny test, który wyśle znane wzorce i pozwoli odczytać, który bit steruje czym.

**Proponowany test `lcd_patgen` (pattern generator):**
- Wyłączyć normalny display refresh
- Ręcznie wysyłać sekwencje przez `shift24` z debug logiem
- Test 1: każdy bit osobno w każdej z 3 pozycji bajtów
  - Np. `0x01,0x00,0x00`, `0x02,0x00,0x00`, ... `0x80,0x00,0x00`
  - Potem `0x00,0x01,0x00`, ... itd.
- Test 2: każdy digit osobno (bity 0-3 w bajcie 1)
- Test 3: colon bit (bity 4-5 w bajcie 1)
- Test 4: znane wzorce znaków (8, 0, 1) — porównać z oczekiwanymi segmentami

### 🔄 Krok 2: Swap byte order z (33,18,32)
Możliwe że bajty są w innej kolejności (74HC595 cascade order). Wypróbować:
```cpp
// Zamiast 0x00, colon|dig, segments
// Spróbować: segments, colon|dig, 0x00
// lub: colon|dig, 0x00, segments
// lub: segments, 0x00, colon|dig
```

### 🔄 Krok 3: LSB-first bit order z (33,18,32)
```cpp
// Zamiast for i = 7..0
for (int i = 0; i < 8; i++) {
  gpio_set_level(mosi, (bytes[b] >> i) & 1);
  ...
}
```

### 🔄 Krok 4: Przetestować (33,18,32) z różnymi segment mapami
Możliwe że segmenty są inaczej mapowane niż standardowe (a=bit0, b=bit1, ... dp=bit7). Wykonać test bitów (krok 1) i na podstawie wyników zrekonstruować mapę.

### 🔄 Krok 5: Powtórzyć lcd_find na {25,32,33}
Ponieważ ręczne śledzenie PCB wskazuje GPIO25, GPIO33, GPIO32 jako podejrzane:

| # | CLK | CS | MOSI |
|:-:|:---:|:--:|:----:|
| 0 | 25 | 32 | 33 |
| 1 | 25 | 33 | 32 |
| 2 | 32 | 25 | 33 |
| 3 | 32 | 33 | 25 |
| 4 | 33 | 25 | 32 |
| 5 | 33 | 32 | 25 |

Ale to już było testowane (częściowo) w 210 permutacjach. GPIO25 i GPIO33 były testowane jako CS w 60 permutacjach z CLK=5, MOSI=32 — bez rezultatu.

### 🔄 Krok 6: Konfiguracja >3 pinów (osobne DATA dla każdego 74HC595)
Zamiast zakładać kaskadę, przetestować:
- Wspólny CLK, wspólny LATCH, 3× DATA (5 pinów)
- Osobne LATCH dla segmentów/digitów/colona
- Kombinacje: CLK + 3× DATA + LATCH (lub 3× LATCH)

Zbiór pinów do testowania: {5, 18, 25, 32, 33, 21, 22, 23, 27}

### 🔧 Krok 7: pin_diag podczas normalnej pracy
Uruchomić `pin_diag: true` podczas gdy kombajn normalnie pracuje (handshake OK). Obserwować które GPIO zmieniają stan. Jeśli wyświetlacz jest odświeżany przez ORYGINALNY firmware — zobaczymy aktywność.

Uwaga: jeśli oryginalny firmware został zastąpiony ESPHome, piny wyświetlacza są martwe — nie ma odświeżania. To jest problem — nie mamy jak podejrzeć działającego oryginalnego firmware'u na tym samym ESP32.

### 🔧 Krok 8: Analiza dalszej dekompilacji firmware ESP32
Głównie:
- Znaleźć funkcję bit-bang shift24 (może być inlined)
- Zidentyfikować tablicę segment bitmap dla stringów "IdLE", "LoCK", "Mow ", "HoME", "ChAr"
- Określić bit order, byte order, segment mapping z kodu oryginalnego

### 🔧 Krok 9: Weryfikacja ciągłości mimo lakieru
Mimo że PCB jest polakierowane, można delikatnie zarysować lakier na padach testowych (TP29 itp.) cienką igłą, żeby zmierzyć ciągłość:
- ESP32 GPIO25 → R34 → TP29 → przelotka → 74HC595 pin 12 (ST_CP)
- ESP32 GPIO5 → R28 → U6 — to NIE idzie do wyświetlacza?

**Najważniejsze pytanie:** czy GPIO5 (CLK wg FW) faktycznie DOCHODZI do 74HC595 U1/U3/U4? Jeśli nie — teoria A odpada.

### 🧪 Krok 10: Eksperyment z wyłączonym watchdogiem MB
Użyć `pin_diag: true` (watchdog nie startuje) i ręcznie inicjalizować piny display w loop, bez angażowania mainboard w ogóle. Testować różne kombinacje pinów przez dłuższy czas (bez ryzyka utraty zasilania).

## Rekomendowana ścieżka

### Faza 1 (najszybsza, ~1h): Pattern test na (33,18,32)
Zmodyfikować `lcd_find` żeby zamiast 0xFF 0xFF 0xFF wysyłał po kolei:
1. Każdy z 8 bitów w każdej z 3 pozycji bajtów (24 testy × 4s = 96s)
2. Każdy digit osobno (4 testy × 4s = 16s)
3. Colon bit (1 test × 4s)

To powinno jednoznacznie określić:
- Który bajt steruje segmentami, który digitami, który jest wolny
- Który bit to który segment
- Który bit to która cyfra

### Faza 2 (~0.5h): Swap byte/LSB-first z (33,18,32)
Jeśli pattern test wykaże że komunikacja działa (coś się zapala we właściwych miejscach), ale znaki są przekłamane — spróbować LSB-first i różnych kolejności bajtów.

### Faza 3 (~1h): 6 permutacji {25,32,33}
Jeśli (33,18,32) kompletnie nie działa — przetestować 6 permutacji zbioru {25,32,33} z tym samym pattern testem.

### Faza 4 (~1h): Test >3 pinów
Jeśli żadna 3-pinowa konfiguracja nie działa — przetestować konfiguracje z 4-6 pinami (3× DATA + CLK + LATCH).

### Faza 5: Ostateczność
Jeśli nic nie daje rezultatu — wrócić do dekompilacji ESP32 (znaleźć shift24) lub zarysować lakier na padach testowych, żeby zmierzyć ciągłość.

## Podsumowanie

| Hipotetyczne piny | CLK | MOSI | CS | Szansa |
|:---|:---:|:----:|:--:|:------:|
| FW (dekompilacja) | 5 | 32 | 25 | ⭐⭐⭐ — silne dowody z kodu, ale 0 reakcji emp. |
| FW + R28 (U6, nie LCD?) | 5→U6 | 32→? | 25→? | ⭐⭐ — GPIO5 może nie iść do wyświetlacza |
| PCB trace + emp. | 33 | 18 | 32 | ⭐⭐ — jedyne co zapaliło ekran |
| PCB trace ręczny | 25 | 33 | 32 | ⭐⭐ — z manualnego śledzenia ścieżek |
| PCB trace ręczny | 33 | 32 | 25 | ⭐⭐ — j.w. |
| >3 pinów (osobne DATA) | CLK shared | 3× DATA | LATCH | ⭐ — warto sprawdzić jeśli 3-pin fail |

**Najbardziej obiecujące:** rozwinąć (33,18,32) przez structured pattern test — to da odpowiedź czy to właściwe piny i jaka jest mapa bitów. Jeśli tak, reszta to tylko dostosowanie `char_to_segments_` i byte orderu.
