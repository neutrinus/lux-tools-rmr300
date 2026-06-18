#!/bin/bash
# Cotygodniowe wyszukiwanie - tylko platforma SNK
# Uruchamiaj: bash tools/search_mowers.sh
#
# Szuka kosiarek SNK (Lux Tools, Brucke, Scheppach, Adano, Gomag itp.)
# do 300 zł na OLX i Kleinanzeigen

OUTDIR="results/$(date +%Y-%m-%d)"
mkdir -p "$OUTDIR"
SUMMARY="$OUTDIR/podsumowanie.txt"

echo "============================================" > "$SUMMARY"
echo "  Wyszukiwanie SNK: $(date)" >> "$SUMMARY"
echo "============================================" >> "$SUMMARY"
echo "" >> "$SUMMARY"

# ─── OLX ───────────────────────────────────────────────
echo "═══ OLX ═══" >> "$SUMMARY"

fetch_olx() {
  local query="$1"
  local label="$2"
  local url="https://www.olx.pl/dom-ogrod/ogrod/kosiarki/q-${query}/?search%5Border%5D=created_at:desc&search%5Bfilter_float_price:to%5D=300"
  echo "[$label] $url" >> "$SUMMARY"
  echo "  -> $url"
}

# Wszystkie aliasy SNK
fetch_olx "Lux+Tools+A-RMR-300-24" "Lux Tools A-RMR-300-24"
fetch_olx "Adano+RM5" "Adano RM5"
fetch_olx "Scheppach+BRMR300" "Scheppach BRMR300"
fetch_olx "Scheppach+BTRM300" "Scheppach BTRM300"
fetch_olx "Scheppach+RRMA300" "Scheppach RRMA300"
fetch_olx "Brucke+RM500" "Brucke RM500"
fetch_olx "Brucke+RM501" "Brucke RM501"
fetch_olx "Brucke+RM800" "Brucke RM800"
fetch_olx "Gomag+Go-MR300" "Gomag Go-MR300"
fetch_olx "Grouw+City+300" "Grouw City 300"
fetch_olx "Meec+Tools+kosiarka+robot" "Meec Tools robot"
fetch_olx "Julan+robot+kosz%C4%85cy" "Julan robot"
fetch_olx "Landxcape+robot+kosz%C4%85cy" "Landxcape robot"
fetch_olx "Sunseeker+robot+kosz%C4%85cy" "Sunseeker robot"
fetch_olx "Smart+365+robot+kosz%C4%85cy" "Smart 365 robot"
fetch_olx "kosiarka+robot+300m2" "robot 300m2"
fetch_olx "kosiarka+automatyczna+300" "automat 300"

echo "" >> "$SUMMARY"

# ─── eBay Kleinanzeigen (DE) ──────────────────────────
echo "═══ eBay Kleinanzeigen (DE) ═══" >> "$SUMMARY"

for keyword in "scheppach-brmr300" "scheppach-btrm300" "scheppach-rrma300" "lux-tools-a-rmr-300-24" "brucke-rm500" "brucke-rm501" "brucke-rm800" "adano-rm5" "gomag-go-mr300" "grouw-city-300" "smart-365-maehroboter" "meec-tools-maehroboter" "julan-maehroboter" "landxcape-maehroboter" "sunseeker-maehroboter"; do
  echo "https://www.kleinanzeigen.de/s-${keyword}/k0" >> "$SUMMARY"
done

echo "" >> "$SUMMARY"

# ─── Blocket (Szwecja) ─────────────────────────────────
echo "═══ Blocket (SE) ═══" >> "$SUMMARY"

for query in "scheppach+brmr300" "scheppach+btrm300" "scheppach+rrma300" "lux+tools+robotgr%C3%A4sklippare" "brucke+rm500" "adano+rm5" "gomag+go-mr300" "grouw+city+300" "meec+tools+robotgr%C3%A4sklippare" "julan+robotgr%C3%A4sklippare" "sunseeker+robotgr%C3%A4sklippare"; do
  echo "https://www.blocket.se/annonser?q=${query}&sort=date" >> "$SUMMARY"
done

echo "" >> "$SUMMARY"

# ─── eBay (DE) ─────────────────────────────────────────
echo "═══ eBay (DE) ═══" >> "$SUMMARY"

for query in "scheppach+brmr300" "scheppach+btrm300" "scheppach+rrma300" "lux+tools+a-rmr-300-24" "brucke+rm500" "adano+rm5" "gomag+go-mr300" "grouw+city+300" "meec+tools+maehroboter" "sunseeker+maehroboter"; do
  echo "https://www.ebay.de/sch/i.html?_nkw=${query}&_sop=10" >> "$SUMMARY"
done

echo "" >> "$SUMMARY"

# ─── Podsumowanie ──────────────────────────────────────
echo "============================================" >> "$SUMMARY"
echo "  INSTRUKCJA" >> "$SUMMARY"
echo "============================================" >> "$SUMMARY"
echo "" >> "$SUMMARY"
echo "1. Otwórz linki z OLX w przeglądarce" >> "$SUMMARY"
echo "2. Otwórz linki z Kleinanzeigen w przeglądarce" >> "$SUMMARY"
echo "3. Otwórz linki z Blocket w przeglądarce" >> "$SUMMARY"
echo "4. Otwórz linki z eBay w przeglądarce" >> "$SUMMARY"
echo "5. Ręcznie Allegro:" >> "$SUMMARY"
echo "   https://allegro.pl/kategoria/roboty-koszace-154355?condition=USED&order=qd" >> "$SUMMARY"
echo "     - szukaj: scheppach, brucke, lux tools, adano" >> "$SUMMARY"
echo "6. Allegro Lokalnie:" >> "$SUMMARY"
echo "   https://allegrolokalnie.pl/oferty/roboty-koszace?conditionType=USED&priceTo=300" >> "$SUMMARY"
echo "" >> "$SUMMARY"
echo "Zapisano: $SUMMARY" >> "$SUMMARY"

cat "$SUMMARY"
