#!/bin/bash
# Cykliczne wyszukiwanie SNK kosiarek na OLX
# Uruchamiaj: bash tools/olx_weekly_search.sh
#
# Szuka uszkodzonych/zablokowanych egzemplarzy do 300 zł
# Przeszukuje wszystkie aliasy platformy SNK

OUTDIR="olx_results/$(date +%Y-%m-%d)"
mkdir -p "$OUTDIR"

cat > "$OUTDIR/README.txt" << 'EOF'
OTWÓRZ TE LINKI W PRZEGLĄDARCE:

Wyszukiwarki OLX (posortowane od najnowszych, do 300 zł):
EOF

# Lista wyszukiwań do ręcznego otwarcia w przeglądarce
cat >> "$OUTDIR/README.txt" << 'MANUAL'

═══ Lux Tools A-RMR-300-24 ═══
https://www.olx.pl/dom-ogrod/ogrod/kosiarki/q-Lux-Tools-A-RMR-300-24/?search%5Border%5D=created_at:desc&search%5Bfilter_float_price:to%5D=300

═══ robot koszący Lux Tools ═══
https://www.olx.pl/dom-ogrod/ogrod/kosiarki/q-Lux+Tools+robot+kosz%C4%85cy/?search%5Border%5D=created_at:desc&search%5Bfilter_float_price:to%5D=300

═══ Adano RM5 ═══
https://www.olx.pl/dom-ogrod/ogrod/kosiarki/q-Adano+RM5/?search%5Border%5D=created_at:desc&search%5Bfilter_float_price:to%5D=300

═══ Brucke RM500 ═══
https://www.olx.pl/dom-ogrod/ogrod/kosiarki/q-Brucke+RM500/?search%5Border%5D=created_at:desc&search%5Bfilter_float_price:to%5D=300

═══ Scheppach BRMR300 ═══
https://www.olx.pl/dom-ogrod/ogrod/kosiarki/q-Scheppach+BRMR300/?search%5Border%5D=created_at:desc&search%5Bfilter_float_price:to%5D=300

═══ robot koszący uszkodzony (ogólnie) ═══
https://www.olx.pl/dom-ogrod/ogrod/kosiarki/q-robot+kosz%C4%85cy/?search%5Border%5D=created_at:desc&search%5Bfilter_enum_state%5D%5B0%5D=used&search%5Bfilter_float_price:to%5D=300

═══ robot koszący blokada pin (w całym OLX, nie tylko kosiarki) ═══
https://www.olx.pl/search/?search%5Border%5D=created_at:desc&search%5Bfilter_float_price:to%5D=300&q=robot+kosz%C4%85cy+pin+blokada
MANUAL

echo "=== OLX Weekly Search $(date) ==="
echo ""
echo "Zapisano linki do: $OUTDIR/README.txt"
echo ""
echo "Otwórz w przeglądarce i sprawdź nowe oferty."
echo "Zapisz wyniki w katalogu olx_results/"

# Also try to auto-search via web
echo ""
echo "=== Próba auto-wyszukania ==="

for query in \
  "OLX Lux Tools A-RMR-300-24 uszkodzony cena do 300" \
  "OLX robot koszący zablokowany pin 300 zł" \
  "OLX Adano RM5 używane cena" \
  "OLX Brucke RM500 uszkodzony" \
  "OLX Scheppach BRMR300 używany"; do
  echo "--- $query ---"
done

echo ""
echo "=== Gotowe ==="
