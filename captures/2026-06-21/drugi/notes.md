# Capture: drugi — PIN + START + error 16

**Duration**: ~60s
**Action**: Power on, enter PIN, press START. Mower attempts mowing (state=2) but gets error 16 (out of perimeter wire range).

## State sequence

```
state:0 (idle)
  → PIN unlock (0x41000005 pwd:9633)
  → result:true, lock:1
  → state:1 (ready)
  → START (physical button)
  → 0x41000020 {"result":1}    ← START_ACK
  → state:2 (MOWING!)           ← ◆ MOWING!
  → 0x41000003                  ← EXEC_ACTION
  → state:6 (transition?)
  → 0x41000004 {"err":16}       ← ERROR_NOTIFY
  → state:7,error:16            ← Error 16 (out of wire)
  → cycle state:6→7:16 repeats
  → stop_state:1 (STOP pressed)
  → stop_state:0
  → continues error cycle
```

## Key observations

- **state=2 = MOWING** — confirmed!
- Error 16 = out of perimeter wire (range)
- START goes directly to U16 — no UART command for start exists
- ESP only ACKs (`0x41000020 START_ACK`)
