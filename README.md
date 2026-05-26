
A live price ticker for the **ESP32-2432S028R (Cheap Yellow Display / CYD)**.  
The ESP32 displays real-time prices for cryptocurrencies, ETFs, and stocks on its built-in touchscreen. All prices are fetched from a self-hosted backend – no external API keys required.

![IMG_8574](https://github.com/MaxDeavy/ESP32-CYD-Ticker/blob/main/demo.jpg?raw=true)

---

## Features

- Live prices in EUR for crypto, ETFs, and stocks (powered by [yfinance](https://github.com/ranaroussi/yfinance))
- Three categories: **Crypto · ETFs · Stocks** – pages are created automatically (2 cards per CYD page)
- Multiple time ranges: **Live · 14 days · 30 days · 1 year** with sparkline charts
- Touch controls: tap left = next page, tap right = cycle time range
- Mobile-optimised web UI served by the backend
- **Simple configuration**: edit `server/config.json` to add or remove assets – the ESP and web UI update automatically, no re-flashing needed
- Fully self-hosted, no third-party API keys
- One-line Docker deployment

---

## Project Structure

```
esp32-cyd-ticker/
├── server/                        # Docker backend
│   ├── config.json                # ← Edit this to manage assets
│   ├── server.py                  # FastAPI backend (quotes + API)
│   ├── config_loader.py           # Loads config.json, builds page layout
│   ├── requirements.txt           # Python dependencies
│   ├── Dockerfile
│   ├── .dockerignore
│   ├── docker-compose.yml         # Backend deployment
│   ├── deploy.env.example         # SERVER_NAME for Nginx (copy → deploy.env)
│   ├── static/
│   │   └── index.html             # Mobile web UI
│   └── deploy/
│       ├── nginx-ticker.conf.template
│       ├── generate-nginx.sh / .ps1
│       └── nginx-ticker.conf      # generated (gitignored)
└── cyd/                           # ESP32 firmware (Arduino IDE / PlatformIO)
    ├── cyd.ino                    # Main sketch + configuration (Wi-Fi, backend URL)
    ├── lv_conf.h                  # LVGL configuration
    ├── User_Setup.h               # TFT_eSPI pin mapping for the CYD
    ├── lv_font_price_28.c/h       # Custom font (Montserrat 28 px with € symbol)
    ├── platformio.ini             # PlatformIO build config (optional)
    └── fonts/                     # Source files for regenerating the custom font
```

---

## Quick Start

### 1. Start the Backend (Docker)

```bash
cd server
docker compose up -d --build
```

The backend runs on port **4546**:

| Endpoint | Description |
|---|---|
| `http://localhost:4546/` | Web UI (mobile-optimised) |
| `http://localhost:4546/api/quotes` | JSON API for the ESP32 |
| `http://localhost:4546/api/config` | Current layout (pages, categories) |
| `http://localhost:4546/health` | Health check |

### 2. Flash the ESP32

1. Open the `cyd/` folder in Arduino IDE (it will find `cyd.ino` automatically) and fill in your Wi-Fi credentials and backend URL at the top (see [ESP32 Configuration](#esp32-configuration)).
2. Compile and flash using **Arduino IDE** or **PlatformIO** (serial monitor: 115200 baud).
3. The ESP32 connects to the backend on boot and starts displaying prices.

---

## Managing Assets

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
    { "id": "aapl",  "name": "Apple",    "category": "stock",  "yahoo": "AAPL",      "convert_usd_to_eur": true },
    { "id": "csspx", "name": "S&P 500",  "category": "etf",    "yahoo": "CSSPX.MI",  "yahoo_fallback": "CSPX.L" }
  ]
}
```

### Asset fields

| Field | Required | Description |
|---|---|---|
| `id` | yes | Unique internal identifier (lowercase) |
| `name` | yes | Display name shown on the CYD and in the web UI |
| `category` | yes | `crypto`, `etf`, or `stock` |
| `yahoo` | yes | Yahoo Finance ticker (e.g. `BTC-EUR`, `AAPL`, `CSSPX.MI`) |
| `convert_usd_to_eur` | no | Set to `true` for USD-denominated assets (uses live EUR/USD rate) |
| `yahoo_fallback` | no | Alternative ticker if the primary one has no data |

### Automatic page splitting

`assets_per_page: 2` → pages are created per category automatically:

| Assets in category | CYD pages |
|---|---|
| 2 | 1 page |
| 4 | 2 pages |
| 5 | 3 pages (last page has 1 card) |

**After every change to `server/config.json`:**

```bash
cd server
docker compose up -d --build
```

The ESP picks up the new layout on its next automatic data refresh – no re-flashing required.

### Finding Yahoo Finance tickers

- **Crypto:** `BTC-EUR`, `ETH-EUR`, `XRP-EUR`, etc.
- **US stocks:** `AAPL`, `MSFT`, `NVDA`, `GOOGL`, `TSLA` (always add `"convert_usd_to_eur": true`)
- **EUR-denominated ETFs:** e.g. `CSSPX.MI` (iShares S&P 500, Borsa Italiana), `IWDA.AS` (iShares MSCI World, Amsterdam)
- **European stocks:** `SAP.DE`, `BMW.DE`, etc.

Tip: search on [finance.yahoo.com](https://finance.yahoo.com) – the ticker is shown in the URL.

---

## ESP32 Configuration

At the top of **`cyd/cyd.ino`** there is a configuration block:

```cpp
#define WIFI_SSID        "YourNetwork"
#define WIFI_PASSWORD    "YourPassword"
#define BACKEND_HOST     "your-backend.example.com"  // or IP: "192.168.1.100"
#define BACKEND_PORT     443                   // 80 for HTTP, 443 for HTTPS
#define AUTO_SWITCH_MS   10000UL

#define DISPLAY_CURRENCY 0   // 0 = EUR, 1 = USD
#define DISPLAY_LANGUAGE 0   // 0 = DE,  1 = EN
```

| Option | Values | Effect |
|---|---|---|
| `DISPLAY_CURRENCY` | `0` / `1` | Show prices in € or $ (USD converted using `eur_usd` from the API) |
| `DISPLAY_LANGUAGE` | `0` / `1` | UI strings, period labels, category titles (e.g. Krypto/Crypto) |

Only these values need to be changed. Everything else (assets, pages, categories) comes from the backend. Asset display names in `server/config.json` are independent of `DISPLAY_LANGUAGE`.

> **Note:** `cyd/cyd.ino` contains your Wi-Fi password. Replace the credentials with placeholders before pushing to a public repository.

> **Wi-Fi:** The ESP32 only supports **2.4 GHz** networks. It will not connect to 5 GHz-only or mixed-band networks where the 2.4 GHz SSID is not separately visible. If your router broadcasts both bands under the same name, either separate them in your router settings or create a dedicated 2.4 GHz network.

---

## Display Controls

| Action | Function |
|---|---|
| Tap **left** (< screen centre) | Next page |
| Tap **right** (> screen centre) | Cycle time range (Live → 1 Year → 30 Days → 14 Days) |
| **BOOT button** | Next page (without fetching new data) |
| **Auto** (every 10 s) | Next page + fetch fresh data |

---

## Reverse Proxy (Public URL)

Copy `server/deploy.env.example` → `server/deploy.env`, set `SERVER_NAME`, then generate Nginx config:

```bash
cd server
cp deploy.env.example deploy.env   # edit SERVER_NAME
cd deploy && ./generate-nginx.sh
sudo cp nginx-ticker.conf /etc/nginx/sites-available/ticker.conf
sudo ln -sf /etc/nginx/sites-available/ticker.conf /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
```

Docker: `cd server && docker compose up -d` (host port **4546** by default, override with `TICKER_HOST_PORT` in the shell or `.env`).

### Debugging

```bash
cd server
docker compose logs -f ticker-api
curl http://localhost:4546/api/debug
curl http://localhost:4546/health
```

`/api/debug` shows per-asset errors, Yahoo tickers, and sparkline lengths.  
The ESP32 serial monitor (**115200 baud**) prints connection and data-loading logs.

---

## Running the Backend Without Docker

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

The `platformio.ini` in the `cyd/` folder is pre-configured. Libraries are downloaded automatically.

---

## Custom Font (€ Symbol)

The default LVGL fonts do not include the € symbol, so `lv_font_price_28.c/h` provides a custom Montserrat 28 px font for the price row.

The `cyd/fonts/` folder contains the source files to regenerate it (Node.js required):

```bash
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
- **Power:** USB (flash + run)

Other CYD variants (e.g. with capacitive touch) may require adjustments in `cyd/User_Setup.h` and the touch pin definitions in `cyd/cyd.ino`.

---

## Library Versions

The firmware is tested with the following library versions. Other versions may work but are not guaranteed.

| Library | Version | Notes |
|---|---|---|
| [lvgl/lvgl](https://github.com/lvgl/lvgl) | **8.4.x** | LVGL 9.x has breaking API changes – do not upgrade |
| [bodmer/TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | **2.5.43** | Pin config via `cyd/User_Setup.h` |
| [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson) | ^7.2.0 | |
| [paulstoffregen/XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) | ^1.4 | |

These are also pinned in `cyd/platformio.ini`. When using Arduino IDE, install exactly these versions via the Library Manager to avoid compatibility issues.

---

## License

GPL-3.0 – see [LICENSE](LICENSE).
