# Firmware Decompilation Setup

## How to run ghidra-cli for firmware decompilation

### 1. Requirements

| Tool | Path |
|------|------|
| Ghidra 12.1.2 | `/home/marek/tmp/kosiarka/sw/ghidra_extracted/ghidra_12.1.2_PUBLIC` |
| ghidra-cli (modified) | `/home/marek/tmp/kosiarka/sw/ghidra-cli/target/release/ghidra` |
| JDK 21 (Adoptium Temurin) | `/home/marek/tmp/kosiarka/sw/jdk-21.0.6+7` |

### 2. Environment variables

```bash
export JAVA_HOME=/home/marek/tmp/kosiarka/sw/jdk-21.0.6+7
export PATH=$JAVA_HOME/bin:$PATH
export GHIDRA_INSTALL_DIR=/home/marek/tmp/kosiarka/sw/ghidra_extracted/ghidra_12.1.2_PUBLIC

alias ghidra="$GHIDRA_INSTALL_DIR/../ghidra-cli/target/release/ghidra"
```

### 3. Configuration (one-time)

```bash
ghidra config set ghidra_install_dir "$GHIDRA_INSTALL_DIR"
ghidra config set ghidra_project_dir /home/marek/tmp/kosiarka/ghidra_proj
ghidra config set default_project Mower_Firmware
```

### 4. Import and analysis

```bash
# Import raw binary as ARM Cortex-M4
"$GHIDRA_INSTALL_DIR/support/analyzeHeadless" \
  /home/marek/tmp/kosiarka/ghidra_proj Mower_Firmware \
  -import /home/marek/tmp/kosiarka/u13/firmware/u13_flash.bin \
  -overwrite \
  -processor ARM:LE:32:Cortex \
  -cspec default \
  -loader BinaryLoader \
  -loader-baseAddr 0x08000000 \
  -noanalysis

# Run bridge + analysis (bridge compiles .java → .class and launches Ghidra)
ghidra analyze --project Mower_Firmware --program u13_flash.bin -vv
```

### 5. Common commands

```bash
# List functions
ghidra function list --project Mower_Firmware --program u13_flash.bin

# Filter functions
ghidra function list --filter "name ~ usb OR name ~ key" --project Mower_Firmware

# Decompile a specific function
ghidra decompile FUN_0800cf38 --project Mower_Firmware --program u13_flash.bin

# Search strings
ghidra find string "FORMATFLASH" --project Mower_Firmware --program u13_flash.bin

# Cross-reference (where a string/function is used)
ghidra x-ref to 0x0800300c --project Mower_Firmware --program u13_flash.bin

# Cross-reference FROM
ghidra x-ref from 0x0800cf38 --project Mower_Firmware --program u13_flash.bin

# Program summary
ghidra summary --project Mower_Firmware --program u13_flash.bin

# Display memory map
ghidra memory list --project Mower_Firmware --program u13_flash.bin

# List strings
ghidra strings list --project Mower_Firmware --program u13_flash.bin
```

### 6. Known issues and solutions

#### Issue: OSGi in Ghidra 12.1.2 won't compile `.java` scripts

Ghidra 12.1.2 has broken Java compilation via OSGi (Phidias/BND) — even an empty
`extends GhidraScript` script won't compile. Affects Java 21+ and Java 25.

**Solution:** ghidra-cli was modified (`src/ghidra/bridge.rs`):
1. After saving `GhidraCliBridge.java` to disk, it runs `javac` with Ghidra's full classpath
2. Compiles to `.class` in `~/.config/ghidra-cli/class_scripts/`
3. Runs `analyzeHeadless` with `-postScript GhidraCliBridge.class` (bypassing OSGi)
4. HeadlessAnalyzer loads `.class` directly via `classLoaderForDotClassScripts`

Modifications in `GhidraCliBridge.java`:
- `CParserUtils.parseSignature()` API changed in Ghidra 12.x — returns `FunctionDefinitionDataType`
  directly instead of `CParseResults.getDataType()`. Lines 2541-2549 fixed.

#### Issue: Java 25 with Ghidra

System JDK is Java 25-openjdk. Ghidra 12.1.2 requires JDK ≥ 21.
Downloaded Adoptium Temurin JDK 21.0.6+7 to `/home/marek/tmp/kosiarka/sw/jdk-21.0.6+7`.

```bash
export JAVA_HOME=/home/marek/tmp/kosiarka/sw/jdk-21.0.6+7
```

`java_home.save` is ignored by Ghidra — `JAVA_HOME` in env is necessary.

#### Issue: Missing x-refs for strings

After analysis, some strings (e.g. `FORMATFLASH.json`, `ready to format flash`)
have no cross-references — the code referencing them was not disassembled.
Possible causes:
- code is in an undisassembled branch (dead code)
- strings are used via pointers in tables (data-driven)
- analysis didn't detect all jumps (e.g., jump tables, vtables)

To find usage, binary-search for the string offset or trace
xrefs manually.

### 7. Analysis results for u13_flash.bin (GD32F305)

| Parameter | Value |
|-----------|-------|
| Architecture | ARM Cortex-M4 Thumb2 |
| Base address | `0x08000000` |
| Size | 512 KB (`0x80000`) |
| Functions found | **1672** |
| Input format | Raw Binary |
| Language | `ARM:LE:32:Cortex` |

#### Key strings

| Address | String | Meaning |
|---------|--------|---------|
| `0x0800300c` | `env_read.json` | JSON configuration |
| `0x08003990` | `FORMATFLASH.json` | Flash formatting switch |
| `0x080039a4` | `ready to format flash` | Ready to format |
| `0x080039bc` | `format flash` | Execute format |
| `0x0800394c` | `USB disk Ready` | USB detection |
| `0x0800cfdc` | `key press down!` | Button handling |
| `0x0800d004` | `key press power on` | Power button |
| `0x08003f24` | `env file size too large` | Env validation |
| `0x0800459f` | `into usb host mode` | USB host mode |
| `0x080045b4` | `into usb device mode` | USB device mode |

### 8. Ghidra project

Location: `/home/marek/tmp/kosiarka/ghidra_proj/Mower_Firmware.gpr`

Status:
- `u13/firmware/u13_flash.bin` — imported and analyzed (1672 functions)
- `u16/firmware/u16_flash.bin` — not imported (simple UART bridge, no PIN logic)

### 9. Recreating from scratch (on another computer)

```bash
# 1. Install Ghidra 12.1.2
unzip ghidra_12.1.2_PUBLIC_20260605.zip
# 2. Install JDK 21+ with JDK (javac required!)
# 3. Build ghidra-cli
cd ghidra-cli
cargo build --release
# 4. Modify ghidra-cli (see section 6 — bridge.rs changes)
# 5. Configure
export JAVA_HOME=/path/to/jdk-21
export GHIDRA_INSTALL_DIR=/path/to/ghidra_12.1.2_PUBLIC
ghidra doctor
# 6. Import binary
# 7. Run analysis
# 8. Done
```
