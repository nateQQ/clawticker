"""Coin prices from the public CoinGecko API (no key needed)."""
import json
import urllib.error
import urllib.parse
import urllib.request

URL = "https://api.coingecko.com/api/v3/simple/price"


def parse_coin_list(text):
    """'bitcoin:BTC, ethereum:ETH' -> [('bitcoin', 'BTC'), ('ethereum', 'ETH')]."""
    out = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        cid, _, sym = part.partition(":")
        cid = cid.strip().lower()
        out.append((cid, (sym.strip() or cid).upper()[:6]))
    return out[:4]


def fetch(coins, vs="usd", timeout=15):
    """Return [{'s': 'BTC', 'p': 67000.5, 'c': 1.2}], or None on failure."""
    if not coins:
        return []
    query = urllib.parse.urlencode({
        "ids": ",".join(c for c, _ in coins),
        "vs_currencies": vs,
        "include_24hr_change": "true",
    })
    try:
        with urllib.request.urlopen(URL + "?" + query, timeout=timeout) as r:
            data = json.load(r)
    except (urllib.error.URLError, OSError, ValueError):
        return None
    rows = []
    for cid, sym in coins:
        entry = data.get(cid)
        if not isinstance(entry, dict) or vs not in entry:
            continue
        rows.append({"s": sym, "p": float(entry[vs]),
                     "c": round(float(entry.get(vs + "_24h_change") or 0), 2)})
    return rows or None
