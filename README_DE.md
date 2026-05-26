# ESP32 CYD Ticker

Ein Kurs-Ticker für das **ESP32-2432S028/R (Cheap Yellow Display / CYD)**.

English: [README.md](README.md)  

Der ESP32 zeigt Live-Preise für Kryptowährungen, ETFs und Aktien auf dem eingebauten Touchscreen – alle Kurse kommen von einem selbst gehosteten Backend.

![IMG_8574](https://github.com/MaxDeavy/ESP32-CYD-Ticker/blob/main/demo.jpg?raw=true)

---

## Features

- Live-Kurse in EUR für Krypto, ETFs und Aktien (via [yfinance](https://github.com/ranaroussi/yfinance))
- Kategorien: **Krypto · ETFs · Aktien** – automatische Seitenaufteilung (je 2 Karten pro CYD-Seite)
- Mehrere Statistikzeiträume: **Live · 14 Tage · 30 Tage · 1 Jahr** (Chart)
- Touch-Bedienung: links = nächste Seite, rechts = Zeitraum wechseln
- Web-UI unter der Backend-URL (mobil-optimiert, alle Kategorien als Reiter)
- **Einfache Konfiguration**: Assets nur in `server/config.json` eintragen – ESP und Web-UI passen sich automatisch an
- Self-hosted, keine externen API-Keys nötig
- Docker-Deployment in einer Zeile

---

## Schnellstart

### 1. Backend starten (Docker)

```bash
cd server
docker compose up -d --build
```

Das Backend läuft dann auf Port **4546**:

| Endpunkt | Beschreibung |
|---|---|
| `http://localhost:4546/` | Web-UI (mobil-optimiert) |
| `http://localhost:4546/api/quotes` | JSON-API für den ESP32 |
| `http://localhost:4546/api/config` | Aktuelles Layout (Seiten, Kategorien) |
| `http://localhost:4546/health` | Health-Check |

### 2. ESP32 flashen

1. Den `cyd/`-Ordner in der Arduino IDE öffnen (findet `cyd.ino` automatisch) und am Anfang WLAN-Zugangsdaten + Backend-URL eintragen (siehe [ESP32-Konfiguration](#esp32-konfiguration)).
2. Sketch mit **Arduino IDE** oder **PlatformIO** kompilieren und flashen (Serial: 115200).
3. Nach dem Start verbindet sich der ESP automatisch mit dem Backend und zeigt die Kurse an.

---

## Assets konfigurieren

**Einzige Datei, die du anpassen musst:** `server/config.json`

```json
{
  "display": {
    "assets_per_page": 2
  },
  "categories": {
    "crypto": "Krypto",
    "etf": "ETFs",
    "stock": "Aktien"
  },
  "category_order": ["crypto", "etf", "stock"],
  "assets": [
    { "id": "btc",   "name": "Bitcoin",    "category": "crypto", "yahoo": "BTC-EUR" },
    { "id": "aapl",  "name": "Apple",      "category": "stock",  "yahoo": "AAPL",  "convert_usd_to_eur": true },
    { "id": "csspx", "name": "S&P 500",    "category": "etf",    "yahoo": "CSSPX.MI", "yahoo_fallback": "CSPX.L" }
  ]
}
```

### Felder

| Feld | Pflicht | Beschreibung |
|---|---|---|
| `id` | ja | Eindeutiger interner Bezeichner (Kleinbuchstaben) |
| `name` | ja | Anzeigename auf dem Display und in der Web-UI |
| `category` | ja | `crypto`, `etf` oder `stock` |
| `yahoo` | ja | Yahoo Finance Ticker (z. B. `BTC-EUR`, `AAPL`, `CSSPX.MI`) |
| `convert_usd_to_eur` | nein | `true` für US-Aktien (USD → EUR Umrechnung via EUR/USD-Kurs) |
| `yahoo_fallback` | nein | Alternativer Ticker, falls der Primäre keine Daten liefert |


**Nach jeder Änderung an `server/config.json`:**

```bash
cd server
docker compose up -d --build
```

Der ESP übernimmt das neue Layout beim nächsten automatischen Kurs-Abruf – kein neues Flashen nötig.

### Yahoo Finance Ticker finden

- **Krypto:** `BTC-EUR`, `ETH-EUR`, `XRP-EUR` usw.
- **US-Aktien:** `AAPL`, `MSFT`, `NVDA`, `GOOGL`, `TSLA` (immer mit `"convert_usd_to_eur": true`)
- **ETFs (EUR-notiert):** z. B. `CSSPX.MI` (iShares S&P 500, Borsa Italiana), `IWDA.AS` (iShares MSCI World, Amsterdam)
- **Deutsche Aktien:** `SAP.DE`, `BMW.DE` usw.

Tipp: Auf [finance.yahoo.com](https://finance.yahoo.com) nach dem Wertpapier suchen – der Ticker steht in der URL.

---

## ESP32-Konfiguration

**WLAN + Backend** am Anfang von **`cyd/cyd.ino`**:

```cpp
#define WIFI_SSID        "MeinNetzwerk"
#define WIFI_PASSWORD    "MeinPasswort"
#define BACKEND_HOST     "deine-domain.de"   // ohne https://
#define BACKEND_PORT     443
```

Optional in **`cyd.ino`**:

```cpp
#define DISPLAY_CURRENCY 0   // 0 = EUR, 1 = USD
#define DISPLAY_LANGUAGE 0   // 0 = DE,  1 = EN
```

| Option | Werte | Wirkung |
|---|---|---|
| `DISPLAY_CURRENCY` | `0` / `1` | Preise in € oder $  -  0 = € 1 = $ |
| `DISPLAY_LANGUAGE` | `0` / `1` | UI-Texte, Kategorie-Titel -  0 = DE,  1 = EN |

> **WLAN:** Der ESP32 unterstützt ausschließlich **2,4-GHz-Netzwerke**. Er verbindet sich nicht mit reinen 5-GHz-Netzen oder gemischten Netzwerken, bei denen das 2,4-GHz-Band unter demselben Namen läuft. Falls dein Router beide Bänder unter einer gemeinsamen SSID ausstrahlt, trenne sie in den Router-Einstellungen oder richte ein dediziertes 2,4-GHz-Netz ein.

---

## Bedienung am Display

| Aktion | Funktion |
|---|---|
| Tippen **links** (< Bildschirmmitte) | Nächste Seite |
| Tippen **rechts** (> Bildschirmmitte) | Zeitraum wechseln (Live → 1 Jahr → 30 Tage → 14 Tage) |
| **BOOT-Taste** | Nächste Seite (ohne Daten neu zu laden) |
| **Auto** (alle 10 s) | Nächste Seite + Kurse neu laden |

---

## Reverse Proxy (öffentliche URL)

**`server/deploy.env`** (SERVER_NAME) → Nginx erzeugen:

```bash
cd server
cp deploy.env.example deploy.env   # SERVER_NAME eintragen
cd deploy && ./generate-nginx.sh
sudo cp nginx-ticker.conf /etc/nginx/sites-available/ticker.conf
sudo ln -sf /etc/nginx/sites-available/ticker.conf /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
```

Docker: `cd server && docker compose up -d` (Host-Port **4546**).

### Fehlerbehebung TLS

| Symptom | Ursache | Lösung |
|---|---|---|
| `TLS-Verbindung fehlgeschlagen` | Platzhalter-Domain in `cyd.ino` | `BACKEND_HOST` auf echte Domain setzen, neu flashen |
| `DNS FEHLER` | Falscher `BACKEND_HOST` oder kein Internet | Hostname prüfen (ohne `https://`) |
| TLS ok, aber `Kein Layout` | Backend/Nginx down | `curl https://DEINE-DOMAIN/health` |
| Nur lokal testen | Port 443 ohne Cloudflare | `BACKEND_PORT` **4546** + HTTP (nur LAN, Sketch anpassen) |

Serial Monitor **115200**: nach dem Fix erscheinen `DNS … -> …` und `HTTP Status: … (200)`.

### Debug

```bash
cd server
docker compose logs -f ticker-api
curl http://localhost:4546/api/debug
curl http://localhost:4546/health
```

`/api/debug` zeigt Fehler pro Asset, Yahoo-Ticker und Sparkline-Längen.  
ESP32 Serial Monitor (**115200 Baud**) gibt Verbindungs- und Lade-Logs aus.

---

## Backend ohne Docker (lokal)

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

## ESP32 flashen mit PlatformIO

```bash
cd cyd
pio run -t upload
pio device monitor
```

Die `platformio.ini` im `cyd/`-Ordner ist vorkonfiguriert. Bibliotheken werden automatisch heruntergeladen.

---

## Custom Font (€-Zeichen)

LVGL-Standardfonts enthalten kein €-Zeichen – deshalb gibt es `lv_font_price_28.c/h` (Montserrat 28 px, Preiszeile).

Der Ordner `cyd/fonts/` enthält die Quelldateien zum Neu-Generieren (Node.js erforderlich):

```powershell
cd cyd
npx --yes lv_font_conv \
  --font fonts/Montserrat-Medium.ttf \
  --size 28 --bpp 4 --format lvgl --no-compress \
  --symbols "0123456789. €*" \
  -o fonts/lv_font_price_28.c \
  --lv-font-name lv_font_price_28
```

Danach `lv_font_price_28.c` und `.h` aus `cyd/fonts/` in den `cyd/`-Ordner kopieren.

---

## Hardware

- **Board:** ESP32-2432S028R (Cheap Yellow Display)
- **Display:** 320 × 240 px, ILI9341
- **Touch:** XPT2046 (resistiv)
- **USB:** zum Flashen und zur Stromversorgung

Andere CYD-Varianten (z. B. mit kapazitivem Touch) erfordern ggf. Anpassungen in `cyd/User_Setup.h` und den Touch-Pin-Definitionen in `cyd/cyd.ino`.

---

## Bibliotheks-Versionen

Die Firmware ist mit folgenden Versionen getestet. Andere Versionen können funktionieren, sind aber nicht garantiert.

| Bibliothek | Version | Hinweis |
|---|---|---|
| [lvgl/lvgl](https://github.com/lvgl/lvgl) | **8.4.x** | LVGL 9.x hat Breaking Changes – nicht upgraden |
| [bodmer/TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | **2.5.43** | Pin-Konfiguration über `cyd/User_Setup.h` |
| [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson) | ^7.2.0 | |
| [paulstoffregen/XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) | ^1.4 | |

Diese Versionen sind auch in `cyd/platformio.ini` festgelegt. Bei der Arduino IDE diese Versionen exakt über den Bibliotheks-Manager installieren, um Kompatibilitätsprobleme zu vermeiden.

---

## Lizenz

GPL-3.0 – siehe [LICENSE](LICENSE).
