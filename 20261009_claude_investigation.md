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

### Śledzenie ścieżek na zdjęciach (2026-10-10) [Z]

Płytka jest polakierowana, więc zamiast przedzwaniania nałożyłem `display_back.jpg` na `display_front2.jpg` (transformacja perspektywiczna po 7 otworach i pinach J1, błąd ~0,2 mm) i prześledziłem ścieżki.

| Przycisk | Wynik | Pewność |
|---|---|---|
| K4 ON | przelotka pod przyciskiem → prosto do pinu `ON` złącza, bez elementów po drodze, nie idzie do ESP32 | widoczne |
| K1 START | pod dolną krawędzią modułu, mostek na spodzie pod J1, do wiązki przy sieci RC (R1/R23/R30); koniec przy IO22 (pin 36) zasłania klej anteny | prawdopodobne |
| K2 HOME | zgubione przy przycisku (przelotki pod obudową przełącznika); brak ścieżki do złącza | nieprześledzone |
| K3 OK | w stronę dolnej krawędzi pod brzęczykiem i naklejką, tam zgubione | nieprześledzone |

Wyraźnie widać krótkie ścieżki z pinu 31 (IO19) do C13 i z pinu 33 (IO21) do C10 (filtry RC wejść, rezystory „101” = 100 Ω). Dwie ścieżki z pinów `ST` i `OK` złącza dochodzą do tej samej wiązki. Żaden przycisk nie przechodzi przez tranzystor ani obwód zasilania. Zdjęcia są zgodne z GPIO22/21/19 z firmware i niczemu nie przeczą, ale nie domykają ścieżek. Rozstrzygnęłoby zdjęcie bez kleju na antenie (piny 35–38) albo z odłączonym złączem, robione prosto z góry.

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

**Brakujące zdjęcia.** `HARDWARE.md` powołuje się na `PXL_20260616_120305142 (2).jpg` i `PXL_20260620_182450200.jpg`. `.gitignore` wyklucza `/PXL_*.jpg`; zdjęcie jest w repo jako `img/display_front2.jpg` (strona elementów płytki: U5 WROOM-32UE, złącze J1 `3V3/T/R/GND/GND/P` do programowania, J2 do płyty głównej). Przycisków na tej stronie nie widać, więc nie rozstrzyga mapowania K1–K3.

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

## 9. Dopisek 2026-10-10: zdalne sterowanie, dane stanu, koszenie krawędzi

Powiązany projekt: [Sdahl1234/Sunseeker-lawn-mower](https://github.com/Sdahl1234/Sunseeker-lawn-mower) (integracja HA z chmurą Sunseeker). Klucze, które nasz ESP32 wysyła do chmury `sk-robot.com` (`mode`, `power`, `errortype`, `station`, `rain_status`, `mul_zon1`…, `Trimming`, `slice`), to dokładnie te, które ta integracja obsługuje dla modeli „OLD”. To ten sam protokół chmury.

### Komendy z aplikacji → UART [F]

Zadanie IoT w ESP32 (`0x400dc4a8`) przyjmuje z chmury `{"cmd":101,"mode":N}`. Tłumaczy `mode` przez tabelę `0x3f4046b4` na komendę UART do MB:

| `mode` z aplikacji | znaczenie (Sunseeker, OLD) | komenda ESP→MB |
|---|---|---|
| 0 | pauza / stop | `0x10000023` |
| 1 | start | `0x10000021` |
| 2 | do stacji | `0x10000022` |
| 3 | — | `0x10000007` |
| 4 | **koszenie krawędzi (border)** | `0x10000015` |

Gdy ESP jest w stanie 4 (błąd), każda komenda `mode` zamienia się na `0x10000007`.

Po stronie U13 dekoder komend (`0x08063808`, `dpport`) zamienia je na bity akcji, te same co dla klawiszy (zapis w `*0x200002c0 + 4`):

| bit | klawisze (`0x100000xx`) | zdalne | co robi U13 w stanie oczekiwania (`0x0806ab90`) |
|---|---|---|---|
| 0x01 | `0x0a` | `0x15` | `0x0803a2cc`: „idle manual trim command, leave to trim”, **tylko gdy kosiarka stoi w stacji** |
| 0x02 | `0x01` | `0x11`, `0x21` | `0x0803a080`: „manual start command, leave to cutting” |
| 0x04 | `0x02` | `0x12`, `0x22` | `0x08038f6c`: powrót |
| 0x08 | `0x03` | `0x13`, `0x23` | stop / pauza |
| 0x10 | `0x04` | `0x14`, `0x24` | wywołanie zwrotne (nieustalone) |
| 0x40 | `0x07` | — | wybór (START/HOME), okno potwierdzenia |

**Odpowiedź: tak, protokół ma koszenie krawędzi.** To `0x10000015` (albo `0x1000000a`). U13 wykonuje je tylko ze stacji. W przeciwnym razie loguje „trim command, but robot not in station, ignore”. Komend zdalnych nie widać w żadnym capture'ze (nikt nie używał aplikacji przy LA). To wynik z firmware, nie z testu.

W komponencie: `stop_mowing()` = `0x10000023`, `trim_edge()` = `0x10000015`. Start i powrót zostają na sekwencji klawiszy, bo ją potwierdzają capture'y. `0x10000021/22` są zapasową drogą.

### Co ESP32 wie o stanie [F]

Parser `0x330000A0` (`0x400d9d20`) zapisuje do struktury `0x3ffbf460`:

| klucz | znaczenie | w komponencie |
|---|---|---|
| `state` | stan MB (1–5 bez zmian, reszta mapowana na stan ESP) | Mower State |
| `bat_per`, `bat_lv` | % baterii, poziom (kreski) | Battery Level, Battery Bars |
| `bat_ctime`, `bat_dtime`, `bat_health` | czasy ładowania/rozładowania, zdrowie | Battery Health |
| `error` | kod błędu (maska bitowa, np. 16 = brak przewodu, 4 = podniesienie) | Error Code |
| `station` | w stacji | Is Docked |
| `rain_state`, `rain_delay` | deszcz, opóźnienie po deszczu | Rain Delay |
| `total_minutes`, `on_minutes`, `cur_minutes` | czasy pracy | Total/On Minutes |
| `cut_area`, `current_area` | powierzchnia | Cut Area |
| `led_en`, `white_en`, `night_en`, `night_start`, `night_end` | ustawienia LED/nocne | — |
| `ult_en`, `ult_sen` | czujnik ultradźwiękowy | — |
| `pwd_en`, `rain_en`, `sch_en`, `zone_en`, `com_en`, `sp_en`, `gps_en`, `map_en` | flagi funkcji | — |
| `bat_id`, `bat_name`, `bat_t` | typ ogniwa (np. `5S2P_SUMSANG_20R`) | Battery Name |
| `mb_hv/sv`, `bb_hv/sv`, `db_hv/sv`, `lb_hv/sv`, `mblt_sv` | wersje HW/SW płytek | Firmware Version |

Do chmury ESP wysyła m.in. `mode` (stan), `power` = `bat_per`, `errortype` (tylko w stanie błędu), `station`.

Mapowanie `mode` w chmurze (Sunseeker, `lawn_mower.py`) to: 0 czuwanie, 1 koszenie, 2 powrót, 3 ładowanie, 4 błąd, 5 PIN, 6 aktualizacja, 7 koszenie krawędzi.

### Home Assistant

ESPHome nie ma platformy `lawn_mower`. Komponent wystawia przyciski Start Mowing, Stop, Return to Dock i Trim Edge, sensor Mower State oraz sensory baterii. Do encji `lawn_mower` w HA trzeba osobnej integracji (np. MQTT `lawn_mower`) albo szablonu po stronie HA. Tego nie zrobiłem.

## 10. Dopisek 2026-10-10: watchdogi U13, szybkie wyłączenie i OTA

Pierwsza wersja tej sekcji opisywała tylko 3-sekundowy limit łącza i wyłączenie po ~20 minutach. Marek widział wyłączenie po kilkunastu sekundach, gdy ESPHome nie trzymał się protokołu. To osobna ścieżka, opisana niżej.

### Handshake przy starcie U13 [F] [C]

`0x080446e4` (dpport) przy starcie U13:

1. Wysyła `0x40000009` (BOOT_HEART) co 100 ms, najwyżej 25 razy (~2,5 s), aż ESP odpowie.
2. Wysyła `0x40000008` (BOOT_INIT) co 20 ms, najwyżej 50 razy (~1 s), aż ESP_INIT `{"init":3}` ustawi stan łącza 2.
3. Po sukcesie wysyła `0x20000004` dwa razy. Potem idzie DEVICE_INFO.
4. Po porażce ustawia stan 4 i sygnalizuje to `rw_init`.

Capture `02-boot-pin` (la_decode, oba kierunki w jednej osi czasu) pokazuje, że oryginalny ESP odpowiada w ~3 ms na każdą ramkę: ESP_INFO na BOOT_HEART i ESP_INIT na BOOT_INIT. Stary komponent wysyłał ESP_INFO i ESP_INIT dopiero po DEVICE_INFO, a DEVICE_INFO przychodzi dopiero po udanym handshake.

### Szybkie wyłączenie: watchdog sprzętowy [F]

| Element | Adres | Opis |
|---|---|---|
| FWDGT 16 s | `0x0805b7ac` → `0x0805deba` | dzielnik ÷256, reload 2500 (IRC40K), ustawiany na początku `rw_init` |
| FWDGT 1,6 s | `0x0806ff0a` → `0x0805dea6` | ÷32, reload 2000, po starcie usługi konfiguracji |
| Pętla błędu `rw_init` | `0x0805b974` | gdy `[ctx+8]` (bity błędów inicjalizacji) ≠ 0: co 2 s `0x20000002 {"error":bity}` i miganie, **bez karmienia watchdoga** |
| Bity błędów | `0x08060a44` | 0x1 ultradźwięki, 0x2 wersja MB, **0x4 „display borad disconnect”**, 0x8 płytka przewodu, 0x10 flash, … |
| Wyłączenie | `0x08070d3c` → `0x0807e72c` | zeruje PWM silników, PB12 (enable sterowników), PE9, PD11, potem PE7 i w pętli **PE12 (główny zatrzask zasilania)** |

Gdy ESP nie odpowie na handshake, U13 kończy inicjalizację z błędem, wysyła `0x20000002` i po ~16 s resetuje się przez watchdog. Kosiarka gaśnie właśnie wtedy, co zgadza się z „kilkunastoma sekundami” i z `ha.md` §4 („MB wysyła 0x20000002 i odcina zasilanie”) [W: przejścia od porażki handshake do bitu 0x4 nie prześledziłem do końca]. Sam reset nie zwalnia zatrzasku: bootloader od razu znowu podnosi PE12 (`0x08000f38`) i loguje „watchdog Triggered”. Co wyłącza kosiarkę po resecie, nie jest prześledzone.

Okno ciszy `boot_delay: 30` dokładnie wywoływało ten scenariusz: ESP milczał akurat wtedy, gdy U13 czekał na odpowiedź.

### Inne ścieżki wyłączenia [F]

- **`0x10000004`, `0x10000014`, `0x10000024`** ustawiają bit akcji 0x10. Każdy proces U13 (security, wait, charging, error, find_bd i inne) reaguje na niego „Robot manual power off” i stanem 0xa (wyłączenie). To polecenie wyłączenia, a nie nieznane zdarzenie UI. Stary komponent go nie wysyłał (sprawdziłem historię gita).
- **Ponad 10 nieparsowalnych ramek z rzędu** (`0x08046f94`, licznik w `0x08046e8a`) ustawia stan łącza 6. Jeśli tak jest w chwili startu menedżera procesów, `0x08070f18` loguje „communication failed” i odcina zasilanie.
- **3 s bez żadnej ramki** w trakcie pracy: `0x08044164` ustawia stan 4 i wysyła `0x20000004`, `deal_safety` zgłasza `0x400000` (display_error), a `process_error` wyłącza zasilanie po 48000 tyknięciach. Tyknięcie ma 25 ms (licznik minut pracy w `0x0802772e` dzieli przez 2400), więc to 20 minut. Po powrocie ramek U13 sam wychodzi z błędu („recover dpport”).
- `process_security` wyłącza po 20 minutach czekania na PIN („Robot wait input passwaord >20minutes”).

### Co zmieniłem w komponencie

- **Handshake na zasadzie pytanie-odpowiedź**, jak w oryginale: BOOT_HEART → ESP_INFO, BOOT_INIT → ESP_INIT, `0x20000004` → łącze gotowe. Po tym zapytania o harmonogram, deszcz i strefy oraz PIN.
- **Strażnik łącza**: osobny `esp_timer` wysyła KEEPALIVE, gdy przez 1,5 s nic nie poszło (np. w trakcie uploadu OTA, który blokuje `loop()`). Zapisy na UART są chronione mutexem.
- **Ciepły restart**: jeśli po starcie ESP przyjdzie zwykła ramka (np. RTC albo STATUS), U13 już działa, więc komponent od razu przechodzi w tryb pracy.
- **Interwały jak w oryginale**: POLL co 100 ms przed handshake, KEEPALIVE co 500 ms i status WiFi/BT co 1 s po nim. Usunąłem okresowe ESP_INFO i ESP_STATE, których oryginał po starcie nie wysyła.
- **Błąd, który nadpisywał harmonogram**: stary `send_trim()` wysyłał `0x300000A6` z `"auto":1` i godziną startu równą bieżącej, czyli przy każdym starcie włączał automatyczne koszenie o tej porze codziennie. Oryginał wysyła `0x300000A6` bez pól, jako zapytanie. Teraz komponent robi tak samo.
- **Stan 8** (wyjazd ze stacji do koszenia, capture `trzeci`) jest teraz pokazywany jako koszenie, a nie powrót.
- **Sprzątanie**: usunięte `boot_delay`, `compat_mode`, sensory `battery_voltage` i `signal_level` (nigdy nie publikowane), `send_action` i nieużywane pola. Kod podzielony na `protocol.{h,cpp}` (stałe i ramki), `snk_mower.cpp` (łącze, wysyłanie, akcje), `snk_mower_rx.cpp` (odbiór) i `snk_mower_display.cpp` (wyświetlacz, brzęczyk, stan w HA).

**Weryfikacja:** `esphome config` przechodzi. Pełnej kompilacji nie da się tu zrobić (rejestr PlatformIO jest zablokowany), więc kod sprawdziłem `g++ -fsyntax-only` na atrapach nagłówków i w symulacji na hoście, która odtwarza ramki MB z `02-boot-pin`. Komponent odpowiada na każde BOOT_HEART i BOOT_INIT, łącze wstaje po `0x20000004`, a po restarcie samego ESP wraca do pracy na pierwszej ramce RTC. Na kosiarce nie testowane.
