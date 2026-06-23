# Capture: trzeci — Full cycle: START→MOW→STOP→HOME→STOP

**Duration**: ~60s
**Action**: 
1. Power on, enter PIN
2. START → mower starts mowing (state:2)
3. STOP → stops (state:6, then state:8)
4. HOME → starts returning to dock (state:9)
5. STOP → stops

## State sequence

```
→ state:1 (ready, PIN remembered)
→ START (physical)
  → 0x41000020 {"result":1}    ← START_ACK
  → state:2                    ← MOWING
  → 0x41000003                 ← EXEC_ACTION
  → state:6                    ← stop/pause
→ [mows for a while]
→ STOP (physical)
  → stop_state:1
  → 0x41000003                 ← EXEC_ACTION
  → state:6
  → stop_state:0
  → 0x41000005                 ← ★ something before return
  → state:8                    ← ★ maybe "seek wire"?
→ HOME (physical)
  → 0x41000006                 ← ★ RETURN_HOME! (0x41000006)
  → state:9                    ← ★ RETURNING TO DOCK
→ [returns to dock]
→ STOP (physical)
  → stop_state:1
  → 0x41000003                 ← EXEC_ACTION
  → state:6
  → stop_state:0
```

## Key observations

- `0x41000006` (RETURN_HOME) = 1090519046 — confirmed notification after HOME button
- state:8 observed before returning — possibly "seek wire" phase
- `0x41000005` without `pwd` field sent by MB before state:8
