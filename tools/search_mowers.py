#!/usr/bin/env python3
"""Wyszukiwarka SNK — Lux Tools A-RMR-300-24 i klony (platforma SNK OEM).

Trzy portale obsługiwane tanio, po zwykłym HTTP (curl_cffi z impersonacją
Firefoksa — CloudFront/WAF przepuszcza odcisk Firefoksa, blokuje OpenSSL
Pythona):

  OLX      -> publiczne API /api/v1/offers (JSON: pełny opis, cena, link)
  Kleinanzeigen -> kleinanzeigen.de (HTML, article[data-adid], cena, wysyłka)
  Blocket  -> blocket.se (base64 React Query state -> docs, SEK)

Allegro ma DataDome i wymaga prawdziwej przeglądarki + ręcznie rozwiązanej
captchy. Raz na jakiś czas:

  .venv/bin/python tools/search_mowers.py --login     # otwórz Allegro, kliknij captchę

Sesja zapisuje się w trwałym profilu .browser_profile_allegro, więc kolejne
skany Allegro (--allegro-only / pełny skan) działają już bez captchy.

Użycie:
  .venv/bin/python tools/search_mowers.py                     # OLX+KA+Blocket (+Allegro jeśli sesja)
  .venv/bin/python tools/search_mowers.py --allegro-only      # tylko Allegro
  .venv/bin/python tools/search_mowers.py --all-prices        # bez limitu ceny
  .venv/bin/python tools/search_mowers.py --portals olx,ka    # wybrane portale
"""

import argparse
import base64
import html
import json
import logging
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
os.chdir(HERE)

try:
    from curl_cffi import requests as _cffi
except Exception:                                   # pragma: no cover
    _cffi = None

try:
    from playwright.sync_api import sync_playwright
except Exception:                                   # pragma: no cover
    sync_playwright = None

OUTDIR = Path("results") / datetime.now().strftime("%Y-%m-%d")
OUTDIR.mkdir(parents=True, exist_ok=True)
ALLEGRO_PROFILE = HERE / ".browser_profile_allegro"
COOKIE_FILE = HERE / ".allegro_cookies.json"

LOG = logging.getLogger("snk")

UA = "Mozilla/5.0 (X11; Linux x86_64; rv:152.0) Gecko/20100101 Firefox/152.0"

# Limity cenowe (miękkie) — droższe sztuki pomijamy, licząc je w raporcie.
# Służą do łowienia tanich/uszkodzonych egzemplarzy do reverse engineeringu.
MAX_PRICE = {"PLN": 600, "EUR": 160, "SEK": 1800}


# ---------------------------------------------------------------------------
# HTTP (curl_cffi, fallback urllib)
# ---------------------------------------------------------------------------

def http_get(url, headers=None, timeout=30, impersonate="firefox"):
    """GET z odciskiem TLS Firefoksa. Zwraca (status, text)."""
    h = {"User-Agent": UA}
    h.update(headers or {})
    if _cffi is not None:
        r = _cffi.get(url, headers=h, impersonate=impersonate, timeout=timeout)
        return r.status_code, r.text
    req = urllib.request.Request(url, headers=h)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


# ---------------------------------------------------------------------------
# Rozpoznawanie platformy SNK (po tytule + opisie)
# ---------------------------------------------------------------------------

# (nazwa modelu, wzorzec)
SNK_RULES = [
    ("Lux Tools A-RMR-300-24", r"a[\s.-]?rmr[\s.-]?300[\s.-]?24"),
    ("Lux Tools ARM-320-V",    r"arm[\s.-]?320[\s.-]?v"),
    ("Lux Tools Oryx 300",     r"oryx\s*300|a[\s.-]?rmr[\s.-]?300[\s.-]?26"),
    ("Scheppach BRMR300",      r"brmr\s?-?300"),
    ("Scheppach BTRM300",      r"btrm\s?-?300"),
    ("Scheppach RRMA300",      r"rrma\s?-?300"),
    ("Brucke RM500",           r"brucke\s*rm\s?-?500"),
    ("Brucke RM501",           r"brucke\s*rm\s?-?501"),
    ("Brucke RM800",           r"brucke\s*rm\s?-?800"),
    ("Adano RM5",              r"adano\s*rm\s?-?5\b"),
    ("Gomag Go-MR300",         r"go[\s.-]?mr\s?-?300|gomag"),
    ("Grouw City 300",         r"grouw\s+city\s+300"),
    ("Smart 365",              r"smart\s+365"),
    ("Meec Tools 300 m²",      r"meec.{0,40}?(?:300\s?m|027415|\bme\s?-?340\b)"),
    ("Julan 300 m²",           r"julan.{0,25}300"),
    ("Sunseeker V1 Vision",    r"sunseeker.{0,30}?(?:v1|vision)"),
]

# Marki platformy SNK (do trafień niepewnych — bez kodu modelu)
_LOW_BRAND = re.compile(
    r"lux[\s-]?tools|scheppach|brucke|adano|gomag|grouw|meec|julan|"
    r"landxcape|sunseeker|oryx")
# Kontekst „robota koszącego" — odsiewa zwykłe kosiarki i narzędzia tych firm
_ROBOT_CTX = re.compile(
    r"robot|m[äa]hroboter|robotklipp|gr[äa]sklipparrobot|"
    r"kosiarka\s+automatyczn|kosz[ąa]c")


def classify(title, desc=""):
    """Zwraca (nazwa_modelu, pewność) dla oferty SNK albo (None, None).
    'low' = prawdopodobnie SNK (marka bez kodu modelu), wymaga weryfikacji."""
    text = f"{title or ''} {desc or ''}".lower()
    # Meec 800 m² to inna (większa) konstrukcja — nie SNK
    if "meec" in text and re.search(r"\b800\s?m", text) and not re.search(r"\b300\s?m", text):
        return None, None
    for name, pat in SNK_RULES:
        if re.search(pat, text):
            return name, "high"
    if _LOW_BRAND.search(text) and _ROBOT_CTX.search(text):
        return "SNK? (marka bez modelu — zweryfikuj)", "low"
    return None, None


# ---------------------------------------------------------------------------
# Wspólne
# ---------------------------------------------------------------------------

def fmt_price(price, currency):
    if price is None:
        return "?"
    try:
        return f"{int(round(price)):,} {currency}".replace(",", " ")
    except (TypeError, ValueError):
        return f"{price} {currency}"


def to_int(num):
    """'1 234,56' / '1.234' / '1234' -> int (heurystyka PL/DE)."""
    if num is None:
        return None
    s = str(num).replace("\u00a0", " ").strip()
    s = re.sub(r"[^\d.,]", "", s)
    if not s:
        return None
    if "," in s and "." in s:                    # 1.234,56 -> 1234 ; 1,234.56 -> 1234
        if s.rfind(",") > s.rfind("."):
            s = s.replace(".", "").replace(",", ".")
        else:
            s = s.replace(",", "")
    elif "," in s:
        s = s.replace(",", ".") if len(s.split(",")[-1]) <= 2 else s.replace(",", "")
    try:
        return int(round(float(s)))
    except ValueError:
        return None


def over_budget(price, currency, allow_all):
    if allow_all or price is None:
        return False
    cap = MAX_PRICE.get(currency or "")
    return cap is not None and price > cap


# ---------------------------------------------------------------------------
# OLX — publiczne API
# ---------------------------------------------------------------------------

OLX_API = "https://www.olx.pl/api/v1/offers/"
OLX_LIMIT = 40
OLX_HEADERS = {"Accept": "application/json", "Accept-Language": "pl-PL,pl;q=0.9"}


def _olx_price(it):
    for p in it.get("params") or []:
        if (p or {}).get("key") != "price":
            continue
        v = p.get("value") or {}
        if not isinstance(v, dict) or v.get("budget") or v.get("free") or v.get("exchange"):
            return None, None
        if v.get("value") is None:
            return None, None
        try:
            return float(v["value"]), (v.get("currency") or "PLN")
        except (TypeError, ValueError):
            return None, None
    return None, None


def _clean_html(text):
    if not text:
        return ""
    t = re.sub(r"<br\s*/?>", "\n", text, flags=re.I)
    t = re.sub(r"<[^>]+>", " ", t)
    return re.sub(r"[ \t]{2,}", " ", t).strip()


def _olx_map(it):
    price, cur = _olx_price(it)
    loc = it.get("location") or {}
    city = ((loc.get("city") or {}) if isinstance(loc.get("city"), dict) else {}).get("name")
    region = ((loc.get("region") or {}) if isinstance(loc.get("region"), dict) else {}).get("name")
    place = ", ".join(x for x in (city, region) if x)
    return {
        "portal": "OLX",
        "external_id": str(it.get("id") or ""),
        "title": (it.get("title") or "").strip(),
        "description": _clean_html(it.get("description"))[:4000],
        "price": price,
        "currency": cur or "PLN",
        "url": it.get("url"),
        "location": place,
        "shipping": None,
        "posted": (it.get("created_time") or "")[:10],
    }


def scan_olx(queries, pages, offers, stats, allow_all):
    stats["OLX"] = {"ok": 0, "errors": 0}
    for q in queries:
        got = 0
        try:
            for page in range(pages):
                url = (f"{OLX_API}?offset={page * OLX_LIMIT}&limit={OLX_LIMIT}"
                       f"&query={urllib.parse.quote(q)}")
                status, text = http_get(url, headers=OLX_HEADERS, timeout=25)
                if status != 200:
                    raise RuntimeError(f"HTTP {status}")
                data = json.loads(text)
                total = (data.get("metadata") or {}).get("total_elements") or 0
                items = data.get("data") or []
                for it in items:
                    o = _olx_map(it)
                    model, conf = classify(o["title"], o["description"])
                    if not model:
                        continue
                    if over_budget(o["price"], o["currency"], allow_all) and not _is_part(o):
                        stats["OLX"]["over"] = stats["OLX"].get("over", 0) + 1
                        continue
                    o.update(model=model, confidence=conf, query=q)
                    offers.append(o)
                    got += 1
                if (page + 1) * OLX_LIMIT >= total or not items:
                    break
            stats["OLX"]["ok"] += 1
            print(f"  [{q[:38]:<38}] ✅ {got}")
        except Exception as e:                  # noqa: BLE001
            stats["OLX"]["errors"] += 1
            print(f"  [{q[:38]:<38}] ❌ {e}")
    return offers


# ---------------------------------------------------------------------------
# Kleinanzeigen — HTML (article[data-adid])
# ---------------------------------------------------------------------------

KA_HEADERS = {"Accept": "text/html,application/xhtml+xml",
              "Accept-Language": "de,en;q=0.7"}
KA_BLOCK = ("captcha-delivery", "access denied", "zu viele anfragen")


def _ka_card(article):
    adid = re.search(r'data-adid="(\d+)"', article)
    href = re.search(r'data-href="([^"]+)"', article)
    if not (adid and href):
        return None
    url = ("https://www.kleinanzeigen.de" + href.group(1)
           if href.group(1).startswith("/") else href.group(1))
    ld_title = ld_desc = ""
    m = re.search(r'<script type="application/ld\+json">(.*?)</script>', article, re.S)
    if m:
        try:
            ld = json.loads(m.group(1))
            ld_title = (ld.get("title") or "").strip()
            ld_desc = (ld.get("description") or "").strip()
        except Exception:                       # noqa: BLE001
            pass
    vis = re.sub(r"<script.*?</script>", " ", article, flags=re.S)
    clean = vis
    vis = re.sub(r"<[^>]+>", " ", vis)
    vis = html.unescape(re.sub(r"\s+", " ", vis)).strip()
    title = ld_title or vis[:120]
    price = None
    mpr = re.search(r'class="[^"]*text-title3[^"]*"[^>]*>\s*([^<]*€[^<]*)', clean)
    if mpr:
        price = to_int(mpr.group(1))
    if price is None:
        mp = re.search(r"([\d][\d.\s]*)\s*€", vis)
        if mp:
            price = to_int(mp.group(1))
    shipping = None
    low = vis.lower()
    if "nur abholung" in low:
        shipping = "Nur Abholung ❌"
    elif "versand" in low:
        shipping = "Versand ✅"
    loc = None
    ml = re.search(r"\b(\d{5})\s+([A-Za-zÄÖÜäöüß().\- ]{2,40}?)\s+\d{2}\.\d{2}\.\d{4}\b", vis)
    if ml:
        loc = f"{ml.group(1)} {ml.group(2).strip()}"
    return {
        "portal": "Kleinanzeigen",
        "external_id": adid.group(1),
        "title": title,
        "description": ld_desc,
        "price": price,
        "currency": "EUR",
        "url": url,
        "location": loc,
        "shipping": shipping,
        "posted": None,
    }


def scan_kleinanzeigen(queries, pages, offers, stats, allow_all):
    stats["Kleinanzeigen"] = {"ok": 0, "errors": 0}
    for q in queries:
        got = 0
        try:
            for page in range(1, pages + 1):
                seite = "" if page == 1 else f"/seite:{page}"
                url = f"https://www.kleinanzeigen.de/s-{q}{seite}/k0"
                status, text = http_get(url, headers=KA_HEADERS, timeout=25)
                if status != 200 or any(b in text.lower() for b in KA_BLOCK):
                    raise RuntimeError(f"HTTP {status} / blokada")
                for article in re.split(r"<article", text)[1:]:
                    o = _ka_card(article)
                    if not o:
                        continue
                    model, conf = classify(o["title"], o["description"])
                    if not model:
                        continue
                    if over_budget(o["price"], o["currency"], allow_all) and not _is_part(o):
                        stats["Kleinanzeigen"]["over"] = stats["Kleinanzeigen"].get("over", 0) + 1
                        continue
                    o.update(model=model, confidence=conf, query=q)
                    offers.append(o)
                    got += 1
                time.sleep(0.4)
            stats["Kleinanzeigen"]["ok"] += 1
            print(f"  [{q[:38]:<38}] ✅ {got}")
        except Exception as e:                  # noqa: BLE001
            stats["Kleinanzeigen"]["errors"] += 1
            print(f"  [{q[:38]:<38}] ❌ {e}")
    return offers


# ---------------------------------------------------------------------------
# Blocket — base64 React Query state
# ---------------------------------------------------------------------------

BLOCKET_HEADERS = {"Accept": "text/html", "Accept-Language": "sv,en;q=0.7"}


def _blocket_docs(text):
    m = re.search(r'<script[^>]*data-react-query-state[^>]*>(.*?)</script>', text, re.S)
    if not m:
        return None
    try:
        state = json.loads(base64.b64decode(m.group(1).strip() + "==="))
    except Exception:                           # noqa: BLE001
        return None
    for q in state.get("queries", []):
        key = q.get("queryKey") or []
        if key and isinstance(key[0], dict) and key[0].get("scope") == "search":
            return (q.get("state") or {}).get("data") or {}
    return None


def _blocket_map(doc):
    price = doc.get("price") or {}
    loc = doc.get("location")
    if isinstance(loc, dict):
        loc = loc.get("name") or loc.get("title") or ", ".join(
            str(v) for v in loc.values() if isinstance(v, str))
    ts = doc.get("timestamp")
    posted = None
    if ts:
        try:
            posted = datetime.fromtimestamp(int(ts) / 1000).strftime("%Y-%m-%d")
        except (TypeError, ValueError, OSError):
            posted = None
    return {
        "portal": "Blocket",
        "external_id": str(doc.get("id") or doc.get("ad_id") or ""),
        "title": (doc.get("heading") or "").strip(),
        "description": "",
        "price": price.get("amount"),
        "currency": price.get("currency_code") or "SEK",
        "url": doc.get("canonical_url"),
        "location": loc,
        "shipping": None,
        "posted": posted,
    }


def scan_blocket(queries, pages, offers, stats, allow_all):
    stats["Blocket"] = {"ok": 0, "errors": 0}
    for q in queries:
        got = 0
        try:
            url = f"https://www.blocket.se/annonser?q={urllib.parse.quote(q)}"
            status, text = http_get(url, headers=BLOCKET_HEADERS, timeout=25)
            if status != 200:
                raise RuntimeError(f"HTTP {status}")
            data = _blocket_docs(text) or {}
            for doc in data.get("docs") or []:
                o = _blocket_map(doc)
                if not o["url"]:
                    continue
                model, conf = classify(o["title"], o["description"])
                if not model:
                    continue
                if over_budget(o["price"], o["currency"], allow_all) and not _is_part(o):
                    stats["Blocket"]["over"] = stats["Blocket"].get("over", 0) + 1
                    continue
                o.update(model=model, confidence=conf, query=q)
                offers.append(o)
                got += 1
            stats["Blocket"]["ok"] += 1
            print(f"  [{q[:38]:<38}] ✅ {got}")
        except Exception as e:                  # noqa: BLE001
            stats["Blocket"]["errors"] += 1
            print(f"  [{q[:38]:<38}] ❌ {e}")
    return offers


# ---------------------------------------------------------------------------
# Allegro — Playwright (DataDome)
# ---------------------------------------------------------------------------

ALLEGRO_URLS = [
    ("Roboty koszące używane", "https://allegro.pl/listing?string=robot+kosz%C4%85cy&order=qd&stan=u%C5%BCywane"),
    ("Kosiarka automatyczna używana", "https://allegro.pl/listing?string=kosiarka+automatyczna&order=qd&stan=u%C5%BCywane"),
    ("Lux Tools kosiarka", "https://allegro.pl/listing?string=lux+tools+kosiarka+automatyczna&order=qd"),
    ("Scheppach kosiarka", "https://allegro.pl/listing?string=scheppach+kosiarka+automatyczna&order=qd"),
    ("Meec Tools kosiarka", "https://allegro.pl/listing?string=meec+tools+kosiarka+automatyczna&order=qd"),
    ("Robot koszący uszkodzony", "https://allegro.pl/listing?string=robot+kosz%C4%85cy+uszkodzony&order=qd"),
]


def _allegro_blocked(page):
    try:
        low = page.inner_text("body").lower()
    except Exception:                           # noqa: BLE001
        return True
    if not low or "captcha-delivery" in low or "datadome" in low \
            or "please enable js" in low:
        return True
    try:
        if page.query_selector('iframe[src*="captcha-delivery"], iframe[src*="datadome"], '
                               'iframe#datadome, iframe[title*="captcha"]'):
            return True
    except Exception:                           # noqa: BLE001
        pass
    return False


def _wait_allegro(page, timeout=90):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not _allegro_blocked(page):
            # akceptuj cookies jeśli modal
            for pat in ("Akceptuj", "Zaakceptuj wszystkie", "Zgadzam się", "Przejdź do serwisu"):
                try:
                    b = page.get_by_role("button", name=pat).first
                    if b.is_visible(timeout=400):
                        b.click()
                        page.wait_for_timeout(500)
                        break
                except Exception:               # noqa: BLE001
                    continue
            return True
        page.wait_for_timeout(2000)
    return False


def _allegro_price(text):
    t = text.replace("\u00a0", " ").replace("\u202f", " ")
    m = re.search(r"([\d][\d\s]*(?:[.,]\d{1,2})?)\s*zł", t, re.I)
    if not m:
        return None
    return to_int(m.group(1))


def _allegro_card(card):
    text = card.inner_text()
    lines = [ln.strip() for ln in text.split("\n") if ln.strip()]
    if not lines:
        return None
    link = None
    for a in card.query_selector_all("a[href]"):
        href = a.get_attribute("href") or ""
        if "/oferta/" in href or "/produkt/" in href:
            link = href if href.startswith("http") else "https://allegro.pl" + href
            break
    if not link:
        return None
    ext = re.search(r"(?:-|offerId=|,)(\d{6,})", link)
    return {
        "portal": "Allegro",
        "external_id": ext.group(1) if ext else link,
        "title": lines[0],
        "description": "",
        "price": _allegro_price(text),
        "currency": "PLN",
        "url": link.split("?")[0] if "/produkt/" not in link else link,
        "location": None,
        "shipping": None,
        "posted": None,
    }


def scan_allegro(offers, stats, allow_all, headless=False):
    stats["Allegro"] = {"ok": 0, "errors": 0}
    if sync_playwright is None:
        print("  ❌ Playwright niedostępny")
        return offers
    pw = sync_playwright().start()
    ctx = None
    try:
        ctx = pw.firefox.launch_persistent_context(
            user_data_dir=str(ALLEGRO_PROFILE), headless=headless,
            viewport={"width": 1360, "height": 900},
            locale="pl-PL", timezone_id="Europe/Warsaw")
        page = ctx.new_page()
        if COOKIE_FILE.exists():
            try:
                ctx.add_cookies(json.loads(COOKIE_FILE.read_text()))
            except Exception:                   # noqa: BLE001
                pass
        for label, url in ALLEGRO_URLS:
            got = 0
            try:
                for attempt in range(2):
                    try:
                        page.goto(url, wait_until="domcontentloaded", timeout=30000)
                        break
                    except Exception:               # noqa: BLE001 — NS_BINDING_ABORTED itp.
                        if attempt:
                            raise
                        page.wait_for_timeout(1000)
                if not _wait_allegro(page, timeout=20):
                    raise RuntimeError("DataDome — uruchom --login")
                for card in page.query_selector_all("article"):
                    o = _allegro_card(card)
                    if not o:
                        continue
                    model, conf = classify(o["title"], o["description"])
                    if not model:
                        continue
                    if over_budget(o["price"], o["currency"], allow_all) and not _is_part(o):
                        stats["Allegro"]["over"] = stats["Allegro"].get("over", 0) + 1
                        continue
                    o.update(model=model, confidence=conf, query=label)
                    offers.append(o)
                    got += 1
                stats["Allegro"]["ok"] += 1
                print(f"  [{label[:38]:<38}] ✅ {got}")
            except Exception as e:              # noqa: BLE001
                stats["Allegro"]["errors"] += 1
                print(f"  [{label[:38]:<38}] ❌ {e}")
        try:
            COOKIE_FILE.write_text(json.dumps(ctx.cookies()))
        except Exception:                       # noqa: BLE001
            pass
    finally:
        if ctx is not None:
            ctx.close()
        pw.stop()
    return offers


def login_allegro(timeout=1800):
    """Otwiera Allegro headful; czeka aż captcha zniknie, potem zapisuje sesję."""
    if sync_playwright is None:
        print("Playwright niedostępny — nie mogę otworzyć Allegro.")
        return 1
    pw = sync_playwright().start()
    ctx = pw.firefox.launch_persistent_context(
        user_data_dir=str(ALLEGRO_PROFILE), headless=False,
        viewport={"width": 1360, "height": 900},
        locale="pl-PL", timezone_id="Europe/Warsaw")
    try:
        page = ctx.new_page()
        page.goto("https://allegro.pl/", wait_until="domcontentloaded")
        print("Rozwiąż captchę DataDome w otwartym oknie Allegro…")
        print("(skrypt sam wykryje sukces i zapisze sesję)")
        if _wait_allegro(page, timeout=timeout):
            COOKIE_FILE.write_text(json.dumps(ctx.cookies()))
            print(f"✅ Sesja zapisana ({len(ctx.cookies())} ciasteczek): {COOKIE_FILE}")
            return 0
        print("⏱️  Timeout — captcha nierozwiązana, sesja niepewna.")
        return 1
    finally:
        ctx.close()
        pw.stop()


# ---------------------------------------------------------------------------
# Raport
# ---------------------------------------------------------------------------

def dedupe(offers):
    seen, out = set(), []
    for o in offers:
        key = o.get("url") or (o["portal"], o.get("external_id"))
        if key in seen:
            continue
        seen.add(key)
        out.append(o)
    return out


# Akcesoria/consumables i elektronika — nie są robotami
_CONSUMABLE_RE = re.compile(
    r"(?<!bez)przew[óo]d|kabel|begrenzungsdraht|ograniczaj|"
    r"n[óo][żz]\b|no[żz]e|klingen|klinge\b|messer\b|blade\b|haken|"
    r"z[łl][ąa]czk|erdn[äa]gel|bodenanker|"
    r"platine|mainboard|hauptplatine|ersatzteil|\bdisplay\b")
_SHELTER_RE = re.compile(r"^\s*(garage|h[äa]uschen|schutzgarage)\b")


def _is_part(o):
    t = (o.get("title") or "").lower()
    return bool(_CONSUMABLE_RE.search(t) or _SHELTER_RE.search(t))


def write_report(offers, stats, allow_all):
    total_ok = sum(s.get("ok", 0) for s in stats.values())
    total_err = sum(s.get("errors", 0) for s in stats.values())
    total_over = sum(s.get("over", 0) for s in stats.values())

    lines = [f"Wyszukiwanie SNK: {datetime.now():%Y-%m-%d %H:%M}",
             f"Pasujących ofert: {len(offers)}  "
             f"(zapytań OK: {total_ok}, błędów: {total_err}, pominięto droższych: {total_over})",
             ""]

    if not allow_all:
        lines.append(f"Limit ceny: " + ", ".join(f"{k} ≤ {v}" for k, v in MAX_PRICE.items())
                     + "  (--all-prices = bez limitu)")
        lines.append("")

    main = [o for o in offers if not _is_part(o)]
    parts = [o for o in offers if _is_part(o)]
    lines[1] = (f"Pasujących ofert SNK: {len(main)} mowerów"
                + (f" + {len(parts)} części/akcesoriów" if parts else "")
                + f"  (zapytań OK: {total_ok}, błędów: {total_err}, "
                  f"pominięto droższych: {total_over})")

    def render(items):
        out = []
        for portal in ["OLX", "Kleinanzeigen", "Blocket", "Allegro"]:
            pitems = [o for o in items if o["portal"] == portal]
            if not pitems:
                continue
            pitems.sort(key=lambda o: (o["price"] is None, o["price"] or 0))
            out.append(f"── {portal} ({len(pitems)}) ──")
            for o in pitems:
                flag = "✓" if o["confidence"] == "high" else "?"
                price = fmt_price(o["price"], o["currency"])
                meta = "  ".join(x for x in (o.get("location"), o.get("shipping"),
                                             o.get("posted")) if x)
                out.append(f"[{flag}] {o['model']}  —  {price}"
                           + (f"  |  {meta}" if meta else ""))
                out.append(f"    {o['title']}")
                out.append(f"    {o['url']}")
                if o.get("description"):
                    desc = re.sub(r"\s+", " ", o["description"]).strip()
                    out.append(f"    {desc[:240]}")
                out.append("")
            out.append("")
        return out

    lines += render(main)
    if parts:
        lines.append(f"── Części / akcesoria ({len(parts)}) — nie są robotami ──")
        for o in sorted(parts, key=lambda o: (o["price"] is None, o["price"] or 0)):
            lines.append(f"    {fmt_price(o['price'], o['currency'])}  {o['title']}")
            lines.append(f"    {o['url']}")
        lines.append("")

    lines.append("Legenda: ✓ = pewny model (kod), ? = prawdopodobny SNK (do weryfikacji)")
    report = OUTDIR / "podsumowanie.txt"
    report.write_text("\n".join(lines), encoding="utf-8")

    (OUTDIR / "oferty.json").write_text(
        json.dumps(offers, ensure_ascii=False, indent=1), encoding="utf-8")
    return report


# ---------------------------------------------------------------------------
# Zapytania per portal
# ---------------------------------------------------------------------------

OLX_QUERIES = [
    "lux tools a-rmr-300-24", "lux tools kosiarka automatyczna", "lux tools rmr 300",
    "scheppach brmr300", "scheppach btrm300", "scheppach rrma300",
    "brucke rm500", "brucke rm501", "brucke rm800",
    "adano rm5", "gomag go-mr300", "grouw city 300",
    "meec tools kosiarka automatyczna", "julan kosiarka automatyczna",
    "robot koszący uszkodzony", "robot koszący nie działa",
    "kosiarka automatyczna uszkodzona", "robot koszący pin blokada",
]

KLEINANZEIGEN_QUERIES = [
    "lux+tools+a-rmr-300-24", "lux+tools+maehroboter",
    "scheppach+brmr300", "scheppach+btrm300", "scheppach+rrma300",
    "brucke+rm500", "brucke+rm501", "brucke+rm800",
    "adano+rm5", "gomag+go-mr300", "grouw+city+300",
    "meec+tools+maehroboter", "julan+maehroboter",
    "maehroboter+defekt", "maehroboter+kaputt", "maehroboter+unbekannter+pin",
]

BLOCKET_QUERIES = [
    "scheppach brmr300", "scheppach btrm300", "scheppach rrma300",
    "brucke rm500", "brucke rm501", "brucke rm800",
    "adano rm5", "gomag go-mr300", "grouw city 300", "grouw robotgrasklippare",
    "meec tools robotgrasklippare", "julan robotgrasklippare",
    "lux tools robotgrasklippare", "sunseeker v1 robotgrasklippare",
]


def main():
    ap = argparse.ArgumentParser(description="Wyszukiwarka SNK (Lux Tools i klony)")
    ap.add_argument("--login", "--get-cookies", dest="login", action="store_true",
                    help="Otwórz Allegro i rozwiąż captchę ręcznie (zapisuje sesję)")
    ap.add_argument("--allegro-only", action="store_true", help="Tylko Allegro")
    ap.add_argument("--all-prices", action="store_true", help="Nie odrzucaj droższych ofert")
    ap.add_argument("--portals", default="olx,ka,blocket,allegro",
                    help="Lista portali: olx,ka,blocket,allegro")
    ap.add_argument("--pages", type=int, default=2, help="Stron na zapytanie (OLX/KA)")
    ap.add_argument("--headless", action="store_true", help="Allegro bez okna (mniej niezawodne)")
    args = ap.parse_args()

    if args.login:
        sys.exit(login_allegro())

    want = {p.strip().lower() for p in args.portals.split(",") if p.strip()}
    if args.allegro_only:
        want = {"allegro"}

    print(f"=== Wyszukiwanie SNK: {datetime.now():%Y-%m-%d %H:%M} ===")
    if _cffi is None:
        print("⚠️  curl_cffi niedostępny — używam urllib (może łapać 403/CloudFront)")

    offers, stats = [], {}
    if "olx" in want:
        print("\n── OLX ──")
        scan_olx(OLX_QUERIES, args.pages, offers, stats, args.all_prices)
    if "ka" in want or "kleinanzeigen" in want:
        print("\n── Kleinanzeigen ──")
        scan_kleinanzeigen(KLEINANZEIGEN_QUERIES, max(2, args.pages), offers, stats, args.all_prices)
    if "blocket" in want:
        print("\n── Blocket ──")
        scan_blocket(BLOCKET_QUERIES, 1, offers, stats, args.all_prices)
    if "allegro" in want:
        print("\n── Allegro ──")
        scan_allegro(offers, stats, args.all_prices, headless=args.headless)

    offers = dedupe(offers)
    report = write_report(offers, stats, args.all_prices)
    print(f"\n{'=' * 52}\nPasujących ofert SNK: {len(offers)}")
    print(f"Raport: {report}")


if __name__ == "__main__":
    main()
