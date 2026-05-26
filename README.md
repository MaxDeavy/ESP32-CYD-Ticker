# ESP32 CYD Ticker

A price ticker for the **ESP32-2432S028/R (Cheap Yellow Display / CYD)**.

The ESP32 shows live prices for cryptocurrencies, ETFs, and stocks on the built-in touchscreen – all quotes come from a self-hosted backend.

Deutsch: [README_DE.md](README_DE.md)

![Demo](https://github.com/MaxDeavy/ESP32-CYD-Ticker/blob/main/demo.jpg?raw=true)

---

## Features

- Live prices in EUR for crypto, ETFs, and stocks (via [yfinance](https://github.com/ranaroussi/yfinance))
- Categories: **Crypto · ETFs · Stocks** – automatic page layout (2 cards per CYD page)
- Multiple time ranges: **Live · 14 days · 30 days · 1 year** (chart)
- Touch controls: tap left = next page, tap right = change time range
- Web UI at the backend URL (mobile-friendly, categories as tabs)
- **Simple setup**: edit assets in `server/config.json` only – ESP and web UI update automatically
- Self-hosted, no external API keys
- One-line Docker deployment

---

## Quick start

### 1. Start the backend (Docker)

```bash
cd server
docker compose up -d --build
```

The backend runs on port **4546**:

| Endpoint | Description |
|---|---|
| `http://localhost:4546/` | Web UI (mobile-friendly) |
| `http://localhost:4546/api/quotes` | JSON API for the ESP32 |
| `http://localhost:4546/api/config` | Current layout (pages, categories) |
| `http://localhost:4546/health` | Health check |

### 2. Flash the ESP32

1. Open the `cyd/` folder in Arduino IDE (it picks up `cyd.ino` automatically) and enter your Wi-Fi credentials and backend URL at the top (see [ESP32 configuration](#esp32-configuration)).
2. Build and flash with **Arduino IDE** or **PlatformIO** (serial monitor: 115200).
3. On boot, the ESP connects to the backend and displays prices.

---

## Configuring assets

**The only file you need to edit:** `server/config.json`

```json
{
  "display": {
    "assets_per_page": 2
  },
  "categories": {
    "crypto": "Crypto",
    "etf": "ETFs",
    "stock": "Stocks"
  },
  "category_order": ["crypto", "etf", "stock"],
  "assets": [
    { "id": "btc",   "name": "Bitcoin",  "category": "crypto", "yahoo": "BTC-EUR" },
    { "id": "aapl",  "name": "Apple",    "category": "stock",  "yahoo": "AAPL", "convert_usd_to_eur": true },
    { "id": "csspx", "name": "S&P 500",  "category": "etf",    "yahoo": "CSSPX.MI", "yahoo_fallback": "CSPX.L" }
  ]
}
```

### Fields

| Field | Required | Description |
|---|---|---|
| `id` | yes | Unique internal id (lowercase) |
| `name` | yes | Display name on the CYD and in the web UI |
| `category` | yes | `crypto`, `etf`, or `stock` |
| `yahoo` | yes | Yahoo Finance ticker (e.g. `BTC-EUR`, `AAPL`, `CSSPX.MI`) |
| `convert_usd_to_eur` | no | `true` for US stocks (USD → EUR via live EUR/USD rate) |
| `yahoo_fallback` | no | Alternative ticker if the primary returns no data |

**After every change to `server/config.json`:**

```bash
cd server
docker compose up -d --build
```

The ESP picks up the new layout on the next automatic quote refresh – no re-flash needed.

### Finding Yahoo Finance tickers

- **Crypto:** `BTC-EUR`, `ETH-EUR`, `XRP-EUR`, etc.
- **US stocks:** `AAPL`, `MSFT`, `NVDA`, `GOOGL`, `TSLA` (always add `"convert_usd_to_eur": true`)
- **EUR-denominated ETFs:** e.g. `CSSPX.MI` (iShares S&P 500, Borsa Italiana), `IWDA.AS` (iShares MSCI World, Amsterdam)
- **European stocks:** `SAP.DE`, `BMW.DE`, etc.

Tip: search on [finance.yahoo.com](https://finance.yahoo.com) – the ticker is in the URL.

---

## ESP32 configuration

**Wi-Fi + backend** at the top of **`cyd/cyd.ino`**:

```cpp
#define WIFI_SSID        "MyNetwork"
#define WIFI_PASSWORD    "MyPassword"
#define BACKEND_HOST     "your-domain.example"   // OR IP,   without https://
#define BACKEND_PORT     443
```

Optional in **`cyd.ino`**:

```cpp
#define DISPLAY_CURRENCY 0   // 0 = EUR, 1 = USD
#define DISPLAY_LANGUAGE 0   // 0 = DE,  1 = EN
```

| Option | Values | Effect |
|---|---|---|
| `DISPLAY_CURRENCY` | `0` / `1` | Prices in € or $ – 0 = €, 1 = $ |
| `DISPLAY_LANGUAGE` | `0` / `1` | UI strings, category titles – 0 = DE, 1 = EN |

> **Wi-Fi:** The ESP32 only supports **2.4 GHz** networks. It will not connect to 5 GHz-only networks, or to combined dual-band networks that hide 2.4 GHz under the same SSID. If your router broadcasts both bands under one name, split them in the router settings or create a dedicated 2.4 GHz network.

---

## Display controls

| Action | Function |
|---|---|
| Tap **left** (< screen centre) | Next page |
| Tap **right** (> screen centre) | Change time range (Live → 1 year → 30 days → 14 days) |
| **BOOT button** | Next page (without reloading data) |
| **Auto** (every 10 s) | Next page + refresh quotes |

---

## Reverse proxy (public URL)

**`server/deploy.env`** (`SERVER_NAME`) → generate Nginx config:

```bash
cd server
cp deploy.env.example deploy.env   # set SERVER_NAME
cd deploy && ./generate-nginx.sh
sudo cp nginx-ticker.conf /etc/nginx/sites-available/ticker.conf
sudo ln -sf /etc/nginx/sites-available/ticker.conf /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
```

On Windows, use `.\generate-nginx.ps1` instead of `./generate-nginx.sh`.

Docker: `cd server && docker compose up -d` (host port **4546**).

### TLS troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `TLS connection failed` | Placeholder domain in `cyd.ino` | Set `BACKEND_HOST` to your real domain, re-flash |
| `DNS ERROR` | Wrong `BACKEND_HOST` or no internet | Check hostname (no `https://`) |
| TLS OK but `No layout` | Backend/Nginx down | `curl https://YOUR-DOMAIN/health` |
| Local testing only | Port 443 without Cloudflare | `BACKEND_PORT` **4546** + HTTP (LAN only, adjust sketch) |

Serial monitor **115200**: after a successful fix you should see `DNS … -> …` and `HTTP Status: … (200)`.

### Debug

```bash
cd server
docker compose logs -f ticker-api
curl http://localhost:4546/api/debug
curl http://localhost:4546/health
```

`/api/debug` shows per-asset errors, Yahoo tickers, and sparkline lengths.  
ESP32 serial monitor (**115200 baud**) prints connection and load logs.

---

## Backend without Docker (local)

```bash
cd server
python -m venv venv
# Windows:
venv\Scripts\activate
# Linux/macOS:
source venv/bin/activate

pip install -r requirements.txt
uvicorn server:app --host 0.0.0.0 --port 8080 --reload
```

---

## Flashing with PlatformIO

```bash
cd cyd
pio run -t upload
pio device monitor
```

The `platformio.ini` in `cyd/` is preconfigured. Libraries are downloaded automatically.

---

## Custom font (€ symbol)

LVGL default fonts do not include **€** – the repo ships `lv_font_price_28.c/h` (Montserrat 28 px for the price row).

The `cyd/fonts/` folder has sources to regenerate the font (Node.js required):

```powershell
cd cyd
npx --yes lv_font_conv \
  --font fonts/Montserrat-Medium.ttf \
  --size 28 --bpp 4 --format lvgl --no-compress \
  --symbols "0123456789. €*" \
  -o fonts/lv_font_price_28.c \
  --lv-font-name lv_font_price_28
```

Then copy `lv_font_price_28.c` and `.h` from `cyd/fonts/` into `cyd/`.

---

## Hardware

- **Board:** ESP32-2432S028R (Cheap Yellow Display)
- **Display:** 320 × 240 px, ILI9341
- **Touch:** XPT2046 (resistive)
- **USB:** flash and power

Other CYD variants (e.g. capacitive touch) may need changes in `cyd/User_Setup.h` and the touch pin definitions in `cyd/cyd.ino`.

---

## Library versions

Firmware tested with these versions. Others may work but are not guaranteed.

| Library | Version | Notes |
|---|---|---|
| [lvgl/lvgl](https://github.com/lvgl/lvgl) | **8.4.x** | LVGL 9.x has breaking changes – do not upgrade |
| [bodmer/TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | **2.5.43** | Pin config via `cyd/User_Setup.h` |
| [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson) | ^7.2.0 | |
| [paulstoffregen/XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) | ^1.4 | |

These are also pinned in `cyd/platformio.ini`. In Arduino IDE, install exactly these versions via the Library Manager to avoid compatibility issues.

---

## License

GPL-3.0 – see [LICENSE](LICENSE).
