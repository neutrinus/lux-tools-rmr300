# Firmware Decompilation Setup

## Jak uruchomić ghidra-cli do dekompilacji firmware

### 1. Wymagania

| Narzędzie | Ścieżka |
|-----------|---------|
| Ghidra 12.1.2 | `/home/marek/tmp/kosiarka/ghidra_extracted/ghidra_12.1.2_PUBLIC` |
| ghidra-cli (zmodyfikowany) | `/home/marek/tmp/kosiarka/ghidra-cli/target/release/ghidra` |
| JDK 21 (Adoptium Temurin) | `/home/marek/tmp/kosiarka/jdk-21.0.6+7` |

### 2. Zmienne środowiskowe

```bash
export JAVA_HOME=/home/marek/tmp/kosiarka/jdk-21.0.6+7
export PATH=$JAVA_HOME/bin:$PATH
export GHIDRA_INSTALL_DIR=/home/marek/tmp/kosiarka/ghidra_extracted/ghidra_12.1.2_PUBLIC

alias ghidra="$GHIDRA_INSTALL_DIR/../ghidra-cli/target/release/ghidra"
```

### 3. Konfiguracja (jednorazowo)

```bash
ghidra config set ghidra_install_dir "$GHIDRA_INSTALL_DIR"
ghidra config set ghidra_project_dir /home/marek/tmp/kosiarka/ghidra_proj
ghidra config set default_project Mower_Firmware
```

### 4. Import i analiza

```bash
# Import raw binary jako ARM Cortex-M4
"$GHIDRA_INSTALL_DIR/support/analyzeHeadless" \
  /home/marek/tmp/kosiarka/ghidra_proj Mower_Firmware \
  -import /home/marek/tmp/kosiarka/u13_flash.bin \
  -overwrite \
  -processor ARM:LE:32:Cortex \
  -cspec default \
  -loader BinaryLoader \
  -loader-baseAddr 0x08000000 \
  -noanalysis

# Uruchom bridge + analizę (bridge kompiluje .java → .class i odpala Ghidrę)
ghidra analyze --project Mower_Firmware --program u13_flash.bin -vv
```

### 5. Najczęstsze komendy

```bash
# Lista funkcji
ghidra function list --project Mower_Firmware --program u13_flash.bin

# Filtrowanie funkcji
ghidra function list --filter "name ~ usb OR name ~ key" --project Mower_Firmware

# Dekompilacja konkretnej funkcji
ghidra decompile FUN_0800cf38 --project Mower_Firmware --program u13_flash.bin

# Wyszukiwanie stringów
ghidra find string "FORMATFLASH" --project Mower_Firmware --program u13_flash.bin

# Cross-reference (gdzie string/funkcja jest używana)
ghidra x-ref to 0x0800300c --project Mower_Firmware --program u13_flash.bin

# Cross-reference FROM
ghidra x-ref from 0x0800cf38 --project Mower_Firmware --program u13_flash.bin

# Podsumowanie programu
ghidra summary --project Mower_Firmware --program u13_flash.bin

# Wyświetl memory map
ghidra memory list --project Mower_Firmware --program u13_flash.bin

# Lista stringów
ghidra strings list --project Mower_Firmware --program u13_flash.bin
```

### 6. Znane problemy i rozwiązania

#### Problem: OSGi w Ghidra 12.1.2 nie kompiluje `.java` skryptów

Ghidra 12.1.2 ma uszkodzoną kompilację Java przez OSGi (Phidias/BND) — nawet pusty skrypt
`extends GhidraScript` nie przechodzi kompilacji. Dotyczy Java 21+ i Java 25.

**Rozwiązanie:** ghidra-cli został zmodyfikowany (`src/ghidra/bridge.rs`):
1. Po zapisaniu `GhidraCliBridge.java` na dysk, uruchamia `javac` z pełnym classpathem Ghidry
2. Kompiluje do `.class` w `~/.config/ghidra-cli/class_scripts/`
3. Uruchamia `analyzeHeadless` z `-postScript GhidraCliBridge.class` (z pominięciem OSGi)
4. HeadlessAnalyzer ładuje `.class` bezpośrednio przez `classLoaderForDotClassScripts`

Modyfikacje w `GhidraCliBridge.java`:
- `CParserUtils.parseSignature()` API zmieniło się w Ghidra 12.x — zwraca `FunctionDefinitionDataType`
  bezpośrednio zamiast `CParseResults.getDataType()`. Linia 2541-2549 poprawiona.

#### Problem: Java 25 z Ghidrą

Systemowy JDK to Java 25-openjdk. Ghidra 12.1.2 wymaga JDK ≥ 21.
Pobrano Adoptium Temurin JDK 21.0.6+7 do `/home/marek/tmp/kosiarka/jdk-21.0.6+7`.

```bash
export JAVA_HOME=/home/marek/tmp/kosiarka/jdk-21.0.6+7
```

`java_home.save` jest ignorowany przez Ghidra — `JAVA_HOME` w env jest konieczny.

#### Problem: Brak x-refów dla stringów

Po analizie część stringów (np. `FORMATFLASH.json`, `ready to format flash`)
nie ma cross-reference — kod nie został zdysasemblowany w miejscach które je
referencjonują. Możliwe przyczyny:
- kod jest w niezdysasemblowanej gałęzi (dead code)
- stringi są używane przez wskaźniki w tablicach (data-driven)
- analiza nie wykryła wszystkich skoków (np. przez tabele skoków,vtables)

Aby znaleźć użycie, trzeba przeszukać binarnie offset stringa lub prześledzić
xref-y ręcznie.

### 7. Wyniki analizy u13_flash.bin (GD32F305)

| Parametr | Wartość |
|----------|---------|
| Architektura | ARM Cortex-M4 Thumb2 |
| Base address | `0x08000000` |
| Rozmiar | 512 KB (`0x80000`) |
| Znalezione funkcje | **1672** |
| Format wejściowy | Raw Binary |
| Język | `ARM:LE:32:Cortex` |

#### Kluczowe stringi

| Adres | String | Znaczenie |
|-------|--------|-----------|
| `0x0800300c` | `env_read.json` | Konfiguracja w JSON |
| `0x08003990` | `FORMATFLASH.json` | Przełącznik formatowania flash |
| `0x080039a4` | `ready to format flash` | Gotowość do formatu |
| `0x080039bc` | `format flash` | Wykonanie formatu |
| `0x0800394c` | `USB disk Ready` | Detekcja USB |
| `0x0800cfdc` | `key press down!` | Obsługa przycisków |
| `0x0800d004` | `key press power on` | Przycisk power |
| `0x08003f24` | `env file size too large` | Walidacja env |
| `0x0800459f` | `into usb host mode` | Tryb USB host |
| `0x080045b4` | `into usb device mode` | Tryb USB device |

### 8. Projekt Ghidry

Lokalizacja: `/home/marek/tmp/kosiarka/ghidra_proj/Mower_Firmware.gpr`

Stan:
- `u13_flash.bin` — zaimportowany i przeanalizowany (1672 funkcje)
- `u16_flash.bin` — niezaimportowany (prosty UART bridge, brak PIN logiki)

### 9. Odtworzenie od zera (na innym komputerze)

```bash
# 1. Zainstaluj Ghidra 12.1.2
unzip ghidra_12.1.2_PUBLIC_20260605.zip
# 2. Zainstaluj JDK 21+ z JDK (javac wymagany!)
# 3. Zbuduj ghidra-cli
cd ghidra-cli
cargo build --release
# 4. Zmodyfikuj ghidra-cli (patrz sekcja 6. — zmiany w bridge.rs)
# 5. Skonfiguruj
export JAVA_HOME=/path/to/jdk-21
export GHIDRA_INSTALL_DIR=/path/to/ghidra_12.1.2_PUBLIC
ghidra doctor
# 6. Zaimportuj binarkę
# 7. Uruchom analizę
# 8. Gotowe
```
