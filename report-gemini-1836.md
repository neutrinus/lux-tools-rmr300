# Raport Analizy i Diagnostyki LCD (GD5643CPG-1) — SNK Mower
**Data:** 19 czerwca 2026 r.  
**Autor:** opencode (Gemini 3.5 Flash)

---

## 1. Analiza stanu faktycznego i hipotez sprzętowych

W wyniku analizy dokumentacji (`ha.md`, `1610-report.md`, `HARDWARE.md`), logów gita oraz fizycznych cech płytki `SNK_DISPLAY_CP_V11` postawiliśmy dwie kluczowe hipotezy:

### Hipoteza 1: Standardowe szeregowe sterowanie (3-5 pinów)
Wyświetlacz `GD5643CPG-1` jest sterowany przez 3 układy `74HC595`.
*   **Rozbieżność pętli skanującej (`lcd_find`):** Wcześniejszy pełny skan 210 kombinacji przestał działać, ponieważ w commitach po `88a26faa` zmieniono wysyłany bufor na `0xFF, 0xFF, 0xFF` (same jedynki). W wyświetlaczu multipleksowanym (Wspólna Anoda/Katoda) taki bufor fizycznie uniemożliwia przepływ prądu przez diody LED (brak różnicy potencjałów między cyfrą a segmentem) — stąd efekt "braku reakcji" przy testowaniu kombinacji `{5, 25, 32}` z analizy FW.
*   **Piny pomocnicze OE / MR:** Układy `74HC595` posiadają wejścia `OE` (Output Enable - pin 13) oraz `MR` (Master Reset - pin 10). Jeśli są one podłączone do ESP32 (podejrzane: `GPIO5`, `GPIO25`), to bez wymuszenia stanu niskiego na `OE` i wysokiego na `MR`, rejestry są zablokowane, a ekran pozostaje ciemny.

### Hipoteza 2: Niezależne sterowanie rejestrami lub Direct Bit-Banging (5-7 pinów)
*   **Niezależne Latch (CS):** Rejestry mogą współdzielić linie `CLK` i `MOSI`, ale mieć oddzielne linie zatrzasku `CS1`, `CS2`, `CS3` (wymaga 5 pinów).
*   **Direct Drive:** Bezpośrednie sterowanie multipleksowane z ESP32 wymagałoby 12-13 pinów. Ponieważ na płytce są fizycznie obecne 3 scalaki SOP-16, direct drive z ESP32 do wyświetlacza jest wykluczony — ESP32 musi komunikować się bezpośrednio z układami pośredniczącymi.

---

## 2. Metodyczny Plan Diagnostyczny ("Zasada Jednego Flasha")

Aby wyeliminować długie, 6-minutowe iteracje i ryzyko watchdog'a, wdrożymy **Automatyczny Diagnostyk Kroczącego Bitu (Automated Rolling Bit Diagnostics)** w naszym custom componentcie.

Zostanie on skompilowany i wgrany **tylko raz**. Po uruchomieniu, diagnostyk będzie co 3 sekundy wysyłał specjalnie spreparowane ramki i logował stan rejestrów do ESPHome:

1.  **Faza 1 (Test Wspólnej Anody):** Wysyłamy pojedynczy bit `1` (HIGH) krocząco od bitu 0 do 23, reszta `0` (LOW).
    *   *Oczekiwany efekt:* Jeśli wyświetlacz jest Wspólną Anodą, po natrafieniu na bit cyfry cała cyfra rozbłyśnie jako pełne "8.".
2.  **Faza 2 (Test Wspólnej Katody):** Wysyłamy pojedynczy bit `0` (LOW) krocząco od bitu 0 do 23, reszta `1` (HIGH).
    *   *Oczekiwany efekt:* Jeśli wyświetlacz jest Wspólną Katodą, po natrafieniu na bit cyfry cała cyfra rozbłyśnie jako pełne "8.".
3.  **Wymuszenie stanów na pinach pomocniczych:** Diagnostyk automatycznie ustawi piny `GPIO5` oraz `GPIO25` w stan niski (`0`), aby odblokować ewentualną linię `OE` (Output Enable) rejestrów.

---

## 3. Implementacja Diagnostics w C++

### Modyfikacja `snk_mower.cpp`:
Wprowadzimy funkcję `run_lcd_diagnostics()`, która co 3 sekundy wykonuje krok diagnostyczny:

```cpp
std::string SnkMower::format_binary(uint32_t val) {
  std::string s = "";
  for (int i = 23; i >= 0; i--) {
    s += ((val >> i) & 1) ? '1' : '0';
    if (i % 8 == 0 && i > 0) s += ' ';
  }
  return s;
}

void SnkMower::run_lcd_diagnostics() {
  uint32_t now = millis();
  if (now - last_lcd_scan_ms_ < 3000) return;
  last_lcd_scan_ms_ = now;

  int total_steps = 48; 
  int step = lcd_scan_idx_ % total_steps;
  
  uint32_t val = 0;
  bool is_phase_1 = (step < 24);
  int bit_idx = step % 24;

  // Aktywujemy piny pomocnicze jako LOW na wypadek, gdyby pełniły rolę OE
  if (display_clk_ != 5 && display_cs_ != 5 && display_mosi_ != 5) {
    gpio_set_direction((gpio_num_t)5, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)5, 0); 
  }
  if (display_clk_ != 25 && display_cs_ != 25 && display_mosi_ != 25) {
    gpio_set_direction((gpio_num_t)25, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)25, 0);
  }

  if (is_phase_1) {
    val = (1ULL << bit_idx);
    uint8_t b0 = (val >> 16) & 0xFF;
    uint8_t b1 = (val >> 8) & 0xFF;
    uint8_t b2 = val & 0xFF;
    shift24(display_clk_, display_mosi_, display_cs_, b0, b1, b2);
    
    ESP_LOGI(TAG, "=== [LCD DIAG] Krok %d/48 (Faza 1 - Wspólna Anoda) ===", step + 1);
    ESP_LOGI(TAG, "  BIT: %d (HIGH), reszta LOW | Rejestr: [ %s ]", bit_idx, format_binary(val).c_str());
    ESP_LOGI(TAG, "  -> JEŚLI CYFRA ROZBŁYŚNIE jako '8.', to BIT %d steruje tą CYFRĄ!", bit_idx);
  } else {
    val = ~(1ULL << bit_idx) & 0xFFFFFF;
    uint8_t b0 = (val >> 16) & 0xFF;
    uint8_t b1 = (val >> 8) & 0xFF;
    uint8_t b2 = val & 0xFF;
    shift24(display_clk_, display_mosi_, display_cs_, b0, b1, b2);
    
    ESP_LOGI(TAG, "=== [LCD DIAG] Krok %d/48 (Faza 2 - Wspólna Katoda) ===", step + 1);
    ESP_LOGI(TAG, "  BIT: %d (LOW), reszta HIGH | Rejestr: [ %s ]", bit_idx, format_binary(val).c_str());
    ESP_LOGI(TAG, "  -> JEŚLI CYFRA ROZBŁYŚNIE jako '8.', to BIT %d steruje tą CYFRĄ!", bit_idx);
  }

  lcd_scan_idx_++;
}
```

---

## 4. Instrukcja uruchomienia testu

Sflashuj kosiarkę z włączoną opcją `lcd_find: true` na pinach, które wcześniej dały reakcję (bardzo silny kandydat):

```yaml
snk_mower:
  id: my_mower
  uart_id: mower_uart
  pin: "9633"
  display_clk: 33
  display_mosi: 18
  display_cs: 32
  lcd_find: true  # Uruchamia diagnostykę
```

Uruchom podgląd logów w konsoli. Nagraj telefonem krótki filmik ekranu LCD i zapisz numery kroków/bitów, przy których zapalają się poszczególne cyfry. Ta metoda da nam pełne mapowanie w 2 minuty.
