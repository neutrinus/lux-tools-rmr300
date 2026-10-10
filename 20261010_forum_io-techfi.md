# Wątek io-tech.fi o Brucke RM500/RM501/RM800 — wnioski dla Lux RMR300 (2026-10-10)

Źródło: [Brucke RM500/RM501/RM800 robottiruohonleikkurin infopaketti](https://bbs.io-tech.fi/threads/brucke-rm500-rm501-rm800-robottiruohonleikkurin-infopaketti.405186/),
47 stron, 07.2022 – 09.2026, po fińsku. Przeczytane wszystkie strony.

Oznaczenia jak w [`FIRMWARE_MAP.md`](FIRMWARE_MAP.md): **[F]** sprawdzone w naszym firmware,
**[W]** tylko z forum, **[I]** wniosek, niezweryfikowany.

## Najważniejsze

1. **Forum nie ma nowszego firmware dla naszej kosiarki.** Pliki Brucke są ze starszej generacji
   i nasz bootloader by ich nie przyjął (§2). Wyjątek: plik ESP32 `SNK_DB_*` mógłby zostać
   przyjęty i nadpisać ESP32 inną linią firmware. Nie wgrywać.
2. **Reset PIN-u do `0000` jest w firmware** jako komenda UART `0x30000023`, wysyłana przez ESP32
   dla chmurowego `{"cmd":112}`. Sprawdzone w ESP32 i U13 (§3). Kasuje tylko PIN, reszta ustawień zostaje.
3. **Chmura to zwykłe MQTT bez TLS** (`mqtt://server.sk-robot.com`, port 1883), tematy
   `/<…>/<id>/get` i `/<…>/<id>/update` [F]. Nasza tabela tematów `snk/device/...` była zgadnięta i błędna.
4. Flashowanie z USB jest opisane na forum, ale procedurę już mieliśmy
   ([`u13/notes/firmware_update.md`](u13/notes/firmware_update.md)). Forum dodało praktyczne szczegóły (§4).

## 1. Poprawki w naszej dokumentacji

| Gdzie | Było | Jest |
|---|---|---|
| `PROTOCOLS.md` | `0x33000021` / `0x33000022` = wynik weryfikacji PIN | Potwierdzenia ramek ESP `0x30000021` (WiFi) i `0x30000022` (BT). Handlery `080461d8` / `080464be` odpowiadają przez `08072604(cmd, result)` [F] |
| `esp32/notes/ESP32.md` | Tematy `snk/device/{sn}/status`, `/command`, `/config`, `/ota` („inferred”) | `/%s/%s/get` i `/%s/%s/update` (ciągi w `ota_0.bin`) [F] |
| `esp32/notes/ESP32.md` | PIN w EEPROM U22 | PIN w env `pwd` na SPI NOR (już ustalone 2026-10-10) |
| `esp32/notes/ESP32.md`, `README.md` | WiFi „nieużywane w produkcie” / „wyłączone w oryginalnym firmware” | Ta sama rodzina firmware działa z aplikacją Sunseeker na Brucke. Na Lux nie próbowano parowania |
| `u13/notes/firmware_update.md` | Tabela wersji z plikami `SNK_MB_23104.bin`, `SNK_MB_23202.bin` | Takich plików nie ma. 23104/23202 to wersje produktu; plik MB dla 23202 to `SNK_MB_22905.bin` |
| `u13/notes/firmware_update.md` | Krok 3: `{"pdt_ver":23104}` | U nas (wersja 31018) niższy `pdt_ver` jest odrzucany |

## 2. Pliki firmware z wątku i dlaczego nie pasują

Nasza kosiarka [F] (captures, `0x330000A1`/`0x330000A2`): wersja produktu 31018, model
`RMC300E20V-ECDNSS`, MB `sv=31315`, bootloader `mblt_sv=50517`, BB (U16) `sv=50003`,
DB/ESP32 `hv=60400 sv=30202`.

| Kto, kiedy | Pliki / wersja | Link |
|---|---|---|
| janil, 08.2022 | `SNK_MB_21841.bin`, `SNK_MBTL_40223.bin`, `SNK_DB_60411.bin` | brak (z API producenta) |
| Maick, 30.05.2023 | paczka z okresu 23202 (zawartość niesprawdzona) | https://drive.filen.io/f/4aade867-46ee-4e39-9d1f-3c40e1981fea#IfZBZfugq5dox0qXMhZR52qVEbwqcinq |
| asminu / red81, 06.2023 | `SNK_MB_22905.bin`, `SNK_MBTL_40501.bin`, `SNK_DB_61107.bin` (= 23202) | brak |
| SnoPro, 11.08.2023 | `RMA501M20V-23205.zip`, ratunkowy `SNK_BB_30505.bin` | https://drive.google.com/file/d/1wA3UsxRyRyQDBSfnQFbM5J-sC_VuJ85q/view?usp=drivesdk |
| HiTec, 21.06.2026 | 23000 dla RM500 | https://limewire.com/d/1Goqw#cUDO442UR5 |
| HiTec, 01.08.2026 | paczka RM500 do RM501 (autor ostrzega przed uceglaniem) | https://limewire.com/d/sQKYr#dwl71yfrSV |

Nowsze wersje produktu z forum: 23303 (RM801, 2024) i 31300 (RM501/RM800 od 2025). Plików 31300
nikt nie udostępnił. Nasze 31018/31315 to najpewniej ta sama generacja co 31300 [I].

Dlaczego nie pasują [F] (tabela „File scan” w [`20261010_usb_investigation.md`](20261010_usb_investigation.md)):
- `SNK_MB_*` jest przyjmowany tylko dla 30000–49999. Brucke MB 21841…22905 odpada.
- `SNK_MBTL_*` / `btl_MB_*` tylko dla 50000–59999. MBTL 40223 / 40501 odpada.
- `SNK_BB_*` tylko gdy `ver/10000` zgadza się z bieżącym BB (u nas 5). `SNK_BB_30505` odpada.
- `SNK_DB_*` wybierany po `db_hv` (60400 → DB) bez zakresu wersji. Brucke `SNK_DB_6xxxx`
  zostałby pewnie przyjęty i zastąpiłby nasz ESP32 3.02.02 starszą linią [I].

## 3. Komenda 112: reset PIN-u

### ESP32 (`esp32/firmware/ota_0.bin`, v3.02.02) [F]

Zadanie IoT `400dc4a8` czyta `cmd` z JSON-a z chmury i obsługuje 100–199. Dla `cmd == 0x70` (112)
pod `400dcfbc` buduje `{"cmd":<double 0x41c80000:0x11800000>}` = `0x30000023` i wysyła je przez UART
do U13 tą samą ścieżką co `cmd 101`. Brak parametrów i brak sprawdzania stanu.
Dla porównania `cmd 113` (`400dcfd8`) wysyła `0x10000008` (CLEAR_USER_SETTINGS).

### U13 (`u13/firmware/u13_flash.bin`) [F]

Dispatcher `08044860` dla `0x3000xxxx` ma tablicę `tbh` pod `0804489a`; indeks = cmd − `0x30000005`,
więc `0x30000023` trafia do `0804650a`:

```
0804650a  obj->set_pwd(0)            ; vtable +0x25c = 0807c95c, zapis env "pwd", cache 0x2000027C
          jeśli błąd: odpowiedź 0x33000023 false, log "reset pwd failed"
          cnt = obj->get_pwd_err_cnt()     ; +0x16c, run_param[0]
          en  = obj->get_pwd_input_en()    ; +0x174, run_param[1]
          jeśli cnt != 0 lub en != 1:
              obj->set_pwd_err_cnt(0)      ; +0x170 = 0807b6f0 ("set password error count")
              obj->set_pwd_input_en(1)     ; +0x178 = 0807b170 ("set password enable input")
          odpowiedź 0x33000023 true, log "reset pwd success"
```

Nazwy metod pochodzą z ich komunikatów błędów. `pwd` = 0 to PIN `0000` (PIN trzymany jako uint32).
Kasowany jest tylko PIN i licznik błędnych prób. Harmonogram, konfiguracja produktu i reszta env
zostają, w przeciwieństwie do `FORMATFLASH.json`.

Na Brucke działa to z chmury: KanOl, 31.05.2024, „komenda 112 przywróciła PIN 0000” [W].

### Jak to wykorzystać u nas [I], nic z tego nie testowane

1. **Przez MQTT.** ESP łączy się z `mqtt://server.sk-robot.com` bez TLS. Przekierować tę nazwę
   (DNS albo NAT) na własny Mosquitto i opublikować `{"cmd":112}` na `/<…>/<id>/get`. Warunek:
   ESP musi mieć skonfigurowane WiFi, czyli kosiarka musi zostać sparowana w aplikacji Sunseeker.
   Na Lux tego nie próbowano. Login/hasło klienta MQTT nie zostały prześledzone.
2. **Przez nasz firmware ESPHome.** Po handshake'u wysłać `{"cmd":805306403}` po UART J8 i czekać na
   `0x33000023`. Najprostsza droga bez SWD i bez chmury. Do zrobienia w `components/snk_mower/`.

## 4. Flashowanie z USB: szczegóły z forum [W]

Procedura 3-etapowa (bootloader → MB+DB → `env_config.json`) jest w
[`u13/notes/firmware_update.md`](u13/notes/firmware_update.md). Forum dodaje:

- Pendrive FAT32 ≤16 GB; exFAT/NTFS nie działają; niektóre pendrive'y są ignorowane.
- Wyświetlacz: `USB`, procenty, `LOAD`. Nie wyłączać w trakcie.
- Aplikacja po aktualizacji pokazuje wersję `0`, bo numer w env nie został przeniesiony.
- Niedokończony OTA zostawia flagę OTA; kosiarka startuje w trybie aktualizacji i czeka na PIN
  (pasuje do `ota == 1/5` w mode select bootloadera [F]).
- Zły `SNK_BB_*` → kosiarka gaśnie przed PIN-em. Ratunek: właściwy plik BB i `env_config`
  z `{"BB":{"VER":0,"BVER":0,"BRF":0}}`.
- Obniżanie `pdt_ver` przez `env_config` nie działa na nowszym firmware; po próbie bootloader
  pokazywał `BAD USB` (2025–2026).
- `USB` na wyświetlaczu bez pendrive'a: mokre gniazdo w komorze baterii.

## 5. Inne nowe informacje [W]

- **Komendy chmury** (lista z [OlliKantola/Sunseeker_LawnMower_Control](https://github.com/OlliKantola/Sunseeker_LawnMower_Control)):
  `101` tryb, `102` zegar, `103` harmonogram, `105` deszcz, `107` nazwa, `108` punkty startowe,
  `109` restart komunikacji, `112` reset PIN, `113` (u nas `0x10000008`), `119` restart/OTA,
  `200`–`210` odczyty. Odpowiedź `509` (log) zawiera SSID i hasło WiFi jawnym tekstem.
  Do 07.2025 broker przyjmował dowolny login. Tabela w [`esp32/notes/ESP32.md`](esp32/notes/ESP32.md).
- **Limity w firmware:** ładowanie max 4 h („charging overtime”, błąd 131072, wyłączenie po ~20 min);
  powyżej ~33 °C prąd ładowania o połowę; po zaniku zasilania stacji wyłączenie po 20 min;
  powrót do stacji przerywany po ~30 min; koszenie krawędzi max 30 min; powrót na ładowanie przy ~14 %;
  opóźnienie po deszczu domyślnie 180 min; nóż 2800 rpm, turbo 3200 rpm, zmiana kierunku obrotów.
- **PIN:** 10 błędnych prób blokuje wpisywanie; trzeba odczekać 10 min przy włączonej kosiarce.
  Wyjęcie baterii pastylkowej PIN-u nie kasuje (zgodne z §3 i [`PIN.md`](PIN.md)).
- **Bateria:** obca bateria bez rozpoznawanego BMS daje „battery type not define” i wyłączenie
  przed PIN-em. Bateria 4 Ah od RM800 (`5S2P_SONY_VTC4`) działa bez zmian.
- **Dokumenty serwisowe SK Robot:**
  https://www.sk-robot.com/uploads/202109/18/210918101541646.pdf ,
  https://www.sk-robot.com/uploads/202109/18/210918101451197.pdf
- **Stacja ładująca:** częsta przyczyna E11 to spalona dioda/rezystor w stacji (burze, mrówki);
  Jula sprzedaje zamienną płytkę.

## Do zrobienia

- Dodać do komponentu ESPHome przycisk „reset PIN” wysyłający `0x30000023` i przetestować na kosiarce.
- Prześledzić login/hasło klienta MQTT w ESP32, jeśli zechcemy iść drogą chmury.
