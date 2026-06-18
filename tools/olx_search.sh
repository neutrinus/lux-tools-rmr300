#!/bin/bash
# Weekly OLX search for damaged/pin-locked SNK mowers under 300 PLN
# Run this manually or via cron every week
#
# Usage: bash tools/olx_search.sh
# Results saved to: olx_results/YYYY-MM-DD/

set -euo pipefail

OUTDIR="olx_results/$(date +%Y-%m-%d)"
mkdir -p "$OUTDIR"

QUERIES=(
  "Lux Tools A-RMR-300-24"
  "Lux Tools robot koszący"
  "Adano RM5"
  "Brucke RM500"
  "Scheppach BRMR300"
  "robot koszący uszkodzony pin"
  "kosiarka zablokowana"
)

echo "=== OLX Search $(date) ===" | tee "$OUTDIR/summary.txt"

for q in "${QUERIES[@]}"; do
  slug=$(echo "$q" | tr '[:upper:]' '[:lower:]' | sed 's/ /-/g')
  url="https://www.olx.pl/dom-ogrod/ogrod/kosiarki/q-$(echo "$q" | sed 's/ /%20/g')/?search%5Bfilter_enum_state%5D%5B0%5D=used&search%5Bfilter_float_price%3Ato%5D=300"
  echo "$q -> $url" >> "$OUTDIR/summary.txt"
  echo "[$q] Saved URL: $url"
done

echo "" >> "$OUTDIR/summary.txt"
echo "=== Manual search URLs ===" >> "$OUTDIR/summary.txt"

# Also generate direct OLX category URLs sorted by price
BASE="https://www.olx.pl/dom-ogrod/ogrod/kosiarki/q-"
for term in "Lux+Tools+A-RMR-300-24" "Adano+RM5" "Brucke+RM500" "Scheppach+BRMR300"; do
  echo "${BASE}${term}/?search%5Border%5D=created_at%3Adesc&search%5Bfilter_float_price%3Ato%5D=300" >> "$OUTDIR/summary.txt"
done

cat "$OUTDIR/summary.txt"
