# Analiza: Komendy koszenia w oryginalnym firmware SNK Mower

## Data: 2026-06-21
## Cel: Odkryć komendy które oryginalna aplikacja/MQTT wysyłała by kosiarka kosiła

---

## PRZEŁOMOWE ODKRYCIA

### 1. Kosiarka MA WiFi/BT/MQTT (mimo twierdzeń producenta)

ESP32-WROOM-32UE zawiera pełny stack ESP-IDF v3.02.02 z:
- **WiFi** (klient, SSID `cy-public`, hasło `88888888`)
- **Bluetooth** 4.2 BR/EDR + BLE
- **MQTT client** (broker: `mqtt://server.sk-robot.com`)
- **Serwery testowe**: `test1` (Ali), `test2` (AWS), `test3` (local), `test4` (Amazon)
- **RF 868/915 MHz** (pilot RF, `driver_rf.c`, `MOW_seed`, `RF match`)

Pliki źródłowe ESP32: `rw_mqtt.c`, `rw_wifi.c`, `rw_ble.c`, `rw_uart.c`, `rw_display.c`, `rw_key.c`, `rw_ota.c`, `rw_env.c`, `rw_timer.c`.

### 2. Architektura komunikacji aplikacja → kosiarka

```
Aplikacja mobilna / chmura
    │ MQTT (server.sk-robot.com)
    ▼
ESP32 (parsuje JSON z polem "action")
    │ UART @230400, JSON: {"cmd":...,"app_main":...,"chedule":...}
    ▼
U16 (GD32F303) — bridge JSON → U13
    │
    ▼
U13 (GD32F305) — main MCU, wykonuje akcję (motory, ostrze)
```

### 3. KLUCZOWA komenda akcji (odkryta w dekompilacji ESP32)

**Funkcja `FUN_400e27fc`** (główna pętla akcji ESP32, @ `0x400e27fc`):
```c
// Pobiera action_value z vtable[+8] obiektu *DAT_400d1288
uVar15 = (**(code **)(iVar10 + 8))();
*(undefined4 *)(iVar20 + 0x3c) = uVar15;  // store action @ offset 0x3c

// Jeśli akcja się zmieniła (0x40 != 0x3c):
if (*(int *)(*piVar6 + 0x40) != *(int *)(*piVar6 + 0x3c)) {
    ESP_LOGI("goto action");  // log "goto action" @ 0x3f405b22
    FUN_400e277c();            // buduj i wyślij komendę do MB
}
```

**Funkcja `FUN_400e277c`** (wykonawca akcji, @ `0x400e277c`):
```c
void FUN_400e277c(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "app_main", 24.125);  // UNK_400d1730 = 0x41c10000 (float)
    
    // Pobierz action_value z obiektu stanu @ offset 0x3c
    uVar3 = (*DAT_400d07b8)(*(undefined4 *)(*DAT_400d1560 + 0x3c), 0);
    cJSON_AddNumberToObject(root, "chedule", uVar3);  // UNK_400d1734 = 0x3f4043f5 -> "chedule"
    
    char *json = cJSON_PrintUnformatted(root);
    // Wyślij przez port vtable[+0x14] (UART do MB)
    (**(code **)(FUN_400dddc8() + 0x14))(json);
    cJSON_Delete(root);
}
```

**Wysłany JSON**: `{"app_main":24.125,"chedule":<action_value>}`

### 4. Klucz JSON to "chedule" (fragment "schedule") — używany CELOWO

- `"chedule"` @ VMA `0x3f4043f5` (pełny string `"schedule"` jest @ `0x3f4043f4`)
- W firmware ESP32 wartość `0x3f4043f5` ("chedule") występuje **3 razy** w literal pool
- Wartość `0x3f4043f4` ("schedule" - pełny) występuje **0 razy**
- To NIE jest błąd Ghidra — firmware naprawdę używa fragmentu "chedule"

### 5. U13 (main MCU) rozumie "action" i ma automatyczne koszenie

Stringi w U13 firmware (`u13_flash_1mb.bin`):
- **`action`** (@ `0x7778`) — komenda action
- **`Robot on schedule, start work`** — automatyczne rozpoczęcie koszenia
- **`on schedule, goto departure`** — wyjście z docku
- **`on schedule and rain disable, goto departure`** — wyjście mimo deszczu
- **`on schedule, but battery voltage low, goto charging`** — powrót przy niskiej baterii
- **`auto schedule, work days=%d, set one day=%d edge`** — konfiguracja harmonogramu
- **`auto schedule work day=0, change to manual model`** — przejście na manual
- **`start work`** — rozpoczęcie pracy
- **`goto charging`** — powrót do ładowania
- **`raining, goto charging`** — powrót przy deszczu

Pliki źródłowe U13:
- `service_movement/movebase_snk_v10.c` — sterowanie ruchem
- `service_map/service_workmap.c` — mapa pracy
- `service_time/service_time.c` — czas/harmonogram
- `service_port/service_bdport.c` — port UART
- `service_blade/service_blade.c` — ostrze
- `service_border/service_border.c` — granica

### 6. Statusy kosiarki (z logów "robot status : XXX" w ESP32)

| Status | Hex cmd | Opis |
|--------|---------|------|
| `idle` | 0 | Bezczynność |
| `err` | — | Błąd (errortype) |
| `work` | — | **Koszenie** |
| `edge` | — | Krawędź |
| `pause` | — | Pauza |
| `dock` | — | **Powrót do docku** |
| `charging` | — | Ładowanie |
| `poweroff` | — | Wyłączony |
| `rain dock` | — | Powrót z deszczu |
| `rain wait` | — | Czekanie na deszcz |
| `rain delay` | — | Opóźnienie z deszczu |

### 7. Fizyczny START — ESP nie uczestniczy

**Capture 04-return-home** potwierdza:
1. Naciśnięcie START → U16 obsługuje LOKALNIE (J8 pin6 → U16 GPIO)
2. MB → ESP: `0x41000020 result=1` (START_ACK)
3. MB → ESP: `STATUS state=2` (MOWING)
4. MB → ESP: `0x41000003` (EXEC_ACTION — notyfikacja)
5. ESP jest tylko INFORMOWANE, nie inicjuje

**Wniosek**: Fizyczny START nie generuje komendy ESP→MB. Nie można symulować START przez ESP.

---

## Dlaczego poprzednie próby ESPHome zawiodły

| Próba | Komenda | Wynik | Dlaczego |
|-------|---------|-------|---------|
| `start_mowing()` | `ESP_TRIM (0x300000A6)` + harmonogram | MB zignorowało | MB ma własny harmonogram (`auto=0`), nie nadpisuje go ESP_TRIM |
| `start_mowing()` | `ESP_STATE(state=2)` | MB zignorowało | `state` od ESP to INFORMACJA (ESP→MB), nie komenda. MB nie zmienia stanu |
| `return_to_dock()` | `ESP_ERR_ACK1 (0x10000001)` | Bez efektu | To jest potwierdzenie błędu, nie komenda powrotu |
| `ESP_TRIM` z `auto=1` | MB odsyła `auto=0` | MB ma priorytet własnych ustawień | MB nie nadpisuje auto=0 wartością z ESP |

**Kluczowy błąd**: Zakładaliśmy że komenda koszenia to jedna z dokumentowanych komend UART (ESP_TRIM, ESP_STATE, etc.). Tymczasem oryginalna aplikacja używała **JSON z polem `action`** przez zupełnie inną ścieżkę — pole `chedule` (nie `cmd`).

---

## HIPOTEZY komendy koszenia

### Hipoteza A: Komenda `{"app_main":24.125,"chedule":<value>}` (NAJBARDZIEJ PRAWDOPODOBNA)

Odkryta w dekompilacji ESP32 (`FUN_400e277c`). Wartości `<action_value>` do przetestowania:

| Wartość | Status kosiarki | Działanie |
|---------|----------------|-----------|
| 0 | idle | stop/bezczynność |
| 1 | work | **KOSZENIE** |
| 2 | edge | krawędź |
| 3 | pause | pauza |
| 4 | dock | **POWRÓT DO DOCKU** |
| 5 | charging | ładowanie |
| 6 | poweroff | wyłącz |
| 7+ | rain dock/wait/delay | deszcz |

### Hipoteza B: Harmonogram z `auto=1` (automatyczne koszenie)

U13 ma "Robot on schedule, start work" — automatycznie zaczyna kosić gdy:
- Harmonogram jest aktywny (`auto=1`)
- Jest dzień pracy (`work days`)
- Jest czas startu (`start minutes`)
- Bateria jest OK
- Nie pada

Wymaga ustawienia pełnego harmonogramu przez ESP_TRIM (0x300000A6) z `auto=1`. Ale MB może nadpisywać `auto=0`.

### Hipoteza C: Bezpośrednia komenda MQTT "action"

U13 ma string "action" — może istnieje bezpośrednia komenda JSON `{"action":"work"}` lub `{"action":1}`. Wymaga dekompilacji U13 aby potwierdzić.

---

## REKOMENDACJE — co dalej (priorytety)

### PRIORYTET 1: Sniffer UART z oryginalnym firmware (DEFITYWNE ROZWIĄZANIE)

**To jest JEDYNY sposób aby z 100% pewnością ustalić komendę koszenia.**

1. Wgrać oryginalny firmware ESP32 (z dumpa `esp32_dump.bin`)
2. Skonfigurować WiFi — ustawić SSID/hasło w NVS (`robot_ssid`, `wifi_passwd`)
3. Uruchomić własny broker MQTT (lub przekierować `server.sk-robot.com` przez DNS/hosts)
4. Wysłać komendę koszenia przez aplikację lub bezpośrednio MQTT
5. **Złapać UART traffic** (J8 piny 3-4, logic analyzer @230400)
6. Zobaczyć dokładny JSON który ESP wysyła do MB

Bez tego możemy tylko zgadywać wartości `action_value`.

### PRIORYTET 2: Test empiryczny komendy `{"app_main":24,"chedule":<value>}`

Zaimplementować w ESPHome wysyłanie JSON przez UART:
```
{"app_main":24,"chedule":1}  ← test koszenia
{"app_main":24,"chedule":4}  ← test powrotu
{"app_main":24,"chedule":0}  ← test stop
```

Obserwować reakcję MB (status, error codes, czy silniki ruszą). **UWAGA**: kosiarka musi być na podłodze (nie na ławce — error 16 lift).

### PRIORYTET 3: Pełna dekompilacja U13 (main MCU)

U13 ma 360 funkcji w `app_functions.txt`. Zdekompilować:
- Handler JSON dla "action" — znajdzie się przez xref do stringu "action" @ `0x08007778`
- Handler JSON dla "chedule"/"schedule"
- Logikę "Robot on schedule, start work" (`service_time.c`)
- Sprawdzić czy U13 rozumie "action" jako bezpośrednią komendę czy tylko przez harmonogram

### PRIORYTET 4: Dekompilacja U16 (board MCU)

U16 jest bridge JSON między ESP a U13. Sprawdzić:
- Czy U16 modyfikuje JSON (np. "chedule" → "schedule")
- Czy U16 dodaje pole `cmd` (numer komendy)
- Jak U16 przekazuje "action" do U13

U16 ma 420 funkcji (`app_functions.txt`), główne: `comm_task` @ `0x08013680`, `process_comm.c`, `driver_mboard_port_snk_v2.c`.

### PRIORYTET 5: Analiza protokołu RF (pilot)

ESP32 ma `driver_rf.c` z obsługą pilota RF 868/915 MHz. Może pilot RF jest alternatywą do sterowania. Stringi:
- `RF match start`, `GET mate, rf seed:%d, mower seed:%d`
- `MOW_seed`, `STA_seed`, `RF_Channel`
- `RF is connected`, `RF disconnected`
- `RF mode:NO`, `RF mode:868`, `RF mode:915`

Może istnieje pilot RF który wysyła komendy koszenia. Wymaga analizy protokołu RF.

---

## PODSUMOWANIE

**Odkryliśmy protokół komendy akcji**: ESP32 oryginalnie wysyłało `{"app_main":24.125,"chedule":<action_value>}` do MB przez UART JSON, z wartością akcji pobraną z vtable. To jest zupełnie inna ścieżka niż komendy `0x30xxxxxx`/`0x40xxxxxx` które testowaliśmy wcześniej.

**U13 rozumie "action"** i ma automatyczne koszenie z harmonogramu ("Robot on schedule, start work").

**Najprawdopodobniejsza komenda koszenia**: `{"app_main":24,"chedule":1}` (wartość 1 = work/mow).

**Najprawdopodobniejsza komenda powrotu**: `{"app_main":24,"chedule":4}` (wartość 4 = dock).

**Aby potwierdzić z 100% pewnością**, potrzebny jest sniffer UART z oryginalnym firmware ESP32 podczas wysyłania komendy koszenia przez aplikację MQTT.

---

## Pliki analizy (artefakty)

| Plik | Zawartość |
|------|-----------|
| `/tmp/opencode/gotoaction_clean.txt` | Dekompilacja FUN_400e27fc (1240 linii, główna pętla akcji) |
| `/tmp/opencode/actionfunc_clean.txt` | Dekompilacja FUN_400e277c (wykonawca akcji) + helpery |
| `/tmp/opencode/find_l32r_strings.py` | Skrypt Python: L32R → string DROM mapper (poprawiony IROM offset 0x30018) |
| `/tmp/opencode/Decompile*.java` | Skrypty Ghidra headless do dekompilacji |
| `esp32/notes/ESP32_DECOMPILATION.md` | Tablica komend ESP32 (CMD 0x4100000A = "action") |
| `captures/04-return-home/notes.md` | Capture fizycznego START + HOME |
| `u13/decomp/` | Dekompilacja U13 (40 plików, częściowa) |

## Narzędzia użyte

- **Ghidra 12.1.2** headless (analyzeHeadless + postScript Java) — dekompilacja ESP32 Xtensa
- **Python** — parser `disasm.s` (34789 instrukcji L32R) + literal pool mapper
- **strings** — analiza stringów w U13/U16/ESP32
- **sigrok/PulseView** — captures UART ( już istniejące, 6 scenariuszy)
