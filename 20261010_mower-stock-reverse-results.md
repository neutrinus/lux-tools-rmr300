# 2026-10-10: co wnosi salonnikov/mower-stock-reverse

[salonnikov/mower-stock-reverse](https://github.com/salonnikov/mower-stock-reverse) (commit `4f97999`) to niezależna analiza tych samych płytek, `SNK_MAINBOARD_CP_V11` i `SNK_DISPLAY_CP_V11`, w egzemplarzu sprzedawanym jako VILLARTEC MI 302. Ich „chip1” to nasz **U13**, „chip2” to nasz **U16**. Autor pisze, że pracował z Claude Code i że wnioski trzeba weryfikować.

Ich dumpy są w [`archive/mower-stock-reverse/`](archive/mower-stock-reverse/README.md).

Oznaczenia: **[F]** sprawdzone w naszych dumpach, **[T]** ich ustalenie (pomiar na ich sprzęcie albo analiza ich dumpu), **[W]** wniosek.

## Porównanie dumpów [F]

| Układ | Wynik |
|---|---|
| U16 | Bit w bit identyczny z naszym `u16/firmware/u16_flash.bin`. Ich adresy dla chip2 działają u nas bez zmian |
| U13 | Inna wersja. Bootloader prawie identyczny (te same funkcje, np. `0x08000f38`), aplikacja przesunięta. Mechanizmy i stałe te same |
| ESP32 | `Display_esp32` **v3.02.05**, u nas v3.02.02. Algorytm deszczu jest w obu wersjach taki sam |

Przy okazji: nasz `u13_flash_1mb.bin` ma 20 uszkodzonych bajtów w bootloaderze (`0x080127a4…0x080127b7`), CRC bootloadera się nie zgadza. `u13_flash.bin` ma je poprawne. CRC aplikacji (`0x080FFFFC`, liczone po `0x08018000…0x080FFFFB`) zgadza się w obu.

## Czujnik deszczu

### Jak działa w oryginale [F]

Czujnik jest rezystancyjny: dwie sprężyny (J3/J4) dociśnięte do płytki w obudowie. Woda zmniejsza opór między nimi.

ESP32 (`0x400df064` init, `0x400defb8` wątek „rain detect thread”):

1. GPIO18 i GPIO5 to wyjścia sterujące elektrodami, GPIO36 to ADC1 kanał 0, 12 bitów, tłumienie 11 dB.
2. Cykl 2 s: 1 s w polaryzacji 1 (GPIO18 = 1, GPIO5 = 0), na jej końcu 5 próbek co 10 ms, potem 1 s w polaryzacji odwrotnej. Odwracanie chroni elektrody przed elektrolizą.
3. Średnia krocząca: `acc += raw − acc/4`, średnia = `acc/4`.
4. `acc > 11999` (średnia > 3000 z 4095) to sucho, inaczej mokro. 16 próbek z rzędu po tej samej stronie zmienia stan, czyli około 6 s.
5. Stan 1 = sucho, 2 = deszcz. Przy zmianie ESP wysyła `0x22000000 {"rain":stan}` (`0x400e2774`).

U13 (`service_rain`, `0x08039198`; obsługa ramki zapisuje `rain` do pola `+8`):

- `rain:2` uruchamia stan „raining”,
- `rain:1` odlicza opóźnienie po deszczu i kończy je („rain delay finish”),
- inna wartość w trakcie opóźnienia zaczyna je od nowa,
- wszystko tylko przy włączonym `rain_en`.

### Co było źle w naszym komponencie ESPHome [F]

Komponent czytał GPIO36 jako wejście cyfrowe, bez wzbudzania elektrod, i wysyłał `rain:0` albo `rain:1`. U13 reaguje tylko na `rain:2`, więc **kosiarka pod ESPHome nigdy nie wiedziała, że pada**. Do tego bez napięcia na GPIO18 elektrody nie dają sygnału, który dałoby się sensownie odczytać.

### Co zmieniłem

`components/snk_mower/snk_mower_rain.cpp` robi to samo co oryginał: te same piny, cykl, średnia, próg 3000 i licznik 16 próbek. Wysyła `1`/`2`. Nowe opcje i encje:

| Klucz YAML | Domyślnie | Co robi |
|---|---|---|
| `rain_pin` | — | wejście ADC (36); bez niego czujnik jest wyłączony |
| `rain_drive_a`, `rain_drive_b` | 18, 5 | piny wzbudzające elektrody |
| `rain_threshold` | 3000 | próg średniej ADC; poniżej = mokro |
| `rain_adc` | — | sensor diagnostyczny ze średnią ADC, co 30 s |
| `raining` | — | binary_sensor (moisture) |

Czy to pozwoli lepiej wykrywać deszcz: tak, w tym sensie, że teraz w ogóle działa tak jak w oryginale. Sensor `rain_adc` pokaże w Home Assistant, ile czujnik odczytuje na sucho i w deszczu, więc próg da się dobrać do swojego egzemplarza. Lepszej czułości niż w oryginale z samego firmware nie będzie; ogranicza ją elektroda i obudowa.

Nie testowane na kosiarce. Sprawdzone tylko `g++ -fsyntax-only` na atrapach nagłówków ESP-IDF i ESPHome. Używa sterownika `esp_adc/adc_oneshot.h` z ESP-IDF 5.x.

## Inne zmiany w komponencie, które z tego wynikają

Nie zmieniałem więcej kodu. Rzeczy, które da się dodać później:

- **Wyłączenie kosiarki z HA**: `0x10000004` („Robot manual power off”) już jest znane z poprzedniego śledztwa. Teraz wiadomo też, co U13 robi dalej: zatrzymuje silniki i trzyma PE12 nisko (`0x08070d3c` → `0x0807e72c`). Przycisk „Power off” to jedna linijka.
- **Dane baterii**: ESP dostaje od U13 `bat_per`, `bat_lv`, `bat_health` itp. (już w komponencie). Bezpośredniego dostępu do BMS z ESP32 nie ma, bo BMS wisi na USART2 U13.
- **Wyświetlacz**: ich mapa bitów (13/12/11/10 = cyfry 1–4, bit 7 kropka, bit 8 dwukropek) zgadza się z naszym `DIGIT_SELECT`. Nic do poprawy.
- **Zasilanie z USB-TTL**: płytka wyświetlacza zasilana z 3,3 V przejściówki resetuje się przez brownout, gdy wstaje Wi-Fi. Przy flashowaniu ESPHome zasilać ją z czegoś mocniejszego [T].

## Sprzęt: co nowego

### IMU zamiast „U22” [F]

U13 rozmawia na I²C (PB10/PB11) z adresem `0x68` z **TDK ICM-426xx** (akcelerometr + żyroskop): sterownik `tdk42688_lib\IcmAlgo.c`, sprawdzenie WHO_AM_I `0x47`/`0x6F` pod `0x0807ce6a`, funkcje I²C z bajtem `0xD0` pod `0x08053930`. Nasz wcześniejszy odczyt „U22” (bajty `0x00–0x5F`) to rejestry tego czujnika: temperatura ≈ 24 °C, przyspieszenie Z ≈ 1 g. Fizycznie IMU nie zostało jeszcze znalezione na zdjęciach; U22 pozostaje niezidentyfikowany.

### Bateria (BMS) [F częściowo]

USART2 U13, PD8/PD9, 19200 8N1, half-duplex. Ramki `1C A1 <LEN> <opcode> <args> <CRC-8/MAXIM>`, odpowiedź `3A A3 …`. Opcode'y `C1` (telemetria), `C3`, `53` (ogniwa), `CE` (wybudzenie), `B0…B4` (tryb ładowania). Szczegóły w [`BATTERY.md`](BATTERY.md#communication-protocol). Init USART2 i szablony ramek sprawdzone w naszym dumpie.

### Zasilanie [F]

- **PE12** = główny zatrzask zasilania, PE7 = szyna pomocnicza, PB0 = drugi zatrzask. Bootloader podnosi je na samym początku (`0x08000f38`).
- Wyłączenie: `0x08070d3c` zeruje PWM silników, PB12, PE9, PD11, a `0x0807e72c` opuszcza PE7 i w pętli PE12.
- Poprzedni opis `0x08070d3c` jako „odcięcie zasilania przez PB12/PE9/PD11” był błędny.
- Wyłączenie po ~16 s bez handshake'u: U13 resetuje się z watchdoga, ale sam reset nie zwalnia PE12 (bootloader podnosi go od razu). Co wyłącza kosiarkę po tym resecie, nie jest prześledzone.

### Sterowniki silników [T]

Na płycie MI 302 nie ma Allegro A4963, mimo nazw plików w firmware. Są trzy **Fortior FU6832N**, które emulują interfejs A4963 po SPI1. Piny: CS PD5/PD4/PD3 (lewe, prawe, nóż), enable PB12, PWM TIMER2 PC9/PC8/PC7. Na ich własnym firmware U13 koła nigdy nie ruszyły (nóż tak), a przyczyny nie ustalili.

### U16 [F]

- Cewki na ADC0 kanał 5 i ADC1 kanał 9 (dual, DMA).
- „Base voltage” to offset DC odbiornika, a nie pomiar baterii.
- Próg wykrycia: `|x| > 2500`.
- U16 nie steruje silnikami. Nasze `u16/notes/U16.md` mówiło inaczej; poprawione.

## Gdzie się mylą

- „chip2 = GD32F4xx”: to GD32F303, i ich drugi raport też to przyznaje.
- „Ramki z wyświetlaczem są XOR-owane `0x5B`”: to opcja w kodzie, nasze capture'y pokazują czysty JSON.
- Ich numery stanów (3 idle … 10 power off) to wewnętrzny automat U13, a nie pole `state` z `0x330000A0`.
- PIN `0x30000009` i start `0x300100de`: to klasa `0x30` w U13. Oryginalny ESP używa `0x41000005` i klawiszy `0x1000000x`.

## Zmienione pliki

| Plik | Zmiana |
|---|---|
| `components/snk_mower/*`, `snk-mower.yaml` | czujnik deszczu jak w oryginale, `rain:1/2`, encje `rain_adc` i `raining` |
| `archive/mower-stock-reverse/` | ich dumpy U13, U16 i ESP32 |
| `HARDWARE.md` | IMU, zatrzask zasilania, sterowniki FU6832N i piny, BMS na J5, piny deszczu i przycisków ESP32 |
| `BATTERY.md`, `u13/notes/GD32F305.md` | protokół BMS |
| `PIN.md`, `u13/notes/eeprom_dumping.md`, `ha.md` | urządzenie `0x68` to IMU; PIN w env na SPI NOR |
| `FIRMWARE_MAP.md` | wyłączanie, CRC obrazów, uszkodzone bajty w dumpie 1 MB, deszcz w ESP32 i U13, IMU, BMS, U16 |
| `20261009_claude_investigation.md` | opis wyłączania |
| `u16/notes/U16.md` | ADC zamiast „timerów”, brak sterowania silnikami, progi |
| `PROTOCOLS.md` | `0x22000000 {"rain":1|2}` |
| `README.md` | link, schemat systemu, katalog `archive/` |
