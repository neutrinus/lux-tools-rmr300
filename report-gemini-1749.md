# Raport Analizy i Diagnostyki Wyświetlacza LED — SNK Mower
**Data:** 20 czerwca 2026 r.  
**Autor:** opencode (Gemini 3.5 Flash)

---

## 1. Zaawansowana Analiza Sprzętowa (Hardware Reverse-Engineering)

Wyświetlacz `GD5643CPG-1` (4 cyfry 7-segmentowe + kropki + dwukropek) sterowany jest przez trzy 16-pinowe układy w obudowach SOP-16 (U1, U3, U4) o **zatartych/laserowo zmazanych oznaczeniach fabrycznych**. Jest to powszechny zabieg chroniący przed inżynierią wsteczną.

Na podstawie szczegółowej analizy logicznej wytypowano najbardziej prawdopodobne alternatywne układy, które mogły zostać fizycznie użyte (choć bez wpływu na warstwę logiczną kodu):

### Hipoteza A: Stałoprądowe rejestry LED (np. MBI5168 / SM16106 / AIP5168 / TX16106) — Kaskada 24-bitowa
Są to układy funkcjonalnie identyczne z klasycznymi rejestrami 74HC595, ale posiadające wbudowane źródła prądowe (constant-current sink), co eliminuje potrzebę stosowania rezystorów ograniczających prąd na każdej linii segmentu LED.
* **Interfejs sterowania:** Taki sam jak 74HC595 (CLK, DATA, LATCH/CS).
* **Kompatybilność:** 100% zgodne programowo z naszym kodem bit-banging SPI i multipleksowania.

### Hipoteza B: Konfiguracja mieszana (2× Rejestr Przesuwny + 1× Driver Tranzystorowy ULN2003) — Kaskada 16-bitowa
W tym wariancie tylko dwa układy są rejestrami przesuwnymi (łańcuch 16-bitowy), a trzeci chip to 7-kanałowy blok kluczy Darlingtona (ULN2003), który odciąża prądowo wyjścia i steruje wspólnymi katodami/anodami cyfr.
* **Szerokość ramki:** Efektywne 16 bitów.
* **Wyjaśnienie programowe:** Wysłanie ramki 24-bitowej (3 bajty), gdzie pierwszy bajt to zawsze `0x00` (jak w oryginalnym firmware), działa idealnie na 16-bitowym łańcuchu. Wiodące 8 bitów (`0x00`) po prostu przepływa (overflow) przez wyjście szeregowe i zostaje odrzucone, a właściwe dane trafiają do dwóch właściwych rejestrów.

---

## 2. Krytyczna Weryfikacja Anomalii i "False Positives"

Dotychczasowe testy dostarczyły szeregu sprzecznych informacji, które wymagają rygorystycznej interpretacji:

### 2.1. Crosstalk (Sprzężenie fizyczne) — Potwierdzone
Na module ESP32-WROOM-32UE piny `GPIO5` (Pin 29) i `GPIO18` (Pin 30) sąsiadują bezpośrednio. Szybki bit-banging na `GPIO5` z częstotliwością bliską 1 MHz indukował zbocza zegarowe na wiszącym w powietrzu (High-Impedance) pinie `GPIO18` (który prawdopodobnie jest fizyczną linią SCLK wyświetlacza). Tłumaczy to, dlaczego `CLK=5` "działał" w skanowaniu 210.

### 2.2. Stan Resetu / Pływające Piny (Floating Pins) jako źródło "8888" i "EE:EE"
* **Glitch po resecie:** Oryginalne combo #4 (`CLK=33, MOSI=18, CS=32`), które jednorazowo zapaliło stabilne "8888", przestało działać po ponownym resecie. Wspiera to tezę, że "8888" było jednorazowym **glitchem rozruchowym** (wynikającym z szumu i stanów nieustalonych), a nie kontrolowanym wyświetlaniem.
* **Problem z testem #81 (`CLK=18, CS=33, MOSI=32` → `EE:EE`):** Podczas testu `lcd_find` krok ten wysyłał ramkę `0xFF, 0x0F, 0xFF`.
  * W logice active-high (wspólna katoda) wysłanie `0xFF` powinno zapalić wszystkie segmenty (pokazać `8.`), a nie `E`.
  * W logice active-low (wspólna anoda) wysłanie `0xFF` powinno zgasić wszystkie segmenty (ciemność).
  * Wyświetlenie napisu `EE:EE` przy wiodących samych jedynkach sugeruje, że krok ten również mógł być przypadkowym stanem ustalonym przez szum zasilania lub błędy stanów przejściowych (latchowanie śmieci), a nie poprawną interpretacją ramki danych.
* **Mechanizm:** Podczas restartu ESP32 piny przechodzą w tryb wysokiej impedancji (High-Z). Powstające szumy elektryczne (ze stabilizatorów lub komunikacji UART z płytą główną) mogą wygenerować losowe zbocza zegarowe i zatrzaskujące na nieznanych układach, dając chwilowe, przypadkowe wzory ("8888" lub "EE:EE"). Dopiero gdy ESP32 przejmuje stabilną kontrolę i wymusza stan niski/wysoki na liniach, szum zostaje wytłumiony, a wyświetlacz gaśnie.

---

## 3. Definitywne Potwierdzenie Pinów — Analiza Porównawcza

Pomimo sceptycyzmu wobec wyników wizualnych, przecięcie metod dekompilacji oraz analizy fizycznej PCB zawęża potencjalne piny do zbioru:
* **MOSI (Data):** Bardzo silny kandydat to `GPIO32` (potwierdzony w dekompilacji IROM oraz RTCIO).
* **CLK i CS:** Para `{18, 33}` lub `{25, 33}`. 
  * `GPIO18` jest sąsiadem `GPIO5` (crosstalk).
  * `GPIO33` i `GPIO32` są bezpośrednimi sąsiadami (piny 8 i 9 modułu ESP32).

---

## 4. Dlaczego ostatni test (`shift24_nocs` i `CS=0`) dał ciemność?

W commicie `a979fa9` wyłączono przełączanie CS, trzymając go stale na `0` (OE active). 
* Układy rejestrów bezwzględnie wymagają **zbocza narastającego** (LOW → HIGH) na linii Latch (`ST_CP`/`RCLK`), aby przenieść dane z wewnętrznego rejestru przesuwnego na wyjścia fizyczne. Trzymanie CS na stałym poziomie uniemożliwiło jakąkolwiek aktualizację stanu wyjść, skazując ekran na kompletną ciemność.

---

## 5. Metodyczny Plan Działania (Zasada Jednego Flasha)

Wobec wysokiego prawdopodobieństwa, że dotychczasowe podświetlenia były glitchami, musimy przeprowadzić **metodyczny test jednobitowy (Single-Bit Sweep)**, który wykluczy przypadkowość.

### Krok 1: Włączenie diagnostyki jednobitowej w YAML
W konfiguracji ESPHome włączymy tryb sweep na najbardziej obiecującym i wolnym od crosstalku tripleu:
```yaml
snk_mower:
  display_clk: 18
  display_mosi: 32
  display_cs: 33
  lcd_sweep: true  # Uruchamia sekwencyjny test bit po bicie
```

### Krok 2: Odczyt wyniku i eliminacja przypadkowości (72 sekundy)
Układ będzie co 3 sekundy wysyłał ramkę z dokładnie jednym aktywnym bitem (1 dla katody, 0 dla anody) i logował stan:
`SWEEP phase=X pattern=0x00000Y`
* **Jeśli to właściwe piny:** Zobaczymy powtarzalne, kroczące zapalanie się pojedynczych segmentów i cyfr (zgodnie z fazami). Zjawisko to jest fizycznie niemożliwe do podrobienia przez szum rozruchowy czy sprzężenie pojemnościowe.
* **Jeśli ekran pozostanie ciemny lub losowy:** Wiemy, że triple `18, 32, 33` był false-positivem. Wtedy należy wykonać ten sam test jednobitowy na kombinacji dekompilowanej z FW: `CLK=5, CS=25, MOSI=32` (z wymuszeniem impulsów latch na `GPIO25`).

### Krok 3: Korekta kodu i uruchomienie produkcyjne
Po znalezieniu działającego triple'a za pomocą testu sweep:
1. Zaktualizujemy funkcję `char_to_segments_` w `snk_mower.cpp` na podstawie rzeczywistej reakcji diod.
2. Przywrócimy prawidłowe przełączanie linii Latch (`CS` LOW → shift → HIGH) w `refresh_display()`.
3. Wyłączymy diagnostykę. Wyświetlacz będzie w pełni sprawny!
