# Capture: czwarty — Docking: HOME+OK → drive onto station → charging

**Duration**: ~60s
**Action**: Mower in front of dock. Power on, enter PIN, press HOME+OK. Mower drives onto station and starts charging.

## State sequence

```
→ boot, device info
→ state:0, result:true, lock:1
→ state:1 (ready)
→ HOME+OK (physical)
  → 0x41000020 {"result":1}    ← START_ACK (drives off station)
  → state:2                    ← driving (brief reverse)
  → 0x41000003                 ← EXEC_ACTION
  → state:6
  → 0x41000006                 ← ★ RETURN HOME
  → state:9                    ← RETURNING TO DOCK
  → station:true               ← ★ ON STATION!
  → border_state:0             ← wire signal gone
  → 0x41000007                 ← ★ CHARGE_START / DOCKED
  → state:10                   ← ★ CHARGING!
  → RTC sync continues...
```

## New discoveries

- **`0x41000007` (1090519047)** = DOCKED/CHARGE_START — sent when mower is on station
- **`station:true`** = field in `0x33000020` (state update) indicating dock detected
- **state:10 = CHARGING**
- HOME sequence: first short reverse (state:2), then return via wire (state:9), then dock detection and charging (state:10)
