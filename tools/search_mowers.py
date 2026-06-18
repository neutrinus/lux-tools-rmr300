#!/usr/bin/env python3
"""Wyszukiwarka SNK - Playwright + Firefox, omija blokady botów.

Usage:
  source .venv/bin/activate
  python3 tools/search_mowers.py                  # automatyczne szukanie (OLX, Blocket, KA)
  python3 tools/search_mowers.py --get-cookies    # otwórz Allegro, wypełnij captchę, zapisz ciasteczka
"""

import sys, os, json, argparse
from datetime import datetime
from pathlib import Path
from playwright.sync_api import sync_playwright

HERE = Path(__file__).resolve().parent.parent
os.chdir(HERE)

OUTDIR = Path("results") / datetime.now().strftime("%Y-%m-%d")
OUTDIR.mkdir(parents=True, exist_ok=True)
COOKIE_FILE = HERE / ".allegro_cookies.json"

MAX_PRICE = 300

OLX_QUERIES = [
    "Lux Tools A-RMR-300-24", "Adano RM5",
    "Scheppach BRMR300", "Scheppach BTRM300", "Scheppach RRMA300",
    "Brucke RM500", "Brucke RM501", "Brucke RM800",
    "Gomag Go-MR300", "Grouw City 300",
    "Meec Tools", "Julan", "Smart 365",
]

BLOCKET_QUERIES = [
    "scheppach brmr300", "scheppach btrm300", "scheppach rrma300",
    "meec tools robotgräsklippare", "brucke rm500",
    "adano rm5", "gomag go-mr300", "grouw city 300",
    "julan robotgräsklippare", "sunseeker robotgräsklippare",
]

KLEINANZEIGEN_QUERIES = [
    "scheppach+brmr300", "scheppach+btrm300", "scheppach+rrma300",
    "lux+tools+a-rmr-300-24", "brucke+rm500",
    "adano+rm5", "gomag+go-mr300", "meec+tools+maehroboter",
]

ALLEGRO_URLS = [
    ("Kategoria roboty koszące", "https://allegro.pl/kategoria/roboty-koszace-154355?condition=USED&order=qd"),
    ("Scheppach BRMR300", "https://allegro.pl/listing?string=scheppach+brmr300&condition=USED&order=qd"),
    ("Scheppach BTRM300", "https://allegro.pl/listing?string=scheppach+btrm300&condition=USED&order=qd"),
    ("Brucke RM500", "https://allegro.pl/listing?string=brucke+rm500&condition=USED&order=qd"),
    ("Lux Tools A-RMR-300-24", "https://allegro.pl/listing?string=lux+tools+a-rmr-300-24&condition=USED&order=qd"),
    ("Adano RM5", "https://allegro.pl/listing?string=adano+rm5&condition=USED&order=qd"),
    ("Gomag Go-MR300", "https://allegro.pl/listing?string=gomag+go-mr300&condition=USED&order=qd"),
]

ALLEGROLOKALNIE = "https://allegrolokalnie.pl/oferty/roboty-koszace?conditionType=USED&priceTo=300"
EBAY_URLS = [
    "https://www.ebay.de/sch/i.html?_nkw=scheppach+brmr300&_sop=10",
    "https://www.ebay.de/sch/i.html?_nkw=brucke+rm500&_sop=10",
]


def extract_listings(page, container_sel, title_sel, price_sel, link_attr, url_prefix, keywords):
    results = []
    containers = page.query_selector_all(container_sel) if container_sel else [page]
    for el in containers:
        title_el = el.query_selector(title_sel)
        if not title_el:
            continue
        title = title_el.inner_text().strip()
        if not title:
            continue
        href = title_el.get_attribute(link_attr) or ""
        price_el = el.query_selector(price_sel) if price_sel else None
        price = price_el.inner_text().strip() if price_el else ""
        if any(kw in title.lower() for kw in keywords):
            url = href if href.startswith("http") else url_prefix + href
            results.append({"title": title[:100], "price": price, "url": url})
    return results


def search_olx(page, query):
    q = query.replace(" ", "+")
    url = f"https://www.olx.pl/dom-ogrod/ogrod/kosiarki/q-{q}/?search%5Border%5D=created_at:desc&search%5Bfilter_float_price:to%5D={MAX_PRICE}"
    page.goto(url, wait_until="domcontentloaded", timeout=20000)
    page.wait_for_timeout(2000)
    kw = ["robot", "kosiarka", "koszący", "koszacy", "landroid", query.lower().split()[-1]]
    return extract_listings(page, "", "a[href*='/d/oferta/'] h6, a[href*='/d/oferta/'] h4",
                            "[data-testid='ad-price'], p[data-size]", "href",
                            "https://www.olx.pl", kw)


def search_blocket(page, query):
    url = f"https://www.blocket.se/annonser?q={query.replace(' ', '+')}&sort=date"
    page.goto(url, wait_until="domcontentloaded", timeout=20000)
    page.wait_for_timeout(2000)
    kw = ["robot", "meec", "scheppach", "brucke", "adano", "gomag", "grouw", "julan", "sunseeker"]
    return extract_listings(page, "article", "h2, a[href*='/annons/']",
                            "div[class*='price'], strong, span[class*='price']",
                            "href", "https://www.blocket.se", kw)


def search_kleinanzeigen(page, query):
    url = f"https://www.kleinanzeigen.de/s-{query}/k0"
    page.goto(url, wait_until="domcontentloaded", timeout=20000)
    page.wait_for_timeout(3000)
    kw = ["maehroboter", "scheppach", "brucke", "lux tools", "adano", "gomag", "meec"]
    return extract_listings(page, "article, div[class*='aditem']",
                            "a[class*='ellipsis'], h2 a, a[href*='/anzeige/']",
                            "span[class*='price'], strong",
                            "href", "https://www.kleinanzeigen.de", kw)


def search_allegro(page, label, url):
    page.goto(url, wait_until="domcontentloaded", timeout=30000)
    page.wait_for_timeout(3000)
    # DataDome captcha? Sprawdź
    if "captcha-delivery" in page.content() or page.url != url and "captcha" in page.url:
        raise Exception("DataDome captcha - użyj --get-cookies by wypełnić ręcznie")
    kw = ["robot", "kosiarka", "scheppach", "brucke", "lux", "adano", "gomag", "meec"]
    return extract_listings(page, "article, div[data-box-name], section",
                            "h2 a, a[class*='title'], a[href*='/oferta/'], h3 a",
                            "span[class*='price'], div[class*='price']",
                            "href", "https://allegro.pl", kw)


def main():
    ap = argparse.ArgumentParser(description="Wyszukiwarka SNK")
    ap.add_argument("--get-cookies", action="store_true",
                    help="Otwórz Allegro w Firefoxie -> wypełnij captchę -> zamknij okno -> ciasteczka zapisane")
    args = ap.parse_args()

    with sync_playwright() as p:
        browser = p.firefox.launch(headless=False)
        context = browser.new_context(
            viewport={"width": 1360, "height": 900},
            locale="pl-PL",
            timezone_id="Europe/Warsaw",
        )
        context.set_default_timeout(30000)
        page = context.new_page()

        if args.get_cookies:
            print("Otwieram Allegro. Wypełnij captchę/cokolwiek potrzeba,")
            print("poczekaj aż strona się załaduje, potem zamknij okno.")
            print("Ciasteczka zostaną zapisane do .allegro_cookies.json\n")
            page.goto("https://allegro.pl/kategoria/roboty-koszace-154355?condition=USED&order=qd")
            page.wait_for_event("close", timeout=0)
            cookies = context.cookies()
            with open(COOKIE_FILE, "w") as f:
                json.dump(cookies, f)
            print(f"Zapisano {len(cookies)} ciasteczek do {COOKIE_FILE}")
            browser.close()
            return

        # Normalny tryb - automatyczne szukanie
        # Załaduj ciasteczka Allegro jeśli istnieją
        if COOKIE_FILE.exists():
            with open(COOKIE_FILE) as f:
                cookies = json.load(f)
            context.add_cookies(cookies)
            print(f"Załadowano {len(cookies)} ciasteczek Allegro")

        print(f"=== Wyszukiwanie SNK: {datetime.now():%Y-%m-%d %H:%M} ===\n")
        all_by_portal = {}

        # OLX
        print("── OLX ──")
        for q in OLX_QUERIES:
            print(f"  [{q[:40]}]...", end=" ", flush=True)
            try:
                r = search_olx(page, q)
                if r:
                    print(f"✅ {len(r)}")
                    all_by_portal.setdefault("OLX", []).extend(r)
                    for i in r:
                        print(f"    {i['price']:>12}  {i['title'][:70]}")
                else:
                    print("❌")
            except Exception as e:
                print(f"❌ {e}")

        # Blocket
        print("\n── Blocket ──")
        for q in BLOCKET_QUERIES:
            print(f"  [{q[:40]}]...", end=" ", flush=True)
            try:
                r = search_blocket(page, q)
                if r:
                    print(f"✅ {len(r)}")
                    all_by_portal.setdefault("Blocket", []).extend(r)
                    for i in r:
                        print(f"    {i['price']:>12}  {i['title'][:70]}")
            except Exception as e:
                print(f"❌ {e}")

        # Kleinanzeigen
        print("\n── Kleinanzeigen ──")
        for q in KLEINANZEIGEN_QUERIES:
            print(f"  [{q[:40]}]...", end=" ", flush=True)
            try:
                r = search_kleinanzeigen(page, q)
                if r:
                    print(f"✅ {len(r)}")
                    all_by_portal.setdefault("Kleinanzeigen", []).extend(r)
                    for i in r:
                        print(f"    {i['price']:>12}  {i['title'][:70]}")
            except Exception as e:
                print(f"❌ {e}")

        # Allegro (tylko jeśli mamy ciasteczka)
        if COOKIE_FILE.exists():
            print("\n── Allegro ──")
            for label, url in ALLEGRO_URLS:
                print(f"  [{label}]...", end=" ", flush=True)
                try:
                    r = search_allegro(page, label, url)
                    if r:
                        print(f"✅ {len(r)}")
                        all_by_portal.setdefault("Allegro", []).extend(r)
                        for i in r:
                            print(f"    {i['price']:>12}  {i['title'][:70]}")
                    else:
                        print("❌ brak")
                except Exception as e:
                    print(f"❌ {e}")
        else:
            print("\n── Allegro ──")
            print(f"  Brak ciasteczek. Uruchom najpierw: python3 tools/search_mowers.py --get-cookies")

        # Raport
        total = sum(len(v) for v in all_by_portal.values())
        summary = OUTDIR / "podsumowanie.txt"
        with open(summary, "w", encoding="utf-8") as f:
            f.write(f"Wyszukiwanie SNK: {datetime.now():%Y-%m-%d %H:%M}\n\n")
            if not total:
                f.write("Brak ofert SNK.\n")
            else:
                f.write(f"Znaleziono {total} ofert:\n\n")
                for portal, items in all_by_portal.items():
                    f.write(f"── {portal} ──\n")
                    for i in items:
                        f.write(f"{i['title']}\n{i['price']}\n{i['url']}\n\n")

        print(f"\n{'='*50}")
        print(f"Znaleziono: {total} ofert")
        print(f"Raport: {summary}")

        browser.close()


if __name__ == "__main__":
    main()
