# Dokumentacja reverse-engineeringu mainboardy (MB) kosiarki RMR300

## Historia dokumentu

| Data | Autor | Opis |
|------|-------|------|
| 2026-06-22 | Marek | Kompleksowa dokumentacja reverse-engineeringu |

---

## 1. Konfiguracja sprzętowa

### Płyta główna (MB)
- **MCU (U16)**: GD32F303CGT6 (ARM Cortex-M4F) — komunikacja z ESP32
- **MCU (U13)**: GD32F305AGT6 (ARM Cortex-M4F) — sterowanie silnikami, KV-store, PIN
- U16 działa jako pomost JSON między ESP32 a U13 na FreeRTOS

### ESP32-WROOM-32UE (płyta wyświetlacza)
- **Fizyczne piny**:
  - `GPIO17` — TX (UART do MB)
  - `GPIO16` — RX (UART z MB)
  - `GPIO33` — CLK (SPI, wyświetlacz 7-segmentowy)
  - `GPIO25` — MOSI (SPI, wyświetlacz 7-segmentowy)
  - `GPIO32` — CS (SPI, wyświetlacz / latch 74HC595)
  - `GPIO27` — Buzzer (PWM cyfrowy)
  - `GPIO36` — Czujnik deszczu (GPIO36 = ADC1_CH0, wejście cyfrowe)
  - `GPIO19` — Przycisk OK (do U16, aktywny LOW)
- **UART**: 230400 8N1, standardowa polaryzacja
- **Wbudowany czujnik światła**: na ADC
- **Miejsce na czujnik Wi-Fi/BT**: niepopulowane (piny przypisane do UART)

### Wyświetlacz
- 4-cyfrowy 7-segmentowy LED (zegarowy)
- Sterowany przez 3× 74HC595 (przesuwne rejestry)
- Fake SPI: 24-bitowe ramki (`b0 + b1 + seg`)
- Odświeżanie: hardware timer 8ms (125Hz)
- `CS` pełni rolę `OE` (Output Enable) — trzymany HIGH, opuszczany na czas transmisji
- Mapowanie segmentów: standardowe 7-segmentowe (bity: g, f, e, d, c, b, a, DP)
- Adresowanie digtów: `b1 = {0x20, 0x10, 0x08, 0x04}` (dla digtów 0..3 od lewej)
- Colon (dwukropek) — lokalizacja nieznaleziona (TODO)

### Czujnik deszczu
- Podpięty do GPIO36, aktywny LOW (0 = deszcz, 1 = brak deszczu)
- W oryginalnym firmware ESP wysyła `{"cmd":570425344,"rain":0/1}` przy zmianie

### Buzzer
- GPIO27, cyfrowy HIGH = włączony
- Używany przy błędach (300ms) i starcie koszenia (100ms)
- W oryginalnym firmware pojawia się też przy przyjęciu PIN (5 cykli)

---

## 2. Odkrycia protokołu

### Format ramki

```
&{json}<CRC>#
```

- Prefiks: `0x26` (`&`)
- JSON z polem `cmd` (32-bit integer)
- CRC: Dallas/Maxim CRC-8 (poly 0x31, init 0x00, ref_in=true, ref_out=true) — liczony tylko z bajtów JSON (bez `&` i `#`)
- Terminator: `0x23` (`#` — **pojedynczy**, nie `##`)
- Ograniczenie: max 128 bajtów na ramkę (ograniczenie drivera mport U16)

> **Korekta 2026-06-22**: Poprzednia wersja dokumentowała terminator `##` (dwa `0x23`).
> Dekodowanie captures pokazuje pojedynczy `#` — np. `...7D A0 23 26...` = `}` CRC `#` `&`(next frame).

### Wszystkie stałe CMD z kodu źródłowego

> **⚠️ KOREKTA 2026-06-22**: Poniższe tabele "ESP → MB" i "MB → ESP" zawierają **błędnie przypisane kierunki**
> dla wielu komend. Tabele zostały wygenerowane z kodu źródłowego, który definiuje stałe CMD
> niezależnie od kierunku — wiele komend jest obsługiwane po obu stronach (np. PIN_SEND jest wysyłane
> przez ESP, ale PIN_RESULT jest odbierane od MB).
>
> **Autorytatywna tabela kierunków**: [PROTOCOLS.md](PROTOCOLS.md) — sekcja "Complete Command Catalog".
> Krótko: prefixy `0x20/0x33/0x50` = MB→ESP, `0x10/0x22/0x30/0x31` = ESP→MB, `0x41` = MB→ESP (z wyjątkiem 0x41000005 PIN_SEND = ESP→MB).

#### ESP → MB (wysyłane przez ESP32)

| Stała | HEX | DEC | Opis |
|-------|-----|-----|------|
| `CMD_POWER_ON` | `0x20000001` | 536870913 | Power-on wake z `action:0` |
| `CMD_POWER_READY` | `0x20000004` | 536870916 | Gotowość do pracy |
| `CMD_SETTING_MODE` | `0x30000006` | 805306374 | Tryb ustawień |
| `CMD_SETTING_APPLY` | `0x30000007` | 805306375 | Zastosuj ustawienia |
| `CMD_ESP_STATE` | `0x30000028` | 805306408 | Raport stanu ESP |
| `CMD_ESP_WIFI` | `0x30000021` | 805306401 | Status Wi-Fi z polami `wifi`, `str` |
| `CMD_ESP_BT` | `0x30000022` | 805306402 | Status BT z polami `bt`, `str` |
| `CMD_ESP_POLL` | `0x300000A1` | 805306529 | Poll/heartbeat (ciągły, co ~30ms) |
| `CMD_ESP_TRIM` | `0x300000A6` | 805306534 | Harmonogram koszenia |
| `CMD_ESP_RAIN_CFG` | `0x300000A7` | 805306535 | Konfiguracja deszczu |
| `CMD_ESP_MULTIZONE` | `0x300000A8` | 805306536 | Konfiguracja multi-stref |
| `CMD_SETTING_START` | `0x31000016` | 822083606 | Start ustawień |
| `CMD_SETTING_SUB` | `0x31000017` | 822083607 | Sub-ustawienie |
| `CMD_PIN_RESULT` | `0x33000021` | 855638177 | Wynik PIN (z DEVICE_INFO!) |
| `CMD_PIN_RESULT2` | `0x33000022` | 855638050 | Wynik PIN (confirm) — w kodzie: `0x33000022` (!= 0x33000021) |
| `CMD_STATUS` | `0x330000A0` | 855638176 | Stan kosiarki (bat, state, error, itp.) |
| `CMD_DEVICE_INFO` | `0x330000A1` | 855638177 | Info o urządzeniu (name, sn, model, version) |
| `CMD_HW_VERSIONS` | `0x330000A2` | 855638178 | Wersje sprzętowe (MB, BB, DB, MBLT) |
| `CMD_SCHEDULE` | `0x330000A6` | 855638182 | Harmonogram |
| `CMD_RAIN_CFG_RSP` | `0x330000A7` | 855638183 | Odpowiedź konfiguracji deszczu |
| `CMD_MULTIZONE_RSP` | `0x330000A8` | 855638184 | Odpowiedź multi-stref |
| `CMD_MB_DEVICE_INFO` | `0x330000A9` | 855638185 | MB device info (sw/hv/sv) |
| `CMD_SCHEDULE_END` | `0x330000AA` | 855638186 | Koniec bloku harmonogramu |
| `CMD_MAP_CFG` | `0x330000B0` | 855638192 | Konfiguracja mapy (area, map_sn) |
| `CMD_ESP_BOOT` | `0x40000004` | 1073741828 | BOOT handshake |
| `CMD_ESP_INIT` | `0x40000001` | 1073741825 | INIT z `init:3` |
| `CMD_ESP_INFO` | `0x40000006` | 1073741830 | ESP HW/SW info (hv, sv, mac) |
| `CMD_BOOT_INIT` | `0x40000008` | 1073741832 | Init w toku |
| `CMD_BOOT_HEART` | `0x40000009` | 1073741833 | Boot heartbeat |
| `CMD_RTC` | `0x40000011` | 1073741841 | Synchronizacja czasu RTC (co ~1s) |
| `CMD_START_TIME_Q` | `0x40000012` | 1073741842 | Zapytanie o czas startu |
| `CMD_CUT_TIME_Q` | `0x40000013` | 1073741843 | Zapytanie o czas koszenia |
| `CMD_UNKNOWN_14` | `0x40000014` | 1073741844 | Nieznane |
| `CMD_LIGHT` | `0x40000020` | 1073741856 | Poziom światła |
| `CMD_BOOT_ACK` | `0x40000021` | 1073741857 | Boot ACK |
| `CMD_LOCK` | `0x41000002` | 1090519042 | Lock z `lock:0/1` |
| `CMD_EXEC_ACTION` | `0x41000003` | 1090519043 | Exec action (po STOP) |
| `CMD_ERROR_NOTIFY` | `0x41000004` | 1090519044 | Notyfikacja błędu |
| `CMD_PIN_SEND` | `0x41000005` | 1090519045 | Wysłanie PIN (z `pwd`) |
| `CMD_RETURN_HOME` | `0x41000006` | 1090519046 | ★ Return to dock |
| `CMD_SHUTDOWN` | `0x41000008` | 1090519048 | Shutdown |
| `CMD_START_ACK` | `0x41000020` | 1090519072 | START ACK dla MB |
| `CMD_BATTERY` | `0x50000021` | 1342177313 | Poziom baterii |

#### MB → ESP (otrzymywane przez ESP32)

| Stała | HEX | DEC | Opis |
|-------|-----|-----|------|
| `CMD_POWER_ON` | `0x20000001` | 536870913 | Power-on (action:0) |
| `CMD_POWER_READY` | `0x20000004` | 536870916 | Power ready |
| `CMD_RAIN` | `0x22000000` | 570425344 | Stan czujnika deszczu (`rain:0/1`) |
| `CMD_ESP_KEEPALIVE` | `0x30000005` | 805306373 | Keepalive (ciągły, ~1s) |
| `CMD_ESP_WIFI` | `0x30000021` | 805306401 | Zapytanie o status Wi-Fi |
| `CMD_ESP_BT` | `0x30000022` | 805306402 | Zapytanie o status BT |
| `CMD_ESP_STATE` | `0x30000028` | 805306408 | Notyfikacja stanu |
| `CMD_ESP_POLL` | `0x300000A1` | 805306529 | Poll MB → ESP (echo) |
| `CMD_ESP_TRIM` | `0x300000A6` | 805306534 | Zapytanie o harmonogram |
| `CMD_PIN_RESULT` | `0x33000021` | 855638177 | Wynik PIN (z `result:true/false`) |
| `CMD_PIN_RESULT2` | `0x33000022` | 855638050 | Drugi wynik PIN |
| `CMD_STATUS` | `0x330000A0` | 855638176 | Stan kosiarki (cykliczny) |
| `CMD_DEVICE_INFO` | `0x330000A1` | 855638177 | Pełna konfiguracja urządzenia |
| `CMD_HW_VERSIONS` | `0x330000A2` | 855638178 | Wersje sprzętowe |
| `CMD_SCHEDULE` | `0x330000A6` | 855638182 | Harmonogram |
| `CMD_RAIN_CFG_RSP` | `0x330000A7` | 855638183 | Konfiguracja deszczu |
| `CMD_MULTIZONE_RSP` | `0x330000A8` | 855638184 | Multi-strefy |
| `CMD_MAP_CFG` | `0x330000B0` | 855638192 | Mapa (area, map_sn) |
| `CMD_SCHEDULE_END` | `0x330000AA` | 855638186 | Koniec harmonogramu |
| `CMD_ESP_BOOT` | `0x40000004` | 1073741828 | BOOT od MB |
| `CMD_ESP_INIT` | `0x40000001` | 1073741825 | INIT confirm |
| `CMD_ESP_INFO` | `0x40000006` | 1073741830 | Zapytanie o ESP info |
| `CMD_BOOT_INIT` | `0x40000008` | 1073741832 | Init w toku |
| `CMD_BOOT_HEART` | `0x40000009` | 1073741833 | Boot heartbeat |
| `CMD_RTC` | `0x40000011` | 1073741841 | RTC heartbeat |
| `CMD_LIGHT` | `0x40000020` | 1073741856 | Poziom światła |
| `CMD_BOOT_ACK` | `0x40000021` | 1073741857 | Boot ACK |
| `CMD_LOCK` | `0x41000002` | 1090519042 | Lock state |
| `CMD_EXEC_ACTION` | `0x41000003` | 1090519043 | Exec action |
| `CMD_ERROR_NOTIFY` | `0x41000004` | 1090519044 | Error notify |
| `CMD_PIN_SEND` | `0x41000005` | 1090519045 | PIN send (od MB!) |
| `CMD_START_ACK` | `0x41000020` | 1090519072 | START ACK |
| `CMD_ERR_ACK1` | `0x10000001` | 268435457 | Error ACK |
| `CMD_ERR_ACK2` | `0x10000002` | 268435458 | Error ACK |
| `CMD_ERR_ACK7` | `0x10000007` | 268435463 | Error ACK (wysyłany przez ESP) |
| `CMD_SUPERVISION` | `0x20000002` | 536870914 | ★ Supervision — MB wyłącza zasilanie |
| `CMD_FRAME_ERROR` | `0x15000001` | 352321537 | Błąd ramki (U16 zgłasza złe CRC) |

### Katalog komend wg prefiksu (kierunki zweryfikowane krzyżowo)

| Prefiks | Kierunek | Opis |
|---------|----------|------|
| `0x10xxxxxx` | ESP→MB | Error ACK |
| `0x15xxxxxx` | MB→ESP | Błąd ramki (U16 zgłasza złe CRC) |
| `0x20xxxxxx` | **MB→ESP** | Power/action notifications (power-on, ready) |
| `0x22xxxxxx` | **ESP→MB** | Czujnik deszczu (sensor na display board J4→ESP32 GPIO36) |
| `0x30xxxxxx` | **ESP→MB** | Keepalive, WiFi/BT status, settings, poll |
| `0x31xxxxxx` | ESP→MB | Settings menu control |
| `0x33xxxxxx` | **MB→ESP** | Device info, status, wersje, harmonogram, wyniki PIN |
| `0x40xxxxxx` | Oba | System: 0x40000001/04/06 = ESP→MB, 0x40000008+ = MB→ESP |
| `0x41xxxxxx` | **MB→ESP** | Lock, exec_action, error, shutdown, start_ack, home, docked — **z wyjątkiem 0x41000005 (PIN_SEND = ESP→MB)** |
| `0x50xxxxxx` | **MB→ESP** | Bateria |

> **Korekta**: Poprzednia wersja tej tabeli miała odwrócone kierunki dla `0x20`, `0x22`, `0x30`, `0x33`, `0x41`, `0x50`.
> Patrz PROTOCOLS.md dla pełnej dokumentacji z krzyżowym potwierdzeniem z captures i firmware.

### Stany MB (pole `state` w `0x330000A0`)

| Wartość | Znaczenie |
|---------|-----------|
| 0 | Idle (przed PIN) |
| 1 | Ready (po PIN) |
| 2 | MOWING (lub jazda) |
| 6 | Stop / pauza |
| 7 | Error |
| 9 | RETURNING TO DOCK |
| 10 | CHARGING |
| 11 | CHARGING (alternatywny) |

### Dodatkowe pola w statusie

- `station` — wykryto stację ładującą (bool)
- `border_state` — kabel ograniczający (0/1)
- `stop_state` — przycisk STOP (0/1)
- `rain_state` — deszcz (0/1)
- `bat_per` — poziom baterii (0-100)
- `bat_lv` — poziom baterii (0-3)
- `error` — kod błędu
- `work_area`, `cut_area` — powierzchnia
- `total_minutes`, `on_minutes` — czasy
- `bat_health` — zdrowie baterii
- `bat_ctime`, `bat_dtime` — czasy ładowania
- `rain_delay` — opóźnienie deszczu

---

## 3. Ewolucja sekwencji bootowania

### Log 1 (`kosiarka-logs (1).txt`)

**Data**: 2026-06-21 16:33

**Co testowano**: Pierwsze uruchomienie z komponentem `snk_mower`. ESP wysyła tylko `CMD_ESP_POLL` (0x300000A1) co ~30ms i `CMD_ESP_KEEPALIVE` (0x30000005) co ~1s. Brak sekwencji bootowej.

**Wynik**: ESP wysyła, ale MB nie odpowiada (cisza). Brak reakcji MB. Log zawiera 1583 linii TX.

**Wnioski**: Potrzebna jest sekwencja bootowa — samo POLL nie wystarcza.

### Log 2 (`kosiarka-logs (2).txt`)

**Data**: 2026-06-22 ~09:52

**Co testowano**: Dodano sekwencję bootową (BOOT → KEEPALIVE → STATE → RAIN → INIT). ESP czeka 5s, potem wysyła boot_seq.

**Wynik**: Brak wyraźnych odpowiedzi MB w logu.

### Log 5 (`kosiarka-logs (5).txt`)

**Data**: 2026-06-22 ~11:50

**Co testowano**: Kolejne iteracje sekwencji bootowej.

**Wynik**: Nadal brak odpowiedzi MB.

### Log 6 (`kosiarka-logs (6).txt`)

**Data**: 2026-06-22 19:02

**Co testowano**: Pierwsza udana komunikacja! Sekwencja:
1. boot delay (200ms POLL)
2. Boot seq: BOOT → KEEPALIVE → STATE → RAIN=1 → INIT
3. Czekanie na DEVICE_INFO przez ~2s
4. DONE → wysłanie PIN
5. Odebrano `RX: 0x300000A1` — MB zaczyna wysyłać POLL!
6. Odebrano `Boot ACK` (0x40000021)
7. Odebrano `Power ON (action=0)` (0x20000001)
8. Odebrano DEVICE_INFO

**Kluczowe zdarzenia** (timestamp):
```
19:02:57.079 Boot: sending BOOT + KEEPALIVE + STATE + RAIN
19:02:57.195 Boot sequence sent — waiting for DEVICE_INFO
19:02:57.203 Power ON (action=0)          ← MB odpowiada!
19:02:58.841 RX: 0x300000A1               ← MB wysyła POLL
19:02:59.084 DEVICE_INFO not received — entering SYNC anyway
19:02:59.588 Boot DONE — switching to keepalive mode
19:02:59.591 Sending PIN to mainboard
19:03:01.166 Boot ACK                     ← MB potwierdza
19:03:01.240 Boot ACK
19:03:01.295 Device: MyMower (...)        ← DEVICE_INFO odebrany!
```

**Problem**: Watchdog (ESP32 component watchdog) zabija ESP. Pętla `loop()` trwa ~119ms z powodu `delay()` w `send_boot_sequence_next()` (wysyłanie wszystkich 7 wiadomości naraz w jednym wywołaniu loop). Limit ESPHome to 50ms.

### Log 7 (`kosiarka-logs (7).txt`)

**Data**: 2026-06-22 19:07

**Co zmieniono**: Zwiększono interwał POLL z 30ms na 200ms podczas boot_delay, żeby watchdog nie zabił ESP.

**Wynik**: Watchdog nie zabija (POLL co 200ms zamiast 30ms = mniej TX), ale MB wyłącza się po ~6s:
```
19:07:37.762 Boot sequence sent
19:07:41.796 Boot ACK
19:07:41.817 Boot ACK
19:07:41.963 Device: MyMower
19:07:43.991 Map: area=300
19:07:44.010 SUPERVISION: MB sent 0x20000002 — power may be cut soon
```

**Wnioski**: POLL co 200ms jest za wolne — MB ma timer nadzoru (supervision timer) który wymaga odpowiedzi co ~30-50ms. MB wysyła `0x20000002` (SUPERVISION) i wyłącza zasilanie.

### Log 8 (`kosiarka-logs (8).txt`)

**Data**: 2026-06-22 19:30

**Co zmieniono**: Dodano 10× POLL w burst (co 30ms) po boot_seq, żeby MB nie uznało że ESP padł.

**Wynik**: Nadal SUPERVISION. Ustalono że problem to nie watchdog (zasilanie MB jest odcinane, nie reboot ESP):
```
19:31:00.069 Boot sequence sent
19:31:04.015 Boot ACK
19:31:04.038 Boot ACK
19:31:04.219 Device: MyMower
19:31:06.210 Map: area=300
19:31:06.225 SUPERVISION: MB sent 0x20000002
```

### Log 9 (`kosiarka-logs (9).txt`)

**Data**: 2026-06-22 19:48

**Co zmieniono**: Zmieniono kolejność bootowania — ESP czeka aż MB zainicjuje komunikację (zgodnie z PROTOCOLS.md). ESP wysyła tylko KEEPALIVE i POLL, nie wysyła boot_seq dopóki MB nie zacznie.

**Wynik**: MB całkowicie cicha — nie wysyła nic. Po 15s boot_delay ESP i tak wysyła boot_seq, MB odpowiada, ale potem znowu SUPERVISION po ~3s.

### Log 10 (`kosiarka-logs (10).txt`)

**Data**: 2026-06-22 19:55

**Co zmieniono**: Cofnięto do wersji ESP-inicjującej (tak jak działało w log 6-8). ESP wysyła boot_seq od razu po boot_delay.

**Wynik**: **MB całkowicie cicha** — zero odpowiedzi, zero RX. ESP wysyła POLL co 200ms przez cały 30s boot_delay, potem boot_seq — i nic. Log ma 2467 linii — wszystkie to TX.

**Hipoteza**: Coś się zepsuło w komunikacji między programowalnymi uploadami — może ESP było programowane podczas gdy MB była włączona i zresetowała się, lub rejestry UART się rozjechały.

### Log 11 (`kosiarka-logs (11).txt`)

**Data**: 2026-06-22 20:06

**Co zmieniono**: Powrót do ESP-inicjującej kolejności. Zablokowano wysyłanie RAIN w boot_seq (usunięto) i zmieniono `rain=1` na `rain=0`. Non-blocking boot sequence (jedna wiadomość na loop). Timeout DEVICE_INFO zwiększony do 10s.

**Wynik**: **MB odpowiada!** Sekwencja:
1. 15s boot_delay (POLL co 200ms)
2. Boot seq wysłana (z `rain=1`) — ale tym razem jedna wiadomość na iterację loop (non-blocking)
3. Warning: `component took 121ms, max is 50ms` — mimo non-blocking, nadal problem z czasem
4. DEVICE_INFO timeout (2s)
5. PIN wysłany (`pwd:9633`)
6. RX od MB: POLL echo (0x300000A1)
7. Boot ACK ×2
8. DEVICE_INFO odebrany (MyMower, RMC300E20V-ECDNSS, S/N=..., v=31018, pwd_en=1)
9. HW versions: MB hv=22500 sv=31315, BB hv=230500 sv=50003, DB hv=60400 sv=30202, MBLT sv=50517
10. Battery bars: 3

**Log kończy się po ~10s normalnej pracy** — tylko TX POLL. Brak SUPERVISION 0x20000002 w logu 11! Log jest ucięty (449 linii).

**Obserwacja**: Sekwencja bootowa nadal zawiera `rain:1` (linia 180). W commitach widzę że później zmieniono na `rain:0` (commit `13f98d4` — "Send rain=0 in boot sequence instead of rain=1").

**Problem 121ms**: Non-blocking boot sequence (jedna wiadomość na loop) dalej robiła `delay(5)` w `send_boot_sequence_next` dla ESP_INFO (wysyła jedno, potem zapis do flash), albo blokada pochodziła z `send_pin()` z `delay()`.

### Aktualny stan (po log 11)

W najnowszym kodzie (po commitach):
- Non-blocking boot sequence (jedna wiadomość na `loop()`)
- 30s boot_delay dla bezpieczeństwa OTA
- POLL co 200ms podczas boot_delay
- POLL co 30ms po przejściu do DONE
- DEVICE_INFO timeout: 10s
- `rain=0` w boot sequence (commit `13f98d4`)
- `compat_mode` — wysyła `wifi=0`, `str=0` (zgodnie z oryginalnym firmware)
- Znacznie zredukowany log spam

---

## 4. Zagadka wyłączania zasilania MB

### Objawy
MB wysyła `0x20000002` (SUPERVISION) i odcina zasilanie ~3-6s po wysłaniu PIN/odebraniu DEVICE_INFO. Dzieje się to we wszystkich logach 6-9.

### Możliwe przyczyny

#### A. Zbyt wolny POLL (najbardziej prawdopodobne)
MB wymaga `CMD_ESP_POLL` (0x300000A1) co ~30-50ms. Gdy interwał wynosi 200ms, timer nadzoru MB wyzwala SUPERVISION:
- Log 6-7: POLL 200ms → ESP działa (watchdog OK), ale MB wyłącza
- Log 8: dodano 30ms burst po boot_seq → nadal wyłącza

**Kontrargument**: W log 8/9, nawet po przejściu na 30ms POLL w DONE, MB się wyłącza. Może chodzi o przerwę między bodźcem a odpowiedzią — MB wysyła POLL, ESP odpowiada swoim POLL. Jeśli odpowiedź ESP przyjdzie za późno, MB uznaje że ESP nie żyje.

#### B. `rain=1` w boot sequence
W boot_seq_ ESP wysyła `{"cmd":570425344,"rain":1}`. To może być odebrane jako "jest deszcz" → MB wyłącza się (oszczędność baterii przy deszczu). W oryginalnym firmware deszcz jest raportowany przez MB do ESP, nie na odwrót.

**Commit `13f98d4`**: zmieniono na `rain:0` — to może rozwiązać problem.

#### C. Odrzucenie PIN
Jeśli PIN jest niepoprawny, MB może się wyłączyć po 3-5 próbach. W logach:
- PIN wysłany (`{"cmd":1090519045,"pwd":9633}`)
- Brak odpowiedzi `PIN_RESULT` (0x33000021) w logach 6-8 — dopiero w log 11 widać DEVICE_INFO po PIN
- PIN może być sprawdzany asynchronicznie, a MB może wyłączyć jeśli PIN nie przejdzie

**Kontrargument**: W log 7 widać że PIN_RESULT (0x33000021) nie pojawia się, ale MB i tak wyłącza. W log 11 MB nie wyłącza się w czasie logowania.

#### D. Brak odpowiedzi na zapytania MB
MB wysyła zapytania (WiFi/BT status, ESP info), a ESP może nie odpowiadać w odpowiednim czasie. W oryginalnym firmware:
- `CMD_ESP_WIFI` i `CMD_ESP_BT` są wysyłane przez MB → ESP odpowiada
- Jeśli ESP nie odpowie, MB może uznać że ESP jest martwy

#### E. CMD_BATTERY
MB oczekuje `CMD_BATTERY` (0x50000021) od ESP. Kod wysyła go tylko przy starcie w boot_seq (w oryginalnym firmware MB wysyła `bat:x`, a ESP odpowiada). W implementacji ESP nie wysyła `CMD_BATTERY` podczas normalnej pracy.

### Stan: Nierozwiązane

Aktualna hipoteza: **rain=1** w boot sequence jest najbardziej podejrzane. Zostało zmienione na `rain=0` w commicie `13f98d8`. Kolejne testy powinny zweryfikować czy to rozwiązuje problem.

---

## 5. Aktualny stan implementacji

### Działa
- ✅ Boot sequence: BOOT → KEEPALIVE → STATE(0) → RAIN(0) → WIFI → ESP_INFO → INIT
- ✅ DEVICE_INFO — pełny parsing (name, model, sn, version, bat_name, pwd_en)
- ✅ HW versions — parsing (mb_hv, mb_sv, bb_hv, bb_sv, db_hv, db_sv, mblt_sv)
- ✅ PIN sending (`pwd:9633`)
- ✅ POLL/KEEPALIVE w normalnej pracy (30ms / 1s)
- ✅ RX parser — obsługa JSON z `{`..`}`, string state, CRC verification
- ✅ CRC8-Dallas — poprawny dla ramek `&{json}<CRC>##`
- ✅ Wyświetlacz — 4-digit 7-segment (SPI, 3× 74HC595, 2MHz, hardware timer)
- ✅ Display auto-off po czasie bezczynności
- ✅ State display cycling: text ↔ battery co 5s
- ✅ Shutdown display ("byE ")
- ✅ Error display ("E" + kod)
- ✅ Buzzer (GPIO27)
- ✅ Czujnik deszczu (GPIO36) z wysyłaniem `0x22000000` co 60s
- ✅ Rain config, schedule, multizone, map_cfg parsing
- ✅ Compat mode (wifi=0, str=0)
- ✅ sensor/binary_sensor/text_sensor publikacja do HA
- ✅ Boot delay (30s domyślnie, configurowalny)
- ✅ Non-blocking boot sequence (jedna wiadomość na loop)
- ✅ 10s timeout na DEVICE_INFO

### Nie działa / problematyczne
- ❌ **MB wyłącza się po boot_seq** (SUPERVISION 0x20000002) — prawdopodobnie przez `rain=1`, weryfikacja po zmianie na `rain=0`
- ❌ **Normalna operacja** — ESP nie może osiągnąć stanu "ustabilizowanej komunikacji" na tyle długo by MB przeszło w normalny tryb
- ❌ `start_mowing()` — wysyła TRIM schedule + error ACK + state=2, ale MB ignoruje (brak komendy do fizycznego startu — START jest tylko przez przycisk na U16)
- ❌ `return_to_dock()` — wysyła CMD_RETURN_HOME, ale mower pozostaje w idle
- ❌ Colon na wyświetlaczu — bit nieznaleziony (TODO w `set_display_text`)
- ❌ ESP nie odpowiada na `CMD_ESP_WIFI` / `CMD_ESP_BT` w odpowiednim czasie bo nie trackuje zapytań MB

---

## 6. Kluczowe szczegóły techniczne

### CRC8-Dallas (MAXIM)

Polynomial: `0x31` (x⁸ + x⁵ + x⁴ + 1)

```python
python3 -c "import crcmod; c=crcmod.mkCrcFun(0x131); print(hex(c(b'{\"cmd\":1073741828}')))"
```

W kodzie: tablica lookup 256 elementów, funkcja `dallas_crc8(data, len)`.

CRC liczony jest z bajtów JSON (pomiędzy `&` a `<CRC>`). Nie obejmuje prefiksu `&` ani suffixu `##`.

### Framing JSON

```
&{json}<CRC>##
```

- Prefiks: `0x26` (`&`) — oznajmia początek ramki
- JSON: dowolny JSON z polem `cmd` (uint32)
- CRC: 1 bajt
- Terminator: `0x23 0x23` (`##`) — dwa znaki `#`

RX parser:
- Szuka `{` aby rozpocząć buforowanie JSON
- Śledzi `rx_in_string_` dla prawidłowej obsługi cudzysłowów w stringach
- Na `}` kończy JSON i deserializuje
- Nie sprawdza CRC w RX (bufory LA pokazują że U16 już zweryfikowało i odrzuciło błędne ramki)

### Wyświetlacz

- 3× 74HC595 w kaskadzie (24-bit shift register)
- SPI bit-banging → hardware SPI (ESP-IDF SPI2, 2MHz)
- Format: 24 bity na digt: `[b0: 8b] [b1: 8b] [segments: 8b]`
- Adresowanie digtów (`b1`): `{0x20, 0x10, 0x08, 0x04}` (digty 0-3 od lewej)
- Segmenty: standardowa mapa bitów dla 7-segment (dp, g, f, e, d, c, b, a)
- `b0`: zawsze `0x00` (może odpowiadać za colon/DP dolnych digtów — TODO)
- CS (GPIO32) pełni rolę OE — trzeba go opuścić na czas transmisji, podnieść po
- Timer: hardware timer esp_timer, callback co 8ms (125 Hz odświeżania)
- Każdy digt świeci przez 2ms (8ms / 4 digty)
- Sekwencja startowa: `8888` → `boot` → stan

### BootPhase state machine

```
PRE ──→ DONE
```

**PRE**:
1. **boot_delay** (jeśli > 0): tylko POLL co 200ms + KEEPALIVE co 1s — czeka na OTA
2. **boot_seq**: jedna wiadomość na loop (non-blocking, co 8ms):
   - 0: BOOT (0x40000004)
   - 1: KEEPALIVE (0x30000005)
   - 2: STATE (0x30000028, state=0)
   - 3: RAIN (0x22000000, rain=0)
   - 4: WIFI + BT (0x30000021 / 0x30000022)
   - 5: ESP_INFO (0x40000006, hv=60400, sv=30202, mac)
   - 6: INIT (0x40000001, init=3)
3. **wait for DEVICE_INFO**: POLL co 30ms, timeout 10s

**DONE**:
- PIN (jeśli nie wysłany i pwd_en)
- POLL co 30ms (wymóg MB supervision)
- KEEPALIVE co 1s
- Rain read co 60s

### Wymagania MB supervision

- MB oczekuje `CMD_ESP_POLL` (0x300000A1) co ~30-50ms
- Jeśli POLL nie przyjdzie w czasie ~50-100ms, MB wysyła `0x20000002` (SUPERVISION) i wyłącza zasilanie
- Podobnie dla KEEPALIVE (0x30000005) co ~1s
- MB monitoruje też odpowiedzi na swoje zapytania (WiFi, BT, ESP_INFO)

### Boot delay (30s)

- Skonfigurowany na 30s w YAML (`boot_delay: 30`)
- W tym czasie ESP wysyła tylko POLL i KEEPALIVE, nie boot_seq
- Celem jest umożliwienie OTA — flashowanie ESP podczas gdy MB jest włączona
- MB może się wyłączyć w czasie boot_delay (brak szybkiego POLL), ale to akceptowalne ryzyko

---

## 7. Wszystkie commity git

### Faza 1: Reverse-engineering i dokumentacja sprzętu

| Hash | Opis |
|------|------|
| `3121e1f` | Complete hardware & firmware documentation for Lux Tools A-RMR-300-24 |
| `82bc517` | Add firmware dumps (U13 512KB, U16 256KB) |
| `ec38024` | Add ESP32 firmware dump (4 MB) and analysis |
| `bd3ec0a` | Add decompilation setup documentation and USB analysis script |
| `c71e95f` | Update docs: J8 inter-board pinout, button fork, UART on ribbon, not J1 |
| `93acdf6` | Update docs: J8 inter-board pinout, button fork, UART on ribbon, not J1 |
| `7295b07` | Add comprehensive firmware analysis results |
| `a45d29c` | Add ghidra-cli OSGi fix patch |
| `1333b36` | Fix PIN analysis: 4-digit PIN stored on EEPROM U22, not ESP32 |
| `aa94cfd` | Add PIN system analysis, FORMATFLASH.json dead code, security assessment |
| `7b91c7a` | EEPROM dumping via SWD: PendSV VTOR trick + FPU fix |
| `09f42df` | repo cleanup: organize files into subdirs, add PIN recovery README |
| `a56e3cb` | Translate remaining Polish documentation to English |
| `f9d0723` | Remove dcd059.pdf, move decompile_usb.py to tools/ |
| `ace14a0` | Fix FORMATFLASH.json (confirmed working via MBTL wildcard) |
| `022f666` | Add note about PCB protective coating on EEPROM |
| `c162784` | Add firmware and EEPROM dumps to repo |
| `a55f454` | ESP32 GPIO pin analysis: literal pool candidates, Xtensa decompilation workflow |
| `9a84118` | Add PROTOCOLS.md: inter-chip communication cross-validation |
| `3d7ceb0` | doc: add schedule configuration guide from manual (PL) |
| `781b69c` | Add PCB docking station photo |
| `95dd2e4` | Verifikowana mapa GPIO ESP32 na podstawie analizy ścieżek PCB |
| `59f7072` | Xtensa disassembly: full objdump listing, GPIO pin mapping confirmed |
| `f9848db` | ESP32 GPIO analysis: confirmed MOSI=12 SCLK=10 MISO=NC |
| `e598d93` | ESPHome custom component + HA design + sniffing plan |
| `22e68f6` | ha.md: add schedule/data-availability section, final KV-store confirmation |

### Faza 2: Pierwsze próby ESPHome component

| Hash | Opis |
|------|------|
| `171ed4e` | snk-mower.yaml: add wifi_signal and uptime sensors |
| `de34e6c` | switch to external_components, drop unused spi dependency |
| `d47426f` | remove text_sensor dependency (not available in ESPHome 2026.6.0) |
| `248e169` | fix for ESPHome 2026.6.0: drop designated init, use ESP-IDF GPIO directly |
| `be4bf2c` | feat: response timeout, buzzer, display auto-off |
| `ce7256d` | refactor: simplify component code |
| `a2640de` | Add UART sniffing captures and JSON protocol analysis |
| `a3b2826` | Rewrite component for JSON protocol at 230400 baud |
| `18864b5` | Expand protocol support based on decompilation and captures |
| `52c7649` | fix: add AUTO_LOAD for text_sensor dependency |
| `2c1042d` | fix: use ESPHome json component and ArduinoJson v7 API |
| `3e432d7` | fix: compilation errors - use ESPHome APIs and correct types |

### Faza 3: UART i boot sequence

| Hash | Opis |
|------|------|
| `6f031cd` | minimal config: wifi + ota + api only |
| `ce590c4` | add uart (rx tx) without custom component |
| `96fd9eb` | full config with snk_mower, web_server, ota |
| `019e6ee` | step 2: add uart + snk_mower component with all sensors |
| `1fd2e4b` | add sram1_as_iram and minimum_chip_revision |
| `52e4e89` | add ESP_POLL (0x300000A1) sending every 100ms, raw RX byte logging |
| `a20f1ff` | uart: try rx=GPIO14 (decompilation candidate) |
| `5ab11d8` | uart: try tx=14,rx=15; display: clk=10,mosi=12,cs=15 (decompilation) |
| `4c21e0d` | display: set pins to -1 (NC) to avoid GPIO15 conflict with UART RX |
| `cc91517` | display: remove pin overrides (use defaults 18/23/5) |
| `c6e54df` | uart: try 115200 with tx=15,rx=13 (HARDWARE.md) |
| `a9b531a` | add pin_diag: true — scan 26 GPIOs every 20ms |
| `c9bfc7e` | fix: add sensor/binary_sensor to AUTO_LOAD |
| `24403a2` | fix: move BOOT/INIT/INFO/STATE sequence to setup() instead of loop() |
| `bce8f2f` | fix: send boot init=2 (not init=3), add 2s delay before POLL/KEEPALIVE |
| `6134038` | fix: match original ESP boot order: BOOT→KEEPALIVE→STATE, send INFO at 5s |
| `b3a6150` | fixed frame format: &JSON{CRC}#, CRC-8 MAXIM, boot sequence cleanup |
| `fc5378f` | fix: remove VERBOSE RX byte log, limit 256 bytes/loop |
| `827a064` | fix: RX parser tracks string state, POLL co 200ms |
| `0e0e873` | reorganize repo: per-processor dirs, clean root, English docs |
| `5897b94` | fix: U16 is not a simple UART bridge, update descriptions |
| `927cc8a` | add note about hidden WiFi/BT in all SNK mowers |
| `05cb6b8` | update ha.md: remove stale RX analysis, document current protocol/CRC/issues |
| `6170e24` | fix snk_mower boot sequence: correct POLL/KEEPALIVE pattern, add pinout docs |
| `d2b8ea0` | fix: don't reset SYNC burst on repeated DEVICE_INFO, remove 1s delay |
| `7fac4c9` | fix: revert CMD_DEVICE_INFO to 0x330000A1, add CMD_MB_DEVICE_INFO 0x330000A9 |
| `21e4c3f` | fix: correct CMD_DEVICE_INFO (0x330000A1→0x330000A9), add KEEPALIVE |
| `446a6a4` | update YAML header comments with verified PCB wiring |

### Faza 4: Wyświetlacz i buzzer

| Hash | Opis |
|------|------|
| `3a3bbd9` | boot handshake works! system stable 30s+. cleanup, GPIO27 buzzer |
| `50b1c55` | temp: test GPIO2 as buzzer |
| `1025b10` | fix: cast buzzer_pin/rain_pin to gpio_num_t |
| `fe53ad7` | remove periodic buzz(80) from PIN accepted |
| `887d628` | docs: buzzer confirmed on GPIO27 |
| `38c3ed3` | fix GPIO32→R31, GPIO33→R33, GPIO27 buzzer path unknown |
| `ab94e34` | docs: GPIO27 buzzer via via internal layer |
| `757220b` | pin_diag: scan all GPIOs 0-39 |
| `2427fa6` | remove pin_diag, return to normal operation |
| `1365262` | pin_diag: skip flash pins 6-11 |
| `220ee7a` | add boot_delay config: delays handshake for OTA safety window |
| `2031f9f` | boot_delay: 10s default |
| `e4cbc1b` | docs: MB supervisor timer and boot_delay workaround |
| `e15e97c` | docs: GPIO36 = rain sensor confirmed by wet finger test |
| `b6d156b` | docs: display pins unconfirmed, add empirical corrections |
| `7ab3a84` | add lcd_find: auto-scan 7 candidates x 3 roles = 210 combos |
| `31ef84c` | display confirmed: CLK=5, CS=34, MOSI=32 via lcd_find |
| `88a26fa` | lcd_find: exclude input-only GPIO34/39, fix CLK=5 MOSI=32 |
| `bc50f42` | external_components: refresh: 0s |
| `e7654ba` | lcd_find: quick 6-permutation scan of pins {18,33,32} |
| `18344e1` | lcd_find: explicit 6-permutation test, no index math |
| `ba250b3` | fix: remove orphaned total reference |
| `b7e4f62` | fix: add delays in shift24 bit-banging |
| `70bac18` | fix: lcd_find now exits cleanly to handshake, display pins {5,32,25} |
| `e681887` | feat: add lcd_sweep mode — single-bit sweep |
| `473937` | snk_mower: add per-phase logging to LCD sweep, slow to 8ms |
| `7459de8` | snk_mower: inverted CS polarity |
| `606ba0d` | snk_mower: add lcd_find_rclk mode |
| `6125972` | snk_mower: RCLK test falling edge |
| `a9aa55f` | snk_mower: RCLK test with transparent mode |
| `fe7c5a7` | HARDWARE: OK button confirmed on GPIO19 |
| `d695f60` | HARDWARE: remove unconfirmed button GPIO mappings |

### Faza 5: Display optimization

| Hash | Opis |
|------|------|
| `615afc5` | snk_mower: expand pin_diag scan — add GPIO0 |
| `48a5100` | snk_mower: display pattern test |
| `942c865` | ha.md: update with pattern test results |
| `7fb3e55` | snk_mower: replace pattern test with minimal glitch test |
| `a360c1a` | document button GPIO/ADC experiments |
| `77aad1e` | snk_mower: replicate v3 — exact shift24(FFFFFF) |
| `6d9b54c` | snk_mower: LCD listen mode |
| `33046a5` | snk_mower: restore 210-combo lcd_find test |
| `2d337b8` | ha.md: update with 210 test results |
| `4aaa98f` | ha.md: add EE:EE result |
| `a979fa9` | snk_mower: use shift24_nocs (no CS toggle) |
| `ddf91b4` | Add MAX7219 test mode |
| `b09f898` | Update YAML: chip_rev 3.1, sram_as_iram, DEBUG log |
| `ff4460d` | snk_mower: fix display driver latching, set definitive GPIO33/25/32 |
| `be19270` | docs: document successful display sweep results |
| `deb660a` | feat: implement high-resolution hardware timer for display |
| `834422b` | feat: boot display, battery on three right digits, error E+code |
| `9c12d2a` | chore: cleanup repo |
| `322d7fc` | fix: send POLL during boot_delay to prevent watchdog kill |
| `b8168af` | fix: fix UART RX deadlock during boot_delay |
| `3dd5262` | chore: clean up redundant YAML files |
| `ca7c0f2` | feat: increase boot_delay to 30s |
| `588393d` | feat: implement split digit select (b0/b1) |
| `b4ff3cd` | feat: complete display layout with symmetrical mapping |
| `d6cf72e` | feat: correct digit mapping with symmetric 0x02/0x04 |
| `67da34e` | feat: implement 16-step DIAG SWEEP test |
| `077254f` | feat: implement safe multiplexed DISPLAY TEST |
| `220810a` | feat: implement targeted B0 SWEEP |
| `b8ca378` | fix: correct display orientation |
| `73554a8` | feat: LEFT SIDE SWEEP |
| `149ab93` | feat: complete digit mapping — all 4 digits |
| `1036fea` | fix: correct DIGIT_B1_MAP order |
| `0368db7` | docs: update DIGIT_B1_MAP order |
| `bcaf301` | feat: COLON SWEEP |
| `d9445cc` | feat: DP test — show decimal points |
| `a4d7279` | ha.md: colon/DP sweep results |
| `08a6357` | DP SWEEP: test all 8 segment bits |
| `a118697` | DP SWEEP: show 8 + sweep b0 (U4) bits |
| `2a9b105` | ha.md: DP segment sweep result |
| `5f2847a` | DP investigation concluded |
| `b45126e` | ha.md: update status, display summary |
| `17c2c13` | snk_mower: remove GPIO/LCD discovery code |
| `0ae836c` | snk_mower: show 'byE' on display at shutdown |
| `8f4d335` | snk_mower: keep 'byE' for 3s during shutdown |
| `119d077` | snk_mower: idle display cycles IdLE 5s ↔ battery 5s |
| `303c49e` | snk_mower: cycle text↔battery on all non-error states |
| `f90d008` | fix: don't reset display cycle on every STATUS message |
| `ed30fd6` | fix boot handshake, implement start_mowing/send_trim |
| `9e4e008` | ha.md: update experiment results |
| `30e4e22` | ha.md: return_to_dock result |

### Faza 6: Akcje i komendy (dodatkowe eksperymenty)

| Hash | Opis |
|------|------|
| `644e9d2` | compat_mode + fix compile errors + capture 04 analysis |
| `1ad3c2b` | remove periodic send_esp_state entirely |
| `94a38de` | add POLL in DONE phase; remove wifi/bt from PRE phase |
| `204203f` | document all experiments in ha.md |
| `f038818` | fix PIN sequence: send PIN_SEND only after PIN_RESULT prompt |
| `25b0c42` | LA captures: full UART protocol decoded |
| `e43d408` | 4th capture: docking sequence |
| `e05cf20` | docs: update PROTOCOLS.md and ha.md with LA capture findings |
| `fc2af6c` | ha.md: add LA capture analysis conclusions |
| `86fd1ac` | ha.md: add hardware notes - ON pin sensitivity |
| `2744cbd` | fix: boot→idle transition, missing DEVICE_INFO handling |
| `4c2b5cf` | snk_mower: fix boot sequence and deduplicate device info |
| `ddfbff1` | snk_mower: add missing member variables for device info |
| `515f54b` | snk_mower: increase CMD_POLL interval from 30ms to 200ms |
| `c6a4329` | snk_mower: add 30ms×10 poll burst after boot_seq |
| `d3d0473` | snk_mower: restore 30ms polling for normal operation |
| `33360ba` | snk_mower: fix boot sequence order — wait for MB first |
| `08e9149` | Revert to ESP-initiated boot order |
| `22a1a08` | Non-blocking boot sequence, reduced log spam, 10s DEVICE_INFO timeout |
| `13f98d4` | Send rain=0 in boot sequence instead of rain=1 |

---

## 8. Schemat architektury

```
┌─────────────────────────────────────────────────────────────────────┐
│ ESP32-WROOM-32UE (płyta wyświetlacza)                               │
│                                                                      │
│  GPIO17 ──TX──┐                                          ┌────────┐ │
│  GPIO16 ──RX──┤  UART 230400 8N1                        │Wyświet.│ │
│                │  JSON &{cmd}<CRC>##                      │ 7-seg  │ │
│  GPIO33 ──CLK─┤  SPI (2MHz)  ────────────────────────────┤ 4-digt │ │
│  GPIO25 ──MOSI┤                                            │ 74HC595│ │
│  GPIO32 ──CS──┤                                            └────────┘ │
│  GPIO27 ──────┤ Buzzer                                                │
│  GPIO36 ──────┤ Czujnik deszczu                                      │
│  GPIO19 ──────┤ Przycisk OK (do U16)                                 │
│                                                                      │
└──────────────────┬───────────────────────────────────────────────────┘
                   │ JSON @ 230400
                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│ U16: GD32F303CGT6 (FreeRTOS)                                        │
│  → Pomost JSON ESP32 ↔ U13                                          │
│  → Obsługuje przyciski (START/STOP/HOME)                            │
│  → Czujniki własne (deszcz, border coil, światło)                   │
│  → max 128 bajtów/ramkę                                             │
│  → EasyLogger v2.2.99                                               │
└──────────────────┬───────────────────────────────────────────────────┘
                   │ JSON @ 230400
                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│ U13: GD32F305AGT6                                                    │
│  → Sterowanie silnikami                                             │
│  → KV-store (EEPROM U22 — PIN, konfiguracja)                        │
│  → cJSON parser                                                     │
│  → RTC (tylko do wyświetlania, nie harmonogramu)                    │
└─────────────────────────────────────────────────────────────────────┘
```

### Uwagi architektoniczne

- **ESP nie może fizycznie uruchomić koszenia** — nie ma komendy UART do START. START/STOP/HOME to fizyczne przyciski podpięte do U16. ESP tylko otrzymuje notyfikacje stanu.
- **U16 nie jest prostym pomostem UART** — dodaje własne wiadomości (czujniki), agreguje dane z U13, ma własną logikę.
- **U13 paruje JSON przez cJSON** — potwierdzone w stringach firmware.
- **OTA**: Cloud → ESP32 → U16 → U13 (ten sam kanał UART, inne ramkowanie: `[2B length LE][N bytes][1B XOR checksum]`).

---

## 9. Wnioski i dalsze kroki

### Co wiemy na pewno
1. Protokół to JSON z CRC8-Dallas, `&` prefix, `##` suffix, 230400 8N1
2. Sekwencja bootowa jest wymagana aby MB zaczęła odpowiadać
3. MB wymaga POLL co ~30ms — timer nadzoru
4. PIN (4-cyfrowy) jest przechowywany w EEPROM U22, weryfikowany przez U13
5. Wyświetlacz to 3× 74HC595, SPI, 24-bit na digt

### Co jest niepewne
1. **Dlaczego MB wyłącza się po boot_seq** — hipoteza: `rain=1` w boot sequence. Zmieniono na `rain=0`.
2. **Czy po `rain=0` MB zostanie włączona** — do zweryfikowania w kolejnym teście.
3. **Czy potrzebne są dodatkowe odpowiedzi na zapytania MB** — WiFi/BT status, ESP_INFO.
4. **Czy MB oczekuje CMD_BATTERY od ESP** — w LA widać że MB wysyła `bat:x`, ESP odpowiada.

### Priorytet dalszych prac
1. Przetestować z `rain=0` w boot sequence (commit `13f98d4`) — czy MB pozostaje włączona
2. Jeśli tak: dodać odpowiedzi na `CMD_ESP_WIFI` / `CMD_ESP_BT` w trybie normalnym
3. Jeśli nie: zbadać czy brak `CMD_BATTERY` powoduje SUPERVISION
4. Znaleźć bit colona na wyświetlaczu (b0, U4)
5. Zrozumieć format godzinowy RTC (0x40000011) — synchronizacja czasu
