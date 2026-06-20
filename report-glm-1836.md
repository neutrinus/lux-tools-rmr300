# Raport: Reverse-engineering sterowania LCD — stan i plan (report-glm-1836)

> Kontekst: migracja oryginalnego firmware ESP32 → ESPHome. Cel: odkryć jak sterować
> wyświetlaczem GD5643CPG-1 (4×7-seg + dwukropek). Nie potwierdzone definitywnie ani
> piny, ani protokół. Raport bazuje na analizie disasm firmware ESP32 + wniosków z ha.md.

## 1. Streszczenie (TL;DR)

- **Nie znamy pinów LCD na pewno.** Obie dotychczasowe hipotezy są słabe:
  - `CLK=5, MOSI=32, CS=25` (z raportu 1610, „analiza literal-pool") — **OBALONE**: to
    były fałszywe trafienia (przypadkowe bajty w kodzie, zero referencji L32R).
  - `CLK=33, CS=32, MOSI=18` (empiryczne lcd_find, świeciło „88:88") — **Niewiarygodne**:
    test all-1s zapala segmenty przez coupling/pływające piny (wcześniejszy „hit" #39 był
    na GPIO34 = input-only = niemożliwe jako CS = potwierdzony false positive).
- **OEM firmware NIE bit-banga GPIO.** `GPIO_OUT` (0x3FF44004/8/C) ma **zero** referencji
  w całym firmware. LCD sterowany jest przez **sprzętowy peripheral szeregowy
  0x3FF4_E0xx** (blok nieujęty w `soc.h`, prawdopodobnie vendorowy LCD/seg engine), z
  buforem danych 0x3FF4E130–0x3FF4E140. Dlatego „pętla shift24" nie istnieje w OEM.
- Dla portu ESPHome to **nie przeszkoda** — 74HC595 można bit-bangować, o ile znamy:
  piny (CLK/MOSI/CS→SH_CP/DS/ST_CP), bit order, byte order, układ 24-bit ramki.
- **Najlepszy zarys pinów (przecięcie metod sprzętowych): MOSI=GPIO32, CLK/CS = {25,33}**
  w jakiejś kolejności. Triple {25,32,33} nigdy nie był czysto testowany.
- **Najtańsza i pełna ścieżka: Ghidra-dekompilacja 6 funkcji display** (0 flashów). Ghidra
  już działa (używany dla U13). Daje piny + mapę segmentów + bit order bez dotykania HW.
- **Definitywna walidacja: oscyloskop/LA na oryginalnym firmware** (2 flash cycle).

## 2. Stan obecny — co jest pewne

| Fakt | Status | Źródło |
|------|--------|--------|
| LCD = GD5643CPG-1, 4 cyfry + kropki + dwukropek | ✅ | inspekcja |
| Sterownik = 3× 74HC595 kaskadowo (24-bit shift reg) | ✅ | 3 scalaki SOP-16 na PCB + HARDWARE.md (U1/U3/U4) |
| Pinout LCD 6+7=13 nóg = 8 seg + 4 commons + 1 colon | ✅ | struktura multipleksowana 4-cyfrowa |
| UART do MB (JSON 230400, CRC-8, handshake) | ✅ | ha.md — nie dotyczy LCD |
| Metoda bit-bang ESPHome (shift24) jest poprawna dla 74HC595 | ✅ | 74HC595 nie wymaga sprzętowego SPI |
| OE prawdopodobnie hardwired low (brak 4. GPIO/PWM) | ✅ medium | brak referencji LEDC/RMT/extra GPIO w ścieżce display |

## 3. Stan obecny — co było BŁĘDNE (korekta wniosków)

### 3.1 Raport 1610 (CLK=5, MOSI=32, CS=25) — obalony
Metoda „skanuj IROM za 4-bajtowe słowa = numer GPIO i grupuj po funkcjach" daje
**fałszywe trafienia**: te 4-bajtowe wartości to przypadkowe bajty w kodzie/instrukcjach,
a nie literały L32R. Weryfikacja: wyszukiwanie `l32r aN, <pool>` dla rzekomych pul GPIO:

| pula (addr) | wartość | # L32R refs w disasm |
|-------------|---------|:-----:|
| 0x40190D94 | 5 | **0** |
| 0x40190964 | 25 | **0** |
| 0x40198148 / 0x4019B618 / 0x40170234 / 0x4014CDC4 | 32 | **0** każde |

Funkcje oznaczone w 1610 jako „display" to w rzeczywistości **printf-formatery**
(0x40199138, 0x40197AFC, 0x40198C78 — parsowanie liczb, ~120-case switch format-string).
Tylko **0x401908B4** jest faktycznie display-owe (konfiguruje peripheral 0x3FF4E0xx).

### 3.2 „Empiryczne 88:88 na {33,32,18}" — niewiarygodne
Test lcd_find wysyła `0xFF,0xFF,0xFF` (wszystkie bity=1) i multipleksuje. Przy wszystkich
segmentach wszystkich cyfr zapalonych jednocześnie, display pokazuje „88:88"-ish **nawet
przez coupling** pomiędzy pobliskimi pinami / pływającymi wejściami 74HC595. Dowód, że
test jest niemiarodajny: combo #39 (CLK=5, CS=34, MOSI=32) „zapaliło" — ale GPIO34 jest
**input-only**, nie może być CS. Czyli all-1s świeci przez coupling, nie przez poprawne
sterowanie. Wniosek: lcd_find all-1s **nie dowodzi** mapowania pinów.

### 3.3 HARDWARE.md (CLK=18, MOSI=23, CS=5) — nie działa
Testowane w normal mode — display ciemny. Ścieżka wizualna prawdopodobnie pomyliła
SH_CP (shift clock) ze ST_CP (latch clock).

## 4. Prawdziwa ścieżka kodu display OEM (nowe, z disasm)

Call graph (potwierdzone `call8`/`callx8`):

```
0x401943BC  display task (top-level)
  → 0x4019153C  INIT     (callx8 0x401B3D00, 0x401B2D34 — peripheral + GPIO setup)
  → 0x40191490  UPDATE   (każdy refresh)
       ├─ 0x40190FB4  RENDER    13-case switch: znak → bajty segment-bit (per cyfra)
       ├─ 0x401908B4  CONFIG    zapis GPIO# do 0x3FF4E0C4/C8/CC (pola 5-bit) + maski per-bit
       ├─ memcpy → bufor ramki BSS 0x3FFCD3B8
       └─ 0x401913E0  SERIALIZE 24-bit (a5=23) przez runtime tabelę reorder @0x3FFCD3CC
              └─ 0x40190E38  WRITE    3 bajty ramki → 0x3FF4E130..0x3FF4E140
```

Potwierdzone literały L32R (twarde dowody):

| Literal addr | Wartość | Znaczenie |
|--------------|---------|-----------|
| 0x40190520 | 0x3FF4E0C4 | reg selektu GPIO peripheral (5-bit pole, bits 10–14) |
| 0x40190524 | 0x3FF4E0C0 | reg config peripheral |
| 0x40190E24..34 | 0x3FF4E130..140 | **bufor danych** (5×4B) |
| 0x401913DC | 0x3FFCD3B8 | **bufor ramki (BSS, runtime-built)** |
| 0x401913D8 | 0x3FFCD3CC | **tabela bit-reorder (BSS, runtime-built)** |
| 0x40190150 | 0x3FFC8498 | struktura kontekstu display (BSS) |
| 0x40190848 | 0xFFFF83FF | maska czyszcząca bits 10–14 z 0x3FF4E0C4 |

Czego brakuje i gdzie jest:

| Nieznane | Gdzie w firmware | Jak odzyskać |
|----------|------------------|--------------|
| **Piny (CLK/MOSI/CS)** | 5-bit pola w 0x3FF4E0C4/C8/CC, zapisywane przez 0x401908B4 + 0x401915CC | dekompilacja tych funkcji |
| **Bit/byte order** | runtime tabela reorder @0x3FFCD3CC (BSS, nie w pliku flash) | dekompilacja funkcji wypełniającej tabelę LUB dump runtime (JTAG — niedostępny) |
| **mapa znak→segment** | 13-case switch w 0x40190FB4 (brak klasycznej tabeli 0x3F/0x06/... w DROM — nie znaleziono) | dekompilacja 0x40190FB4 |
| **OE/MR/jasność** | brak (żadne LEDC/RMT/4. GPIO w ścieżce) | — (OE hardwired low) |

Anomalie do weryfikacji (nie blokują portu ESPHome):
- `0x60033D38 |= 0x0C800000` na końcu 0x40191490 — adres spoza standardowej mapy
  peripheral ESP32 (0x3FF0xxxx–0x3FF6xxxx). Prawdopodobnie strobe „update" lub
  DPORT-alias. Do rozważenia przy debugu, nieistotne dla bit-bang portu.
- Peripheral 0x3FF4_E0xx nie zidentyfikowany (gap w soc.h między UHCI1 a I2S0).
  Nie potrzebujemy go identyfikować — wystarczy skopiować piny + układ ramki.

## 5. Hipotezy pinów — re-ranking

| Źródło | CLK | MOSI | CS | Wiarygodność |
|--------|:---:|:---:|:---:|--------------|
| 1610 (skan literal-pool) | 5 | 32 | 25 | ❌ obalone (fałszywe trafienia) |
| HARDWARE.md (ścieżka wizualna) | 18 | 23 | 5 | ❌ nie świeci |
| lcd_find empiryczne („88:88") | 33 | 18 | 32 | ⚠️ coupling (all-1s) |
| Ręczna inspekcja PCB (user) | {25,32,33} (+34/35 input-only) | | | ⚠️ 34/35 odrzucone |
| **Przecięcie metod sprzętowych** | **25 lub 33** | **32** | **25 lub 33** | **najlepszy zarys** |

- **GPIO32 = MOSI** — jedyne zgodne ze WSZYSTKIMI metodami sprzętowymi (ręczna inspekcja
  + empirycznie + pojawia się w RTCIO config → GPIO32 to RTC GPIO, spójne z 24 ref RTCIO).
- **CLK/CS = {25, 33}** w jakiejś kolejności (przecięcie ręcznej inspekcji i empirycznego).
- Triple **{25,32,33} nie był czysto testowany** (210-perm scan używał niewiarygodnego
  all-1s). Zatem najsilniejszy kandydat pozostaje niezweryfikowany.

## 6. Jakie dane można jeszcze zebrać + eksperymenty (wszystkie, z rangowaniem)

### Path A — Dekompilacja Ghidrą 6 funkcji display ★ (0 flashów, TOP PICK)
Ghidra 12.1.2 już działa (używany dla U13 — patrz `u13/decomp/decompilation.md`).
Import `esp32/firmware/ota_0.elf` jako Xtensa, analiza, dekompiluj:
- **0x401908B4 + 0x401915CC** → **3 numery GPIO** (zapis do 0x3FF4E0C4/C8/CC) + maski
  per-bit układu ramki
- **0x40190FB4** → mapa znak→segment-bit (13 case'ów = odpowiednik tabeli 7-seg)
- **0x401913E0 + funkcja wypełniająca 0x3FFCD3CC** → bit/byte order
- **0x40190E38** → potwierdzenie rozmieszczenia bajtów
- **0x4019153C** → sekwencja init (timing OEM)

**Daje:** piny + pełny układ ramki + bit order + mapa znaków. **Koszt:** kilka godzin
dekompilacji, **zero flashowania**. Najlepszy stosunek wartość/koszt.

### Path B — LA + oscyloskop na ORYGINALNYM firmware (2 flash cycle, definitywne)
Masz FX2 LA + oscyloskop 4ch. Procedura:
1. Wgraj oryginał: `esptool.py --port /dev/... write_flash 0x0 esp32/firmware/esp32_dump.bin`
   (przez J1/usb2ttl). OEM handshake utrzymuje watchdog przy życiu → brak cięć zasilania.
2. **LA (≥6 ch, 2 MHz)** na GPIO{5,18,23,25,32,33} podczas boot/display „IdLE" →
   identyfikacja, które 3 piny toggglują (CLK=ciągły burst 24 zboczy, DATA=nieregularny,
   LATCH=jeden puls/refresh). To daje **piny**.
3. **Oscyloskop 4ch** na znalezionych 3 pinach + 1 ref (GND lub inny) → precyzyjny
   waveform: bit period, szerokość pulsów CLK/LATCH, **bit order** (MSB/LSB z kształtu
   DATA), **byte order** (kolejność bajtów względem LATCH).
4. Zdekoduj protokół bezpośrednio z działającego OEM.
5. Wgraj ESPHome z poprawionymi pinami/układem.

**Daje:** piny + timing + bit order + byte order prosto z działającego sprzętu.
**Koszt:** ~12 min flash + ~30 min LA/scope. Złoty standard, krzyżuje Path A.

### Experiment C — Single-bit segment-sweep w ESPHome (1 flash na kandydat)
Zastąp coupling-prone `lcd_find` (all-1s) trybem, który:
- dla kandydata (CLK,CS,MOSI) generuje **24 wzorce, każdy z dokładnie JEDNYM bitem=1**,
  po 1s na wzorzec, z latchem.
- Coupling **nie może** zasymulować pojedynczego zapalonego segmentu (74HC595 output jest
  niskoomowy). Więc: jeśli jakikolwiek pojedynczy segment się zapali → piny poprawne I
  od razu mapujemy bit→segment/cyfra/dwukropek. Jeśli żadne przez 24 wzorce → piny złe.
- **Jeden flash mapuje cały display** dla danego triple'a. Uruchom na {25,32,33} first,
  potem {33,32,18}, {5,32,25} jako fallback.

**Daje:** definitywny test ESPHome-side + pełna mapa segmentów. **Koszt:** 5-6 min/triple.

### Enabler E1 — Zasilanie display board standalone na 5V (opcjonalnie)
Podaj +5V na J8 pin1 bez mainboardu → lokalny buck robi 3V3, ESP32 bootuje, **brak
watchdoga**. Umożliwia iterację firmware display przez OTA bez ryzyka cięcia zasilania.
(Uwaga: OEM firmware może nie odświeżać display bez stanu z MB — enabler głównie dla
naszych eksperymentów ESPHome, nie dla Path B.)

### Option D — Odczyt pinów live przez JTAG (NIEDOSTĘPNE)
0x3FF4E0C4/C8/CC trzymają live numery pinów (5-bit), BSS 0x3FFCD3B8/0x3FFCD3CC — live
ramkę + tabelę reorder. OpenOCD/JTAG czyta to w minuty. **Wymaga adaptera JTAG ESP32**
(GPIO12-15 wyglądają NC tutaj). Twój debugprobe to SWD (dla GD32), nie ESP32 JTAG →
**odpadnięte**. (Tani esp-prog ~$5 by to odblokował — opcjonalnie na przyszłość.)

### Experiment E — Needle-probe continuity przez lakier (destruktywne, ostateczność)
Przebij ostrymi igłami lakier na padach ESP32 GPIO25/32/33 i pinach 74HC595
DS/SH_CP/ST_CP, zmierz ciągłość. Definitywna mapa pinów. Ryzyko kosmetyczne na lacquer.
Tylko jeśli A+B+C zawiodą.

### Eksperyment uzupełniający F — Identyfikacja OE/MR 74HC595
Sprawdź (z LA na oryginale lub multimetrem po zdrapaniu lakieru), czy piny 13 (OE, active
low) i 10 (MR, active low) 3× 74HC595 są stałe podpięte (OE→GND, MR→VCC). Jeśli OE nie jest
na GND, display pozostanie ciemny mimo poprawnych danych — to tłumaczyłoby, dlaczego
„poprawne" piny nie świeciły. Brak 4. GPIO w firmware sugeruje hardwired, ale warto
potwierdzić fizycznie.

## 7. Sprzęt dostępny i zastosowanie

| Masz | Wykorzystanie |
|------|---------------|
| usb2ttl (J1) | Flashowanie oryginału/ESPHome (Path B), ew. esptool dump_mem w bootloader (ale RAM app nie załadowany → bezużyteczne dla live regs) |
| FX2 LA (≥6 ch) | Path B: identyfikacja pinów (6 GPIO jednocześnie) |
| Oscyloskop 4ch | Path B: precyzyjny timing + bit/byte order na 3 pinach |
| Pico debugprobe (SWD) | Tylko GD32 (U13/U16) — nie ESP32. Bezużyteczne dla LCD |
| Brak JTAG ESP32 | Option D niedostępna |

## 8. Rekomendowany plan (krok po kroku)

1. **Path A (Ghidra)** — dekompiluj 6 funkcji display. Odzyskaj piny (z 0x401908B4/
   0x401915CC), mapę znaków (0x40190FB4), bit order (0x401913E0 + filler tabeli reorder),
   układ ramki (maski per-bit). **Zero flash.**
2. **Path B (LA + scope na oryginale)** — walidacja krzyżowa pinów i timings z działającego
   OEM. Definitywne potwierdzenie Path A. (~12 min flash + 30 min pomiar).
3. **Eksperyment F** — potwierdź OE/MR 74HC595 hardwired (przy okazji Path B, scope na
   pinach 13/10 74HC595 — powinny być stałe).
4. **Experiment C (single-bit sweep)** — finalna walidacja ESPHome-side: flash z
   poprawionym `shift24` + układem ramki + mapą znaków, sweep potwierdza bit→segment,
   potem renderuj „IdLE"/„LoCK"/„Mow".
5. **Aktualizacja komponentu** — popraw `char_to_segments_`, `refresh_display` (układ
   bajtów ramki), piny w YAML. Jeden finalny flash do produkcji.

## 9. Ryzyka i pułapki

- **Bit-bang vs HW peripheral**: nasz shift24 działa na 74HC595 niezależnie od tego, że
  OEM używał peripheralu — ważne tylko piny + układ ramki + bit order. Timing 74HC595 jest
  łagodny (nasze delayMicroseconds(1-2) są w normie).
- **Byte order kaskady**: przy 3× 74HC595 kaskadowanych, **pierwszy przesunięty bajt
  ląduje w najdalszym chipie**. Aktualny kod `shift24(0x00, colon|dig, seg)` zgaduje
  układ — musi być zweryfikowany przez Path A/C.
- **Multipleksowanie**: 4 cyfry, ramka co 4ms (16ms pełny cykl) — aktualny refresh jest
  OK, ale mapping `dig = 1<<current_digit` musi trafić w rzeczywiste bity digit-select.
- **OE/MR** (patrz eksperyment F) — jeśli nie hardwired, display ciemny mimo poprawnych
  danych. Najczęstsza przyczyna „nie świeci mimo poprawnych pinów".
- **Fałszywe nadzieje lcd_find all-1s** — nie powtarzać testów all-1s; używać single-bit
  sweep (Eksperyment C).
- **Watchdog przy OTA** — mamy boot_delay:10; dla czystych eksperymentów display
  rozważ enabler E1 (zasilanie standalone).
- **Lakier na PCB** — utrudnia multimetr; LA/scope na padach ESP32 modułu (niepomalowane)
  działa bez zdrapywania.

## 10. Otwarte pytania / next steps

- Czy Path A (Ghidra) ma sens uruchomić teraz (zero flash, pełny protokół)? **Rekomendacja:
  tak, zacząć od dekompilacji 0x401908B4 i 0x401915CC — to daje piny.**
- Czy po Path A przeprowadzić Path B (LA+scope na oryginale) jako walidację?
- Po ustaleniu pinów: implementacja Experiment C (single-bit sweep) jako finalny dowód
  i budowa mapy segmentów.

## Załącznik: adresy funkcji do dekompilacji (Path A)

| Adres | Stack | Role | Co odzyskujemy |
|-------|:-----:|------|----------------|
| 0x401908B4 | 0x40 | CONFIG peripheral | **piny** (GPIO# do 0x3FF4E0C4/C8/CC) + maski ramki |
| 0x401915CC | ? | CONFIG/GPIO setup | **piny** (drugi zapis) |
| 0x40190FB4 | ? | RENDER (13-case) | **mapa znak→segment-bit** |
| 0x401913E0 | 0x30 | SERIALIZE | **bit/byte order** (indeksowanie tabeli 0x3FFCD3CC) |
| filler 0x3FFCD3CC | ? | (znaleźć przez xref zapisu do 0x3FFCD3CC) | zawartość tabeli reorder |
| 0x40190E38 | 0x30 | WRITE data buffer | rozmieszczenie bajtów 0x3FF4E130..140 |
| 0x4019153C | 0x30 | INIT | sekwencja init, timing OEM |
| 0x401B3D00 / 0x401B2D34 | ? | peripheral/GPIO setup | konfiguracja pinów/matrycy |

**Funkcje do zignorowania** (printf-family, nie display): 0x40199138, 0x40197AFC, 0x40198C78.
