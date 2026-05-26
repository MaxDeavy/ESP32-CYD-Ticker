"""
CYD Ticker Backend – Kurse via yfinance, JSON für ESP32.
Web: GET /  |  API: GET /api/quotes  |  Debug: GET /api/debug
"""

import logging
import os
from pathlib import Path
import threading
import time
from typing import Dict, List, Optional

import yfinance as yf
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse

from config_loader import AppConfig, AssetDef, config_for_api, load_config

SPARKLINE_MAX = int(os.getenv("SPARKLINE_MAX", "32"))
DEBUG = os.getenv("DEBUG", "1") == "1"

logging.basicConfig(
    level=logging.DEBUG if DEBUG else logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("ticker")

EUR_USD_TICKER = "EURUSD=X"

app_config: Optional[AppConfig] = None
quotes: dict = {"assets": [], "updated_at": 0, "eur_usd": 0.92}
last_errors: Dict[str, str] = {}
_lock = threading.Lock()


def reload_app_config() -> AppConfig:
    global app_config, last_errors
    app_config = load_config()
    last_errors = {a.asset_id: "" for a in app_config.assets}
    log.info(
        "Config: %d Assets, %d Seiten",
        len(app_config.assets),
        len(app_config.pages),
    )
    return app_config


def _assets() -> List[AssetDef]:
    if app_config is None:
        reload_app_config()
    return app_config.assets


def _fetch_eur_usd() -> float:
    for ticker in (EUR_USD_TICKER, "EURUSD=X"):
        try:
            hist = yf.Ticker(ticker).history(period="2d", interval="1d")
            if not hist.empty:
                return float(hist["Close"].iloc[-1])
        except Exception as exc:
            log.warning("EUR/USD %s: %s", ticker, exc)
    return 0.92


def _usd_to_eur(price: float, usd_convert: bool, eur_usd: float) -> float:
    if usd_convert and eur_usd > 0:
        return price / eur_usd
    return price


def _period_from_prices(prices: List[float]) -> Optional[dict]:
    if not prices or len(prices) < 2:
        return None
    base = prices[0]
    last = prices[-1]
    change_pct = ((last - base) / base * 100.0) if base else 0.0
    return {
        "sparkline": prices,
        "change_pct": round(change_pct, 2),
    }


def _daily_sparkline_eur(
    ticker: str, usd_convert: bool, eur_usd: float, days: int
) -> Optional[List[float]]:
    """Letzte `days` Handelstage (Tages-Schlusskurse) in EUR."""
    try:
        if days > 60:
            period = "2y"
        elif days > 20:
            period = "6mo"
        else:
            period = "2mo"
        hist = yf.Ticker(ticker).history(
            period=period, interval="1d", auto_adjust=True
        )
        if hist.empty or "Close" not in hist.columns:
            return None
        close = hist["Close"].dropna()
        if len(close) < 2:
            return None

        prices: List[float] = []
        for raw in close.tail(min(days, SPARKLINE_MAX)):
            price = _usd_to_eur(float(raw), usd_convert, eur_usd)
            if price <= 0:
                return None
            prices.append(round(price, 4))
        return prices if len(prices) >= 2 else None
    except Exception as exc:
        log.debug("daily_sparkline(%s, %dd): %s", ticker, days, exc)
        return None


def _yearly_sparkline_eur(
    ticker: str, usd_convert: bool, eur_usd: float
) -> Optional[List[float]]:
    """Rollierendes Jahr (Yahoo period=1y), gleichmäßig auf SPARKLINE_MAX Punkte."""
    try:
        hist = yf.Ticker(ticker).history(
            period="1y", interval="1d", auto_adjust=True
        )
        if hist.empty or "Close" not in hist.columns:
            return None
        close = hist["Close"].dropna()
        if len(close) < 2:
            return None

        n = len(close)
        if n <= SPARKLINE_MAX:
            indices = list(range(n))
        else:
            indices = [
                int(round(i * (n - 1) / (SPARKLINE_MAX - 1)))
                for i in range(SPARKLINE_MAX)
            ]

        prices: List[float] = []
        for i in indices:
            price = _usd_to_eur(float(close.iloc[i]), usd_convert, eur_usd)
            if price <= 0:
                return None
            prices.append(round(price, 4))
        return prices if len(prices) >= 2 else None
    except Exception as exc:
        log.debug("yearly_sparkline(%s): %s", ticker, exc)
        return None


def _intraday_sparkline_eur(
    ticker: str, usd_convert: bool, eur_usd: float
) -> Optional[List[float]]:
    """Stundenkurse der letzten Handelstage (aktueller Verlauf)."""
    try:
        hist = yf.Ticker(ticker).history(
            period="5d", interval="1h", auto_adjust=True
        )
        if hist.empty or "Close" not in hist.columns:
            return None
        close = hist["Close"].dropna()
        if len(close) < 2:
            return None

        prices: List[float] = []
        for raw in close.tail(SPARKLINE_MAX):
            price = _usd_to_eur(float(raw), usd_convert, eur_usd)
            if price <= 0:
                return None
            prices.append(round(price, 4))
        return prices if len(prices) >= 2 else None
    except Exception as exc:
        log.debug("intraday_sparkline(%s): %s", ticker, exc)
        return None


def _latest_price_eur(ticker: str, usd_convert: bool, eur_usd: float) -> Optional[float]:
    try:
        info = yf.Ticker(ticker).fast_info
        price = info.get("last_price") or info.get("regularMarketPrice")
        if price is None or float(price) <= 0:
            return None
        return round(_usd_to_eur(float(price), usd_convert, eur_usd), 4)
    except Exception as exc:
        log.debug("fast_info(%s): %s", ticker, exc)
        return None


def _quote_one(asset: AssetDef, eur_usd: float) -> Optional[dict]:
    tickers = [asset.yahoo]
    if asset.yahoo_fallback:
        tickers.append(asset.yahoo_fallback)

    last_err = "kein Kurs"
    for ticker in tickers:
        usd_convert = asset.convert_usd_to_eur or (
            ticker == asset.yahoo_fallback and ticker.endswith(".L")
        )

        p14 = _daily_sparkline_eur(ticker, usd_convert, eur_usd, 14)
        p30 = _daily_sparkline_eur(ticker, usd_convert, eur_usd, 30)
        py1 = _yearly_sparkline_eur(ticker, usd_convert, eur_usd)
        plive = _intraday_sparkline_eur(ticker, usd_convert, eur_usd)

        if p14 is None:
            last_err = f"{ticker}: keine Daten"
            continue

        block14 = _period_from_prices(p14)
        block30 = _period_from_prices(p30) if p30 else block14
        blocky1 = _period_from_prices(py1) if py1 else block30
        blocklive = _period_from_prices(plive) if plive else block14

        price = _latest_price_eur(ticker, usd_convert, eur_usd)
        if price is None:
            price = plive[-1] if plive else p14[-1]

        source = ticker
        if ticker != asset.yahoo:
            log.info("%s: Fallback %s OK (%.2f EUR)", asset.name, ticker, price)

        return {
            "id": asset.asset_id,
            "name": asset.name,
            "price_eur": price,
            "change_pct": block14["change_pct"],
            "sparkline": block14["sparkline"],
            "periods": {
                "d14": block14,
                "d30": block30,
                "y1": blocky1,
                "live": blocklive,
            },
            "yahoo": source,
            "ok": True,
        }

    last_errors[asset.asset_id] = last_err
    log.error("%s (%s): %s", asset.name, asset.yahoo, last_err)
    return None


def refresh_quotes() -> None:
    log.info("--- Kurs-Update start ---")
    eur_usd = _fetch_eur_usd()
    log.info("EUR/USD: %.4f", eur_usd)

    cfg = app_config
    if cfg is None:
        reload_app_config()
        cfg = app_config

    updated = []
    for asset in cfg.assets:
        row = _quote_one(asset, eur_usd)
        if row:
            updated.append(row)
            last_errors[asset.asset_id] = ""
            log.info(
                "OK  %-12s %-10s %8.2f EUR  14d:%+.2f%% 30d:%+.2f%% 1y:%+.2f%% live:%+.2f%%",
                asset.asset_id,
                row["yahoo"],
                row["price_eur"],
                row["periods"]["d14"]["change_pct"],
                row["periods"]["d30"]["change_pct"],
                row["periods"]["y1"]["change_pct"],
                row["periods"]["live"]["change_pct"],
            )

    with _lock:
        quotes.clear()
        quotes["updated_at"] = int(time.time())
        quotes["eur_usd"] = round(eur_usd, 6)
        quotes["assets"] = updated
        quotes["expected_ids"] = [a.asset_id for a in cfg.assets]
        quotes["period_keys"] = ["d14", "live", "d30", "y1"]
        quotes["pages"] = cfg.pages
        quotes["categories"] = cfg.categories
        quotes["category_order"] = cfg.category_order

    log.info("--- Fertig: %d/%d ---", len(updated), len(cfg.assets))
    if len(updated) < len(cfg.assets):
        missing = [a.asset_id for a in cfg.assets if a.asset_id not in {r["id"] for r in updated}]
        log.warning("Fehlend: %s", ", ".join(missing))


def _quote_worker() -> None:
    """Kurse laden ohne HTTP-Startup zu blockieren (verhindert 502 beim Start)."""
    try:
        reload_app_config()
        refresh_quotes()
    except Exception:
        log.exception("Erstes Kurs-Update fehlgeschlagen")
    while True:
        time.sleep(int(os.getenv("REFRESH_SECONDS", "60")))
        try:
            reload_app_config()
            refresh_quotes()
        except Exception:
            log.exception("Kurs-Update fehlgeschlagen")


STATIC_DIR = Path(__file__).resolve().parent / "static"

app = FastAPI(title="CYD Ticker Backend", version="1.3")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/index.html", include_in_schema=False)
def index_html() -> FileResponse:
    return index_page()


@app.get("/", include_in_schema=False)
def index_page() -> FileResponse:
    """Mobile price overview (web UI)."""
    path = STATIC_DIR / "index.html"
    if not path.is_file():
        raise HTTPException(status_code=404, detail="static/index.html fehlt")
    return FileResponse(
        path,
        media_type="text/html; charset=utf-8",
        headers={"Cache-Control": "no-cache"},
    )


@app.on_event("startup")
def startup() -> None:
    index = STATIC_DIR / "index.html"
    try:
        reload_app_config()
        refresh_s = os.getenv("REFRESH_SECONDS", "60")
    except Exception:
        log.exception("config.json konnte nicht geladen werden")
        refresh_s = "?"
    log.info(
        "Backend gestartet (refresh=%ss, debug=%s, frontend=%s)",
        refresh_s,
        DEBUG,
        "ok" if index.is_file() else "FEHLT",
    )
    threading.Thread(target=_quote_worker, name="quote-worker", daemon=True).start()


@app.get("/api/config")
def get_config() -> dict:
    cfg = app_config or reload_app_config()
    return config_for_api(cfg)


@app.get("/api/quotes")
def get_quotes() -> dict:
    with _lock:
        return dict(quotes)


@app.get("/api/debug")
def get_debug() -> dict:
    with _lock:
        return {
            "quotes": dict(quotes),
            "errors": dict(last_errors),
            "period_keys": quotes.get("period_keys", []),
            "sparkline_lengths": {
                a["id"]: {k: len(a.get("periods", {}).get(k, {}).get("sparkline", []))
                          for k in ("d14", "live", "d30", "y1")}
                for a in quotes.get("assets", [])
            },
            "pages": quotes.get("pages", []),
            "assets_config": [
                {
                    "id": a.asset_id,
                    "name": a.name,
                    "category": a.category,
                    "yahoo": a.yahoo,
                    "fallback": a.yahoo_fallback,
                }
                for a in _assets()
            ],
        }


@app.get("/health")
def health() -> dict:
    with _lock:
        assets = quotes.get("assets", [])
    return {
        "ok": len(assets) >= 4,
        "assets_ok": len(assets),
        "assets_total": len(_assets()),
        "pages_total": len((app_config or reload_app_config()).pages),
        "refresh_seconds": int(os.getenv("REFRESH_SECONDS", "60")),
        "updated_at": quotes.get("updated_at", 0),
    }


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=8080)
