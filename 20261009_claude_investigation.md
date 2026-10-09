# Śledztwo 2026-10-09: przyciski frontu, ścieżka UART, start koszenia

Najpierw firmware (ESP32 `esp32/firmware/ota_0.bin`, U13 `u13/firmware/u13_flash.bin`, U16 `u16/firmware/u16_flash.bin`), potem zdjęcia płytek (`img/`), na końcu capture'y LA. Wcześniejsze wnioski z repo traktowałem jako hipotezy do sprawdzenia.

Oznaczenia: **[F]** potwierdzone w firmware, **[Z]** widoczne na zdjęciu, **[C]** potwierdzone capture'em, **[W]** wniosek, niezweryfikowany fizycznie.

Narzędzia, którymi da się to wszystko powtórzyć, opisuje [`tools/re/README.md`](tools/re/README.md).

## TL;DR

1. **Przyciski START/HOME/OK czyta ESP32** na GPIO22, GPIO21 i GPIO19 (wejście, wewnętrzny pull-up, aktywne niskim) [F]. ON (K4) idzie tylko do płyty głównej [F] [Z].
2. **ESP32 rozmawia bezpośrednio z U13** (`driver_dpport`, USART0) [F]. U16 nie jest mostem: to MCU przewodu granicznego i czujników podnoszenia, połączony z U13 osobnym UART-em (`bdport`) [F].
3. **Komendy `0x1000000x` to klawisze, a nie "error ACK"** [F] [C]:
   - START albo HOME wysyła `0x10000007`,
   - OK w ciągu 3 s po START wysyła `0x10000001` (start koszenia),
   - OK w ciągu 3 s po HOME wysyła `0x10000002` (powrót do stacji).
4. **Tak, ESPHome może uruchomić koszenie**, zarówno z przycisków, jak i zdalnie, wysyłając tę samą sekwencję [C]. Kod jest w komponencie, ale na sprzęcie jeszcze go nie testowano.
5. Poprzedni `esp32/firmware/disasm.s` był przesunięty o 8 bajtów, a wnioski z niego (SPI 12/10, protokół `0xAA 0x55`) były błędne. Plik został wygenerowany od nowa.

## 1. ESP32: sterownik przycisków [F]

Aplikacja `Display_esp32` v3.02.02, ESP-IDF 4.4.3. Aktywna partycja to `ota_0` (otadata seq=1).

**Inicjalizacja** (`0x400daf7c`, wywoływana z BSP init `0x400d77d8`; string błędu "button init error"):

```
gpio_set_direction(22, INPUT)  gpio_set_pull_mode(22, PULLUP_ONLY)
gpio_set_direction(21, INPUT)  gpio_set_pull_mode(21, PULLUP_ONLY)
gpio_set_direction(19, INPUT)  gpio_set_pull_mode(19, PULLUP_ONLY)
```

Funkcje IDF rozpoznałem po stringach w ścieżkach błędów:

| Adres | Funkcja |
|---|---|
| `0x4012ce9c` | `gpio_set_direction` |
| `0x4012ce0c` | `gpio_set_pull_mode` |
| `0x4012cdd0` | `gpio_get_level` |

**Odczyt** (`0x400daef8`) jest aktywny niskim stanem i składa maskę: bit0 = GPIO22, bit1 = GPIO21, bit2 = GPIO19. Zmiana jest uznawana po 7 kolejnych zgodnych próbkach.

**Pętla UI** (`0x400e27f4`) wykonuje się co 10 ms (`vTaskDelayUntil`), więc debounce trwa około 70 ms. W capture'ach komenda pojawia się 75–80 ms po zboczu linii [C]. Kontekst UI to `*0x3ffc5b18`: +0x10 to sterownik przycisków, +0x26 i +0x27 to poprzedni i bieżący kod klawiszy.

**Test fabryczny** (`0x400e0bc4`, "ft-key-", "KeyNum = %d") sprawdza dokładnie klawisze 1, 2 i 4. Na ESP32 są więc trzy przyciski.

### Który GPIO to który przycisk

Długie przytrzymanie (300 ticków, czyli 3 s) generuje zdarzenia obsługiwane w dyspozytorze `0x400e2194`. Zestawienie ich z instrukcją (`docs/manual.txt`, rozdział "Setting"):

| Kod klawiszy | Zdarzenie | Firmware | Instrukcja | Wniosek |
|---|---|---|---|---|
| 1 | 0x64 | ustawianie daty i czasu | START 5 s = rok, data, czas | 1 = START = **GPIO22** |
| 2 | 0x69 | menu `RaiN/zONe/LED/ult` | HOME 3 s = RAIN | 2 = HOME = **GPIO21** |
| 3 (1+2) | 0x65 | zmiana PIN | START+HOME 3 s = PIN | zgodne |
| 4 | 0x66 | tryb 0x0f | OK 3 s = godziny koszenia | 4 = OK = **GPIO19** |
| 5 (1+4) | 0x68 | czas startu | START+OK 3 s = czas startu | zgodne |
| 6 (2+4) | 0x67 | dni | HOME+OK 3 s = dni w tygodniu | zgodne |

Przycisk ON nie występuje w żadnej kombinacji, ani w firmware, ani w instrukcji. W instrukcji służy tylko do włączenia urządzenia. „ON/OFF” w menu deszczu to napis na wyświetlaczu.

Fizycznie nie przedzwoniłem przycisków. Do weryfikacji wystarczy ciągłość K1 ↔ pin 36 WROOM (IO22), K2 ↔ pin 33 (IO21), K3 ↔ pin 31 (IO19).

### Inne GPIO ESP32 [F]

| Funkcja | Piny |
|---|---|
| Wyświetlacz SPI (`TubeInit`, `0x400e6da0`) | MOSI 25, SCLK 33, CS 32, 400 kHz |
| Buzzer | 27 |
| Deszcz | ADC 36 oraz 18/5 |
| Detekcja modułu RF | 34/39 |
| UART do MB | TX 17, RX 16 |

## 2. Złącze płytki wyświetlacza [Z]

Na `img/display_back.jpg` opis 7-pinowego złącza (od góry) to: `OK`, `ST`, `GND`, `↓`, `↑`, `ON`, `5U`.

- Na złącze wychodzą OK, START i ON.
- **HOME nie ma pinu**, więc idzie wyłącznie do ESP32, co zgadza się z firmware.

Wcześniejszy skan GPIO w repo widział tylko GPIO19. Najpewniej dlatego, że nie włączał pull-upów: GPIO21 i 22 pływały, a linia OK ma podciągnięcie po stronie MB [W].

## 3. Płyta główna [F]

**U13 (GD32F305):**

| Port | Sterownik | Peryferium | Literał |
|---|---|---|---|
| Wyświetlacz | `driver_dpport_snk_v1.c` | USART0 | `0x40013800` |
| U16 | `driver_bdport_snk_v1.c` | USART1 | `0x40004400` |
| Moduł LED/ult | `driver_ledport_snk_v1.c` | UART3 | `0x40004C00` |

U13 ma też własny sterownik przycisków ("button driver init failed"). Czyta **PE10** (`0x0804de88`) i **PE11** (`0x0804deb4`), aktywne niskim, vtable `*0x20000674 + 0x18`. Bootloader U13 (`key.c`) czyta te same piny i loguje "key press power on".

[W] PE10 i PE11 to najpewniej linie `ST` i `OK` z J8, czyli równoległa ścieżka do wybudzania lub zasilania. Nie ustaliłem, który pin jest którym. Ścieżki J8 na `img/mainboard_bottom.jpg` biegną w stronę U13 [Z].

**U16 (GD32F303):** jedyny port JSON to `mport` (`driver_mboard_port_snk_v2.c`, USART2, `0x40004800`). Stringi dotyczą wyłącznie "bdboard": przewodu, podnoszenia i wersji. Nie ma tam nic o przyciskach ani wyświetlaczu.

## 4. Capture'y: przyczyna i skutek [C]

Dekodowane `tools/re/la_decode.py`, z obydwoma kierunkami w jednej osi czasu. Stary `decoded.json` dzieli ramki według kanału i przez to gubi kolejność zdarzeń.

**`captures/2026-06-21/trzeci`, START a potem OK, koszenie rusza:**

```
16.7372  START pressed
16.8128  ESP→MB 0x10000007
17.6022  OK pressed
17.6828  ESP→MB 0x10000001
17.7145  MB→ESP 0x41000005
17.7364  MB→ESP 0x330000A0 {"state":8}
```

**`czwarty`, HOME a potem OK, kosiarka wraca do stacji:**

```
17.6614  ESP→MB 0x10000007
18.0219  OK pressed
18.1013  ESP→MB 0x10000002
18.1488  MB→ESP 0x41000006
18.1998  {"state":9}
20.2982  {"station":true}
```

**`drugi` (26.20 s / 27.40 s) i `04-return-home` (28.35 s):** ta sama sekwencja START i OK, ale MB odpowiada `err:16`, bo kosiarka była podniesiona albo poza przewodem.

Płyta główna reaguje dopiero na ramkę UART, a nie na samo zbocze linii START. Naciśnięcia w trakcie wpisywania PIN-u nie generują żadnych ramek: ESP32 sam prowadzi menu i wysyła gotowy `{"cmd":0x41000005,"pwd":…}`. MB odpowiada na to `0x41000020 {"result":1}`, więc `0x41000020` to wynik PIN-u, a nie "START_ACK".

## 5. Odpowiedzi na pytania

### Czy po wgraniu ESPHome kosiarka zacznie kosić po wciśnięciu przycisków?

**Tak.** Komponent robi teraz to samo co oryginał:

- START i HOME wysyłają `0x10000007` i otwierają okno 3 s,
- OK w tym oknie wysyła `0x10000001` albo `0x10000002`.

Implementacja jest w `components/snk_mower/snk_mower.cpp` (`key_start`, `key_home`, `key_ok`, `arm_key`). W `snk-mower.yaml` są trzy `binary_sensor` z `INPUT_PULLUP` i `inverted: true`.

### Czy da się uruchomić koszenie zdalnie?

**Tak.** `start_mowing()` wysyła `0x10000007`, a po 500 ms `0x10000001`. `return_to_dock()` wysyła `0x10000007`, a po 500 ms `0x10000002`. Przyciski "Start Mowing" i "Return to Dock" w Home Assistant wołają te funkcje.

### Zastrzeżenia

- Nie testowano na sprzęcie.
- Tutaj przeszła tylko walidacja `esphome config`. Pełna kompilacja się nie udała, bo registry PlatformIO jest zablokowane w tym środowisku.
- `snk-mower.yaml` pobiera `external_components` z `github://…@main`. Do testu trzeba zmergować gałąź albo zmienić ref.
- MB przyjmie start tylko w stanie, w którym oryginał też by go przyjął: po PIN-ie, bez błędu, na przewodzie. Inaczej odpowie `err`.
- Pozostałe znane problemy z `ha.md` (stabilność komunikacji, nadzór MB) nie były przedmiotem tego śledztwa.
- Nie ustaliłem, co wyzwala zdarzenia `0x10000003` i `0x10000004`.

## 6. Co było w repo błędne i co zmieniłem

| Było | Jest | Gdzie |
|---|---|---|
| ESP32 → U16 → U13, U16 jako most | ESP32 → U13 dpport; U16 na bdport | PROTOCOLS, README, HARDWARE, ha.md, U16.md, MOWING_COMMAND_ANALYSIS |
| START/HOME idą do U16, ESP nie może startować | START/HOME/OK na ESP32, start przez UART | j.w. oraz `captures/2026-06-21/README.md` |
| `0x10000001/2/7` = ERR_ACK | komendy klawiszy | PROTOCOLS, `captures/README.md`, notatki 03/04, ha.md |
| `0x41000020` = START_ACK | wynik weryfikacji PIN | PROTOCOLS, `captures/README.md` |
| SPI 12/10, protokół `0xAA 0x55` | SPI 25/33/32, JSON | `esp32/notes/ESP32_GPIO_ANALYSIS.md` |
| `disasm.s` przesunięty o 8 B | wygenerowany od nowa (7185 funkcji, rozwiązane `l32r` i `call`) | `esp32/firmware/disasm.s` |
| `esp32_img2elf.py` z na sztywno wpisanymi offsetami nagłówków | parsuje tablicę segmentów, dane od `offset+8` | `tools/esp32_img2elf.py` |

**Niebezpieczny błąd w komponencie.** Stary `send_error_ack()` po każdym `ERROR_NOTIFY` wysyłał `0x10000007`, `0x10000001` i `0x10000002`. To było jak „START, OK, start koszenia, a potem wracaj do stacji” po każdym błędzie, także po podniesieniu kosiarki. Usunąłem to. Błędy nie wymagają żadnego potwierdzenia od ESP32.

**Brakujące zdjęcia.** `HARDWARE.md` powołuje się na `PXL_20260616_120305142 (2).jpg` i `PXL_20260620_182450200.jpg`. Nie ma ich w repo, bo `.gitignore` zawiera `/PXL_*.jpg`.

W dokumentach dodałem na górze ramki „Korekta 2026-10-09” i poprawiłem tabele oraz wnioski. Historię rozumowania (np. metody 1–6 w ha.md) zostawiłem z przekreśleniami.

## 7. Narzędzia (`tools/re/`)

```bash
python3 -m pip install 'capstone>=6'
python3 tools/re/esp32dis.py esp32/firmware/ota_0.bin dis 400daf7c 400db01c
python3 tools/re/gd32dis.py u13/firmware/u13_flash.bin dis 0x0804de88 0x0804dec8
python3 tools/re/la_decode.py captures/2026-06-21/trzeci/trzeci.sr --uart D1=ESP,D2=MB --lines D3=START,D4=OK
```

Szczegóły, zastrzeżenia i tabela adresów są w `tools/re/README.md`.

## 8. Otwarte

- Przedzwonić K1, K2 i K3 do GPIO22, 21 i 19 oraz linie J8 `ST`/`OK` do U13 PE10/PE11.
- Ustalić wyzwalacze zdarzeń `0x10000003` i `0x10000004`.
- Przetestować na kosiarce, z LA na D1 i D2, `start_mowing()`, `return_to_dock()` oraz START/HOME→OK.
