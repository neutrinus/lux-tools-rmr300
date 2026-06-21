# Captures 2026-06-21 — LA UART sniffer (oryginalny firmware)

## Setup

- **LA**: fx2lafw (Saleae Logic clone), 4MHz samplerate
- **Kanały**: D1=MB TX (→), D2=ESP TX (←), D3=START, D4=OK
- **Podłączenie**: równolegle do J2 na display PCB
- **Problem**: ON (brązowy) zakłócał pracę — odpięty
- **Baud**: 230400 8N1
- **Format ramki**: `&{json}<CRC>##`

---

## 1. `pierwszy.sr` — PIN entry + boot

**Czas**: ~43s (przerwane Ctrl+C)

**Co robione**: Włączono kosiarkę, wpisano PIN (9633), nie naciskano START.

**Zawartość**:

| Kierunek | CMD | Opis |
|----------|-----|------|
| MB→ESP | `0x40000004` | BOOT handshake |
| MB→ESP | `0x40000001 {"init":3}` | INIT potwierdzenie |
| MB→ESP | `0x30000005` | Keepalive (ciągły) |
| MB→ESP | `0x30000028 {"state":0}` | ESP_STATE=idle |
| MB→ESP | `0x30000021 {"wifi":0,"str":0}` | WiFi status |
| MB→ESP | `0x30000022 {"bt":0,"str":0}` | BT status |
| MB→ESP | `0x22000000 {"rain":1}` | Rain sensor |
| MB→ESP | `0x40000006 {"hv":...,"sv":...,"mac":"..."}` | Device info |
| MB→ESP | `0x300000A6` | ESP_TRIM |
| MB→ESP | `0x41000005 {"pwd":9633}` | **PIN wysłany** |
| ESP→MB | `0x20000001 {"action":0}` | action=0 (idle) |
| ESP→MB | `0x40000009` | ? |
| ESP→MB | `0x50000021 {"bat":2}` | Bateria |
| ESP→MB | `0x33000030 {"map_sn":0,"area":300}` | Mapa / area |
| ESP→MB | `0x40000020 {"lv":255}` | Level |
| ESP→MB | `0x33000021 {"name":"MyMower",...}` | **Device info pełne** |

### Uwagi

- PIN 9633 pojawia się jako `0x41000005 {"pwd":9633}` od MB→ESP (wait, to jest D1!)
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

### Odkryte komendy ESP→MB

| CMD HEX | DEC | Nazwa z disasm | Użycie |
|---------|-----|----------------|--------|
| `0x41000020` | 1090519072 | START_ACK | Gdy START/HOME wciśnięty |
| `0x41000003` | 1090519043 | EXEC_ACTION | Przy każdym STOP |
| `0x41000004` | 1090519044 | ERROR_NOTIFY | Gdy błąd |
| `0x41000005` | 1090519045 | PIN_SEND? | Z `pwd`=PIN albo bez pola |
| `0x41000006` | 1090519046 | (unknown) | **★ HOME / RETURN** |
| `0x41000007` | 1090519047 | (unknown) | **★ DOCKED / CHARGE START** |
| `0x41000002` | 1090519042 | LOCK | lock=1 po PIN |
| `0x4100000A` | 1090519050 | "action" | (nie zaobserwowano) |

### Stany MB (ESP→MB raportuje w `0x33000020`)

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

## Kluczowe wnioski

1. **START/STOP/HOME są fizycznymi przyciskami → U16** — nie przechodzą przez UART
2. **ESP nie może wysłać komendy "start mowing"** przez UART — nie istnieje
3. ESP wysyła ACK (`0x41000020`) po fizycznym starcie
4. ESP wysyła `0x41000006` po fizycznym HOME
5. Aby sterować przez HA, trzeba by podłączyć GPIO ESP do linii przycisków (ale idą do U16, nie ESP)
6. Alternatywnie: zbadać czy można wywołać akcję wysyłając `0x41000006` lub `0x41000020` z polem `action:1/2`
