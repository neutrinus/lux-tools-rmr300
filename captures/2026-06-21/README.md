# Captures 2026-06-21 — LA UART sniffer (oryginalny firmware)

## Setup

- **LA**: fx2lafw (Saleae Logic clone), 4MHz samplerate
- **Kanały**: **D1=ESP TX (←)**, **D2=MB TX (→)**, D3=START, D4=OK
- **Podłączenie**: równolegle do J2 na display PCB
- **Problem**: ON (brązowy) zakłócał pracę — odpięty
- **Baud**: 230400 8N1
- **Format ramki**: `&{json}<CRC>#` (pojedynczy `#`)

> **KOREKTA 2026-06-22**: Kierunki kanałów były ODWRÓCONE w pierwotnej wersji tego pliku.
> Poprzednia wersja mówiła "D1=MB TX, D2=ESP TX" — to było błędne.
>
> **Dowody krzyżowe** (3 niezależne markery, 100% spójne we wszystkich 4 plikach):
> 1. `0x20000001` POWER_ON pojawia się na **D2** → D2 = MB TX
> 2. `0x30000005` KEEPALIVE i `0x22000000` RAIN pojawiają się na **D1** → D1 = ESP TX
>    (RAIN jest wysyłany przez ESP — czujnik jest na display board J4→ESP32 GPIO36)
> 3. `0x41000005 {"pwd":9633}` PIN pojawia się na **D1** → D1 = ESP TX
>    (PIN jest wprowadzany na display board i wysyłany przez ESP do MB)
> 4. `0x40000006 {"mac":"08-f9-e0-b3-da-70"}` na D1 → D1 = ESP TX (WiFi MAC = ESP32)
> 5. `0x330000A1 {"name":"MyMower","sn":"2312CGF..."}` na D2 → D2 = MB TX (device info z mainboard)
>
> **Wszystkie tabele kierunków w tym dokumencie zostały poprawione.**
> (Poprzednia wersja notowała "MB→ESP" dla komend które faktycznie są ESP→MB i odwrotnie.)

---

## 1. `pierwszy.sr` — PIN entry + boot

**Czas**: ~43s (przerwane Ctrl+C)

**Co robione**: Włączono kosiarkę, wpisano PIN (9633), nie naciskano START.

**Zawartość** (kierunki po korekcie):

| Kierunek | CMD | Opis |
|----------|-----|------|
| ESP→MB | `0x40000004` | BOOT handshake |
| ESP→MB | `0x40000001 {"init":3}` | INIT potwierdzenie |
| ESP→MB | `0x30000005` | Keepalive (ciągły) |
| ESP→MB | `0x30000028 {"state":0}` | ESP_STATE=idle |
| ESP→MB | `0x30000021 {"wifi":0,"str":0}` | WiFi status |
| ESP→MB | `0x30000022 {"bt":0,"str":0}` | BT status |
| ESP→MB | `0x22000000 {"rain":1}` | Rain sensor (sensor na display board) |
| ESP→MB | `0x40000006 {"hv":...,"sv":...,"mac":"08-f9-e0-b3-da-70"}` | ESP info (MAC ESP32) |
| ESP→MB | `0x300000A6` | ESP_TRIM |
| ESP→MB | `0x41000005 {"pwd":9633}` | **PIN wysłany** (ESP→MB) |
| MB→ESP | `0x20000001 {"action":0}` | POWER_ON, action=0 (idle) |
| MB→ESP | `0x40000009` | BOOT_HEART |
| MB→ESP | `0x50000021 {"bat":2}` | Bateria |
| MB→ESP | `0x330000B0 {"map_sn":0,"area":300}` | Mapa / area |
| MB→ESP | `0x40000020 {"lv":255}` | Light level |
| MB→ESP | `0x330000A1 {"name":"MyMower",...}` | **Device info pełne** (name, sn, version) |

### Uwagi

- PIN 9633 pojawia się jako `0x41000005 {"pwd":9633}` na D1 (= ESP TX) → **ESP wysyła PIN do MB** ✓
  (W poprzedniej wersji autor pomylił kierunki i napisał "MB→ESP (wait, to jest D1!)" —
  D1 to ESP TX, więc PIN na D1 = ESP→MB, co jest poprawne.)
- Button D3/D4 nie pokazują zmian — słaby kontakt sond

---

## 2. `drugi.sr` — PIN + START + error 16

**Czas**: ~60s

**Co robione**: Włączono, wpisano PIN, naciśnięto START. Mower próbował kosić (state=2), ale dostał error 16 (brak zasięgu kabla ograniczającego).

**Sekwencja stanów**:

```
state:0 (idle)
  → PIN unlock (0x41000005 pwd:9633)
  → result:true, lock:1
  → state:1 (ready)
  → START (fizyczny przycisk)
  → 0x41000020 {"result":1}    ← START_ACK
  → state:2 (MOWING!)           ← ● KOSI!
  → 0x41000003                  ← EXEC_ACTION
  → state:6 (transition?)
  → 0x41000004 {"err":16}       ← ERROR_NOTIFY
  → state:7,error:16            ← Błąd 16 (out of wire)
  → cykl state:6→7:16 powtarza się
  → stop_state:1 (STOP wciśnięty)
  → stop_state:0
  → dalej cykl błędów
```

### Uwagi

- **state=2 = MOWING** — potwierdzone!
- Error 16 = out of perimeter wire (zasięg)
- START idzie bezpośrednio do U16 — nie ma UART command dla startu
- ESP tylko ACKuje (`0x41000020 START_ACK`)

---

## 3. `trzeci.sr` — Pełny cykl: START→MOW→STOP→HOME→STOP

**Czas**: ~60s

**Co robione**: 
1. Włączono
2. START → mower zaczął kosić (state:2)
3. STOP → przestał (state:6, potem state:8)
4. HOME → zaczął wracać do domu (state:9)
5. STOP → przestał

**Sekwencja**:

```
→ state:1 (ready, zapamiętany PIN)
→ START (fizyczny)
  → 0x41000020 {"result":1}    ← START_ACK
  → state:2                    ← KOSI
  → 0x41000003                 ← EXEC_ACTION
  → state:6                    ← stop/pauza
→ [kosi przez chwilę]
→ STOP (fizyczny)
  → stop_state:1
  → 0x41000003                 ← EXEC_ACTION
  → state:6
  → stop_state:0
  → 0x41000005                 ← ★ coś przed powrotem
  → state:8                    ← ★ maybe "seek wire"?
→ HOME (fizyczny)
  → 0x41000006                 ← ★ RETURN_HOME! (0x41000006)
  → state:9                    ← ★ RETURNING TO DOCK
→ [wraca do domu]
→ STOP (fizyczny)
  → stop_state:1
  → 0x41000003                 ← EXEC_ACTION
  → state:6
  → stop_state:0
```

## 4. `czwarty.sr` — Dokowanie: HOME+OK → wjazd na stację → ładowanie

**Czas**: ~60s

**Co robione**: Kosiarka przed stacją. Włączono, wpisano PIN, naciśnięto HOME+OK. Kosiarka wjechała na stację i zaczęła ładować.

**Sekwencja**:

```
→ boot, device info
→ state:0, result:true, lock:1
→ state:1 (ready)
→ HOME+OK (fizyczny)
  → 0x41000020 {"result":1}    ← START_ACK (zjeżdża ze stacji)
  → state:2                    ← jazda (krótki odjazd)
  → 0x41000003                 ← EXEC_ACTION
  → state:6
  → 0x41000006                 ← ★ RETURN HOME
  → state:9                    ← RETURNING TO DOCK
  → station:true               ← ★ NA STACJI!
  → border_state:0             ← kabel zniknął
  → 0x41000007                 ← ★ CHARGE_START / DOCKED
  → state:10                   ← ★ CHARGING!
  → RTC sync continues...
```

### Nowe odkrycia

- **`0x41000007` (1090519047)** = DOCKED/CHARGE_START — wysyłany gdy kosiarka jest na stacji
- **`station:true`** = pole w `0x33000020` (state update) informujące że wykryto stację
- **state:10 = CHARGING**
- Sekwencja HOME: najpierw krótki odjazd (state:2), potem powrót po kablu (state:9), potem wykrycie stacji i ładowanie (state:10)

### Odkryte komendy MB→ESP (kierunek poprawiony)

> **Korekta**: Poprzednia wersja nazywała tę sekcję "ESP→MB" — to było błędne.
> Wszystkie te komendy pojawiają się na D2 = MB TX, więc są **MB→ESP**.

| CMD HEX | DEC | Nazwa z disasm | Użycie |
|---------|-----|----------------|--------|
| `0x41000020` | 1090519072 | START_ACK | Gdy START/HOME wciśnięty (MB potwierdza ESP) |
| `0x41000003` | 1090519043 | EXEC_ACTION | Notyfikacja akcji wykonanej (STOP, itp.) |
| `0x41000004` | 1090519044 | ERROR_NOTIFY | Gdy błąd |
| `0x41000005` | 1090519045 | SEEK_WIRE | Bez pola `pwd` — MB→ESP notyfikacja (przed state:8) |
| `0x41000006` | 1090519046 | RETURN_HOME | **★ HOME / RETURN** notification |
| `0x41000007` | 1090519047 | DOCKED_CHARGE | **★ DOCKED / CHARGE START** |
| `0x41000002` | 1090519042 | LOCK | lock=1 po PIN |
| `0x4100000A` | 1090519050 | "action" | (nie zaobserwowano w captures — możliwe ESP→MB) |

### Stany MB (MB→ESP raportuje w `0x330000A0`)

| State | Znaczenie |
|-------|-----------|
| 0 | Idle (po włączeniu, przed PIN) |
| 1 | Ready (PIN odblokowany) |
| 2 | **MOWING** (lub jazda/odjazd) |
| 6 | Stop/pauza |
| 7 | Error (z polem `error:N`) |
| 8 | **? Seek wire?** |
| 9 | **RETURNING TO DOCK** |
| 10 | **CHARGING** |

---

## Kluczowe wnioski (kierunki poprawione)

1. **START/STOP/HOME są fizycznymi przyciskami → U16** — nie przechodzą przez UART
2. **ESP nie może wysłać komendy "start mowing"** przez UART — nie istnieje
3. **MB wysyła** `0x41000020` (START_ACK) do ESP po fizycznym starcie — ESP jest tylko informowane
4. **MB wysyła** `0x41000006` (RETURN_HOME) do ESP po fizycznym HOME
5. **MB wysyła** `0x41000007` (DOCKED_CHARGE) do ESP po zadokowaniu
6. ESP wysyła do MB: KEEPALIVE, POLL, RAIN, PIN, WiFi/BT status, ESP_INFO, error ACKs
7. Aby sterować przez HA, trzeba by podłączyć GPIO ESP do linii przycisków (ale idą do U16, nie ESP)
8. Alternatywnie: zbadać komendę `0x4100000A` ("action") z dekompilacji ESP32 — nie zaobserwowano w captures
