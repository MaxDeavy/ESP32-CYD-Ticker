/*
 * ESP32 CYD Ticker – Firmware
 *
 * Konfiguration (diese Datei, Abschnitt unten):
 *   WIFI_SSID / WIFI_PASSWORD   – WLAN-Zugangsdaten (nur 2,4 GHz!)
 *   BACKEND_HOST / BACKEND_PORT – URL des Backends
 *   AUTO_SWITCH_MS              – Seiten-Wechsel-Intervall
 *   DISPLAY_CURRENCY / DISPLAY_LANGUAGE – 0/1 (EUR/USD, DE/EN)
 *
 * Assets & Seiten: nur server/config.json anpassen – kein neues Flashen nötig.
 *
 * Weitere .h-Dateien im cyd/-Ordner (nicht in Arduino/libraries kopieren):
 *   lv_conf.h    – LVGL-Konfiguration
 *   User_Setup.h – TFT_eSPI Pin-Belegung für das CYD
 *
 * Arduino IDE: cyd/-Ordner öffnen → cyd.ino auswählen.
 * PlatformIO:  aus cyd/ → pio run -t upload
 */

// --- Konfiguration -----------------------------------------------------------
#define WIFI_SSID        "YOUR_WIFI_SSID"
#define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"
#define BACKEND_HOST     "your-backend.example.com"
#define BACKEND_PORT     443
#define AUTO_SWITCH_MS   10000UL

#define DISPLAY_CURRENCY 0   // 0 = EUR (€), 1 = USD ($)
#define DISPLAY_LANGUAGE 0   // 0 = Deutsch, 1 = Englisch
// -----------------------------------------------------------------------------

#define LV_CONF_INCLUDE_SIMPLE
#include "lv_conf.h"

#define USER_SETUP_LOADED
#include "User_Setup.h"

#ifndef LV_USE_GESTURE
#define LV_USE_GESTURE 1
#endif

#include <lvgl.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <XPT2046_Touchscreen.h>
#include <stdarg.h>
#include "lv_font_price_28.h"

static const bool SERIAL_DEBUG = true;
static const bool STATUS_LED_ENABLED = false;

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33
#define TOUCH_ROTATION 3
#define Z_PRESSURE_MIN 400

SPIClass touchscreenSPI(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
static lv_indev_drv_t indev_drv;

#define BOOT_BTN_PIN 0
#define STATUS_LED_RED_PIN 4
#define STATUS_LED_GREEN_PIN 16
#define STATUS_LED_BLUE_PIN 17

static const unsigned long BOOT_DEBOUNCE_MS = 40;
static bool bootBtnLast = true;
static unsigned long bootBtnLastChange = 0;

static const unsigned long TOUCH_DEBOUNCE_MS = 45;
static const int TOUCH_HEADER_Y_MAX = 28;

static const uint16_t screenWidth = 320;
static const uint16_t screenHeight = 240;
static const unsigned long switchInterval = AUTO_SWITCH_MS;
static const int SPARKLINE_LEN = 32;
static const int MAX_ASSETS = 32;
static const int MAX_PAGES = 16;
static const int SLOTS_PER_PAGE = 2;

static const uint32_t COLOR_BG = 0x000000;
static const uint32_t COLOR_CARD = 0x050608;
static const uint32_t COLOR_CARD_BORDER = 0x161b22;
static const uint32_t COLOR_CHART_BG = 0x030405;
static const uint32_t COLOR_TITLE = 0x484f58;
static const uint32_t COLOR_NAME = 0xc9d1d9;
static const uint32_t COLOR_SYMBOL = 0x6e7681;
static const uint32_t COLOR_UP = 0x2ea043;
static const uint32_t COLOR_DOWN = 0xda3633;
static const uint32_t COLOR_FLAT = 0x6e7681;
static const uint32_t COLOR_STALE = 0xb08800;

struct AssetConfig {
  char displayName[28];
  char backendId[16];
};

struct PageDef {
  char title[24];
  char ids[SLOTS_PER_PAGE][16];
  int idCount = 0;
};

static AssetConfig assets[MAX_ASSETS];
static int numAssets = 0;
static PageDef pages[MAX_PAGES];
static int numPages = 0;

enum StatPeriod { PERIOD_LIVE = 0, PERIOD_YEAR = 1, PERIOD_30 = 2, PERIOD_14 = 3, NUM_STAT_PERIODS = 4 };

static const char* PERIOD_JSON_KEYS[NUM_STAT_PERIODS] = {"live", "y1", "d30", "d14"};

static float eurUsdRate = 1.0f;

struct PeriodStats {
  float changePercent = 0;
  float lastValidChangePercent = 0;
  float sparkline[SPARKLINE_LEN];
  uint8_t sparklineCount = 0;
  bool valid = false;
};

struct MarketData {
  float currentPrice = 0;
  bool dataValid = false;
  float lastValidPrice = 0;
  bool hasBackupData = false;
  PeriodStats periods[NUM_STAT_PERIODS];
};

MarketData marketData[MAX_ASSETS];
int currentPage = 0;
int statPeriod = PERIOD_LIVE;
unsigned long lastSwitchTime = 0;
unsigned long lastLayoutRetry = 0;
static const unsigned long LAYOUT_RETRY_MS = 30000;
static int lastTouchX = 0;
static int lastTouchY = 0;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t* buf;
TFT_eSPI tft = TFT_eSPI();
lv_disp_t* disp;

lv_obj_t* screen_bg;
lv_obj_t* title_label;
lv_obj_t* header_line;
lv_obj_t* period_label;
lv_obj_t* row_card[2];
lv_obj_t* row_accent[2];
lv_obj_t* row_name[2];
lv_obj_t* row_price[2];
lv_obj_t* row_change[2];
lv_obj_t* row_chart[2];
lv_chart_series_t* row_series[2];

WiFiClientSecure client;

void debugLog(const char* msg);
void debugLogf(const char* fmt, ...);

static void initSecureClient() {
  client.setInsecure();
  client.setTimeout(25000);
}

static bool resolveBackendHost(IPAddress& outIp) {
  if (WiFi.hostByName(BACKEND_HOST, outIp)) {
    debugLogf("DNS %s -> %s", BACKEND_HOST, outIp.toString().c_str());
    return true;
  }
  debugLogf("DNS FEHLER: %s", BACKEND_HOST);
  return false;
}

static const char* tr(const char* de, const char* en) {
  return (DISPLAY_LANGUAGE == 1) ? en : de;
}

static const char* periodLabel(int period) {
  static const char* labelsDe[NUM_STAT_PERIODS] = {"Live", "1 Jahr", "30 Tage", "14 Tage"};
  static const char* labelsEn[NUM_STAT_PERIODS] = {"Live", "1 Year", "30 Days", "14 Days"};
  if (period < 0 || period >= NUM_STAT_PERIODS) return "—";
  return (DISPLAY_LANGUAGE == 1) ? labelsEn[period] : labelsDe[period];
}

static const char* localizeCategoryTitle(const char* category, const char* fallback) {
  if (!category || category[0] == '\0') return fallback;
  if (DISPLAY_LANGUAGE == 1) {
    if (strcmp(category, "crypto") == 0) return "Crypto";
    if (strcmp(category, "etf") == 0) return "ETFs";
    if (strcmp(category, "stock") == 0) return "Stocks";
  } else {
    if (strcmp(category, "crypto") == 0) return "Krypto";
    if (strcmp(category, "etf") == 0) return "ETFs";
    if (strcmp(category, "stock") == 0) return "Aktien";
  }
  return fallback;
}

static float priceForDisplay(float priceEur) {
  if (priceEur <= 0.0f) return 0.0f;
  if (DISPLAY_CURRENCY == 1 && eurUsdRate > 0.0f) {
    return priceEur * eurUsdRate;
  }
  return priceEur;
}

static const lv_font_t* priceFont() {
  return (DISPLAY_CURRENCY == 1) ? &lv_font_montserrat_28 : &lv_font_price_28;
}

void debugLog(const char* msg) {
  if (SERIAL_DEBUG) Serial.println(msg);
}

void debugLogf(const char* fmt, ...) {
  if (!SERIAL_DEBUG) return;
  char buf[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.println(buf);
}

void printMarketStatus() {
  Serial.println("--- Anzeige-Status ---");
  for (int i = 0; i < numAssets; i++) {
    const MarketData& d = marketData[i];
    const PeriodStats& ps = d.periods[statPeriod];
    Serial.printf("  [%d] %-10s %-12s valid=%d price=%.4f chg=%+.2f%% spark=%u\n",
                  i, assets[i].backendId, assets[i].displayName,
                  d.dataValid, d.currentPrice, ps.changePercent, ps.sparklineCount);
  }
  Serial.printf("Seiten: %d\n", numPages);
  Serial.println("----------------------");
}

String extractJSON(const String& response) {
  int jsonStart = response.indexOf('{');
  if (jsonStart >= 0) {
    int jsonEnd = response.lastIndexOf('}');
    if (jsonEnd > jsonStart) {
      return response.substring(jsonStart, jsonEnd + 1);
    }
  }
  return "";
}

int assetIndexFromBackendId(const char* id) {
  if (!id || id[0] == '\0') return -1;
  for (int i = 0; i < numAssets; i++) {
    if (strcmp(assets[i].backendId, id) == 0) return i;
  }
  return -1;
}

int ensureAssetRegistered(const char* id, const char* name) {
  int idx = assetIndexFromBackendId(id);
  if (idx >= 0) {
    if (name && name[0] != '\0') {
      strncpy(assets[idx].displayName, name, sizeof(assets[idx].displayName) - 1);
      assets[idx].displayName[sizeof(assets[idx].displayName) - 1] = '\0';
    }
    return idx;
  }
  if (numAssets >= MAX_ASSETS) return -1;
  idx = numAssets++;
  memset(&marketData[idx], 0, sizeof(MarketData));
  strncpy(assets[idx].backendId, id, sizeof(assets[idx].backendId) - 1);
  assets[idx].backendId[sizeof(assets[idx].backendId) - 1] = '\0';
  const char* label = (name && name[0] != '\0') ? name : id;
  strncpy(assets[idx].displayName, label, sizeof(assets[idx].displayName) - 1);
  assets[idx].displayName[sizeof(assets[idx].displayName) - 1] = '\0';
  return idx;
}

void applyPagesFromJson(JsonArray pagesArr) {
  if (pagesArr.isNull()) return;
  int n = 0;
  for (JsonObject p : pagesArr) {
    if (n >= MAX_PAGES) break;
    const char* category = p["category"] | "";
    const char* title = p["title"] | "—";
    const char* locTitle = localizeCategoryTitle(category, title);
    strncpy(pages[n].title, locTitle, sizeof(pages[n].title) - 1);
    pages[n].title[sizeof(pages[n].title) - 1] = '\0';
    pages[n].idCount = 0;
    JsonArray ids = p["ids"].as<JsonArray>();
    if (!ids.isNull()) {
      for (JsonVariant v : ids) {
        if (pages[n].idCount >= SLOTS_PER_PAGE) break;
        const char* id = v.as<const char*>();
        if (!id || id[0] == '\0') continue;
        strncpy(pages[n].ids[pages[n].idCount], id, 15);
        pages[n].ids[pages[n].idCount][15] = '\0';
        ensureAssetRegistered(id, id);
        pages[n].idCount++;
      }
    }
    n++;
  }
  if (n > 0) {
    numPages = n;
    if (currentPage >= numPages) currentPage = 0;
  }
}

void showWaitingLayout(const char* msg) {
  lv_label_set_text(title_label, tr("Ticker", "Ticker"));
  updatePeriodLabel();
  for (int row = 0; row < SLOTS_PER_PAGE; row++) {
    lv_obj_clear_flag(row_card[row], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(row_name[row], "—");
    lv_label_set_text(row_price[row], msg);
    lv_label_set_text(row_change[row], "");
    if (row_chart[row] != nullptr) lv_obj_add_flag(row_chart[row], LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(disp);
}

lv_color_t changeColor(float changePercent, bool stale) {
  if (stale) return lv_color_hex(COLOR_STALE);
  if (changePercent > 0) return lv_color_hex(COLOR_UP);
  if (changePercent < 0) return lv_color_hex(COLOR_DOWN);
  return lv_color_hex(COLOR_FLAT);
}

void formatPrice(float priceEur, char* buf, size_t len) {
  float price = priceForDisplay(priceEur);
  if (DISPLAY_CURRENCY == 1) {
    if (price >= 1000.0f) {
      snprintf(buf, len, "$%.0f", price);
    } else if (price >= 1.0f) {
      snprintf(buf, len, "$%.2f", price);
    } else {
      snprintf(buf, len, "$%.4f", price);
    }
  } else if (price >= 1000.0f) {
    snprintf(buf, len, "%.0f \xE2\x82\xAC", price);
  } else if (price >= 1.0f) {
    snprintf(buf, len, "%.2f \xE2\x82\xAC", price);
  } else {
    snprintf(buf, len, "%.4f \xE2\x82\xAC", price);
  }
}

void loadPeriodSparkline(PeriodStats& ps, JsonArray spark) {
  ps.valid = false;
  ps.sparklineCount = 0;
  if (spark.isNull() || spark.size() < 2) return;

  int n = min((int)spark.size(), SPARKLINE_LEN);
  for (int i = 0; i < n; i++) {
    ps.sparkline[i] = spark[i].as<float>();
  }
  ps.sparklineCount = n;
  ps.valid = true;
}

void updateSparklineChart(int row, int assetIndex, lv_color_t color) {
  if (row_chart[row] == nullptr || row_series[row] == nullptr) return;

  const PeriodStats& ps = marketData[assetIndex].periods[statPeriod];
  int n = ps.sparklineCount;
  if (!ps.valid || n < 2) {
    lv_obj_add_flag(row_chart[row], LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_clear_flag(row_chart[row], LV_OBJ_FLAG_HIDDEN);

  float minV = ps.sparkline[0];
  float maxV = ps.sparkline[0];
  for (int i = 1; i < n; i++) {
    float v = ps.sparkline[i];
    if (v < minV) minV = v;
    if (v > maxV) maxV = v;
  }
  float span = maxV - minV;
  if (span < 0.0001f) span = maxV * 0.001f + 0.01f;

  lv_chart_set_point_count(row_chart[row], n);
  for (int i = 0; i < n; i++) {
    int y = (int)(((ps.sparkline[i] - minV) / span) * 900.0f + 50.0f);
    lv_chart_set_value_by_id(row_chart[row], row_series[row], i, y);
  }

#if LV_VERSION_CHECK(8, 2, 0)
  lv_chart_set_series_color(row_chart[row], row_series[row], color);
#else
  lv_obj_set_style_line_color(row_chart[row], color, LV_PART_ITEMS);
#endif
  lv_obj_set_style_line_width(row_chart[row], 2, LV_PART_ITEMS);
  lv_obj_set_style_size(row_chart[row], 0, LV_PART_INDICATOR);
#if LV_VERSION_CHECK(8, 3, 0)
  lv_chart_refresh(row_chart[row]);
#endif
  lv_obj_invalidate(row_chart[row]);
}

static void styleSparklineChart(lv_obj_t* chart) {
  lv_obj_set_style_radius(chart, 0, 0);
  lv_obj_set_style_bg_color(chart, lv_color_hex(COLOR_CHART_BG), 0);
  lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(chart, lv_color_hex(COLOR_CARD_BORDER), 0);
  lv_obj_set_style_border_width(chart, 1, 0);
  lv_obj_set_style_border_opa(chart, LV_OPA_30, 0);
  lv_obj_set_style_pad_all(chart, 4, 0);
  lv_chart_set_div_line_count(chart, 0, 0);
}

bool readHttpResponse(String& outBody, int& outStatus) {
  outStatus = 0;
  outBody = "";

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  if (statusLine.startsWith("HTTP/1.")) {
    int sp = statusLine.indexOf(' ');
    if (sp > 0) outStatus = statusLine.substring(sp + 1).toInt();
  }
  debugLogf("HTTP Status: %s (%d)", statusLine.c_str(), outStatus);

  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
  }

  while (client.available()) {
    outBody += client.readString();
    lv_task_handler();
  }
  return outStatus == 200;
}

bool fetchFromBackend() {
  if (WiFi.status() != WL_CONNECTED) {
    debugLog("WLAN nicht verbunden");
    return false;
  }

  IPAddress backendIp;
  if (!resolveBackendHost(backendIp)) return false;

  initSecureClient();
  debugLogf(">>> TLS %s:%u /api/quotes", BACKEND_HOST, (unsigned)BACKEND_PORT);

  if (!client.connect(BACKEND_HOST, (uint16_t)BACKEND_PORT)) {
    debugLogf("FEHLER: TLS zu %s:%u", BACKEND_HOST, (unsigned)BACKEND_PORT);
    client.stop();
    return false;
  }

  client.print(String("GET /api/quotes HTTP/1.1\r\n") +
               "Host: " + BACKEND_HOST + "\r\n" +
               "User-Agent: ESP32-CYD-Ticker\r\n" +
               "Accept: application/json\r\n" +
               "Connection: close\r\n\r\n");

  unsigned long timeout = millis() + 20000;
  while (!client.available() && millis() < timeout) {
    delay(10);
    lv_task_handler();
  }
  if (!client.available()) {
    debugLog("FEHLER: Timeout (keine Antwort)");
    client.stop();
    return false;
  }

  int httpStatus = 0;
  String response;
  bool okHttp = readHttpResponse(response, httpStatus);
  client.stop();

  debugLogf("Antwort: %u Bytes", response.length());
  if (!okHttp) {
    debugLogf("FEHLER: HTTP %d", httpStatus);
    if (SERIAL_DEBUG && response.length() > 0 && response.length() < 400) {
      Serial.println(response);
    }
    return false;
  }

  String jsonData = extractJSON(response);
  if (jsonData.length() == 0) {
    debugLog("FEHLER: Kein JSON in Antwort");
    return false;
  }

  if (SERIAL_DEBUG && jsonData.length() < 500) {
    Serial.println(jsonData);
  }

  DynamicJsonDocument doc(65536);
  DeserializationError err = deserializeJson(doc, jsonData);
  if (err) {
    debugLogf("FEHLER: JSON parse: %s", err.c_str());
    return false;
  }

  float rate = doc["eur_usd"] | 0.0f;
  if (rate > 0.0f) eurUsdRate = rate;

  JsonArray pagesArr = doc["pages"].as<JsonArray>();
  if (!pagesArr.isNull()) {
    applyPagesFromJson(pagesArr);
    debugLogf("Layout: %d Seiten", numPages);
  }

  JsonArray assetsJson = doc["assets"].as<JsonArray>();
  if (assetsJson.isNull()) {
    debugLog("FEHLER: Feld 'assets' fehlt");
    return false;
  }

  debugLogf("JSON assets: %u Eintraege", assetsJson.size());

  for (int i = 0; i < numAssets; i++) {
    marketData[i].dataValid = false;
  }

  int ok = 0;
  for (JsonObject item : assetsJson) {
    const char* id = item["id"] | "";
    const char* name = item["name"] | id;
    int idx = ensureAssetRegistered(id, name);
    if (idx < 0) {
      debugLogf("WARN: Asset-Limit erreicht (%s)", id);
      continue;
    }

    float price = item["price_eur"] | 0.0f;
    const char* yahoo = item["yahoo"] | "?";

    if (price <= 0.0f) {
      debugLogf("WARN: %s Preis=0 (yahoo=%s)", id, yahoo);
      continue;
    }

    MarketData& data = marketData[idx];
    data.currentPrice = price;
    data.dataValid = true;
    data.lastValidPrice = price;
    data.hasBackupData = true;

    JsonObject periods = item["periods"].as<JsonObject>();
    if (!periods.isNull()) {
      for (int p = 0; p < NUM_STAT_PERIODS; p++) {
        JsonObject block = periods[PERIOD_JSON_KEYS[p]].as<JsonObject>();
        if (block.isNull()) continue;
        PeriodStats& ps = data.periods[p];
        ps.changePercent = block["change_pct"] | 0.0f;
        loadPeriodSparkline(ps, block["sparkline"].as<JsonArray>());
        if (ps.valid) ps.lastValidChangePercent = ps.changePercent;
      }
    } else {
      PeriodStats& ps = data.periods[PERIOD_14];
      ps.changePercent = item["change_pct"] | 0.0f;
      loadPeriodSparkline(ps, item["sparkline"].as<JsonArray>());
      if (ps.valid) ps.lastValidChangePercent = ps.changePercent;
    }

    const PeriodStats& active = data.periods[PERIOD_14];
    debugLogf("OK  %s %.2f EUR 14d:%+.2f%% spark=%u (%s)",
              id, price, active.changePercent, active.sparklineCount, yahoo);
    ok++;
  }

  for (int i = 0; i < numAssets; i++) {
    if (!marketData[i].dataValid) {
      debugLogf("FEHLT auf Display: %s (%s)", assets[i].backendId, assets[i].displayName);
    }
  }

  debugLogf("Backend: %d/%d Kurse geladen", ok, numAssets);
  printMarketStatus();
  return ok > 0;
}

void fetchAllMarketData() {
  if (WiFi.status() != WL_CONNECTED) {
    debugLog("WLAN nicht verbunden");
    return;
  }
  fetchFromBackend();
}

void updateRow(int row, int assetIndex) {
  if (assetIndex < 0 || assetIndex >= numAssets) {
    lv_label_set_text(row_name[row], "—");
    lv_label_set_text(row_price[row], "—");
    lv_label_set_text(row_change[row], "");
    if (row_chart[row] != nullptr) lv_obj_add_flag(row_chart[row], LV_OBJ_FLAG_HIDDEN);
    return;
  }

  const MarketData& data = marketData[assetIndex];
  const PeriodStats& ps = data.periods[statPeriod];
  lv_label_set_text(row_name[row], assets[assetIndex].displayName);

  bool stale = !data.dataValid && data.hasBackupData;
  float price = data.dataValid ? data.currentPrice : data.lastValidPrice;
  bool periodOk = data.dataValid && ps.valid;
  float change = periodOk ? ps.changePercent
                          : (ps.lastValidChangePercent != 0 ? ps.lastValidChangePercent : 0);
  lv_color_t color = changeColor(change, stale);

  if (data.dataValid || data.hasBackupData) {
    char priceStr[28];
    formatPrice(price, priceStr, sizeof(priceStr));
    if (stale) {
      char display[32];
      snprintf(display, sizeof(display), "%s*", priceStr);
      lv_label_set_text(row_price[row], display);
    } else {
      lv_label_set_text(row_price[row], priceStr);
    }

    char pctStr[20];
    snprintf(pctStr, sizeof(pctStr), "%+.2f%%%s", change, stale ? "*" : "");
    lv_label_set_text(row_change[row], pctStr);
  } else {
    lv_label_set_text(row_price[row], "—");
    lv_label_set_text(row_change[row], tr("keine Daten", "no data"));
    color = lv_color_hex(COLOR_DOWN);
  }

  lv_color_t priceColor = stale ? lv_color_hex(COLOR_STALE) : lv_color_hex(COLOR_NAME);
  lv_obj_set_style_text_color(row_price[row], priceColor, 0);
  lv_obj_set_style_text_color(row_change[row], color, 0);
  if (row_accent[row] != nullptr) {
    lv_obj_set_style_bg_color(row_accent[row], color, 0);
    lv_obj_set_style_bg_opa(row_accent[row], stale ? LV_OPA_50 : LV_OPA_COVER, 0);
  }
  updateSparklineChart(row, assetIndex, color);
}

void updatePeriodLabel() {
  if (period_label != nullptr) {
    lv_label_set_text(period_label, periodLabel(statPeriod));
  }
}

int assetIndexForPageSlot(int page, int slot) {
  if (page < 0 || page >= numPages || slot < 0 || slot >= SLOTS_PER_PAGE) return -1;
  if (slot >= pages[page].idCount) return -1;
  return assetIndexFromBackendId(pages[page].ids[slot]);
}

void updatePageDisplay() {
  if (numPages <= 0) return;
  if (currentPage >= numPages) currentPage = 0;
  lv_label_set_text(title_label, pages[currentPage].title);
  updatePeriodLabel();
  for (int row = 0; row < SLOTS_PER_PAGE; row++) {
    int idx = assetIndexForPageSlot(currentPage, row);
    if (idx >= 0) {
      lv_obj_clear_flag(row_card[row], LV_OBJ_FLAG_HIDDEN);
      updateRow(row, idx);
    } else {
      lv_obj_add_flag(row_card[row], LV_OBJ_FLAG_HIDDEN);
    }
  }
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(disp);
}

void cycleStatPeriod() {
  statPeriod = (statPeriod + 1) % NUM_STAT_PERIODS;
  updatePageDisplay();
  debugLogf("Statistik: %s", periodLabel(statPeriod));
}

void advancePage(bool fetchData, const char* source) {
  if (numPages <= 0) return;
  currentPage = (currentPage + 1) % numPages;
  lastSwitchTime = millis();
  if (fetchData && WiFi.status() == WL_CONNECTED) {
    fetchAllMarketData();
  }
  updatePageDisplay();
  debugLogf("Seite %d/%d: %s (%s)", currentPage + 1, numPages, pages[currentPage].title, source);
}

void handleBootButton() {
  bool pressed = (digitalRead(BOOT_BTN_PIN) == LOW);
  unsigned long now = millis();

  if (pressed != bootBtnLast) {
    bootBtnLastChange = now;
    bootBtnLast = pressed;
  }
  if ((now - bootBtnLastChange) < BOOT_DEBOUNCE_MS) return;

  static bool handled = false;
  if (pressed && !handled) {
    handled = true;
    // BOOT: nur Seite wechseln, keine neuen Daten laden
    advancePage(false, "BOOT");
  } else if (!pressed) {
    handled = false;
  }
}

bool readTouchPoint(int& outX, int& outY) {
  if (!touchscreen.tirqTouched() || !touchscreen.touched()) return false;
  TS_Point p = touchscreen.getPoint();
  if (p.z < Z_PRESSURE_MIN) return false;

  outX = map(p.x, 200, 3800, 0, screenWidth - 1);
  outY = map(p.y, 200, 4000, 0, screenHeight - 1);
  outX = constrain(outX, 0, (int)screenWidth - 1);
  outY = constrain(outY, 0, (int)screenHeight - 1);
  return true;
}

void handleTouchSwitch() {
  int tx = 0, ty = 0;
  bool downNow = readTouchPoint(tx, ty);

  static bool touchDownStable = false;
  static unsigned long touchChangeMs = 0;
  static bool touchHandled = false;

  if (downNow != touchDownStable) {
    touchChangeMs = millis();
    touchDownStable = downNow;
    if (downNow) {
      lastTouchX = tx;
      lastTouchY = ty;
    }
  }

  if ((millis() - touchChangeMs) < TOUCH_DEBOUNCE_MS) return;

  if (touchDownStable && !touchHandled) {
    touchHandled = true;
    if (lastTouchY <= TOUCH_HEADER_Y_MAX) return;

    if (lastTouchX < (int)screenWidth / 2) {
      advancePage(false, "TOUCH");
    } else {
      cycleStatPeriod();
    }
  } else if (!touchDownStable) {
    touchHandled = false;
  }
}

void flush_cb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp_drv);
}

void touchscreen_read(lv_indev_drv_t* indev_drv, lv_indev_data_t* data) {
  data->state = LV_INDEV_STATE_REL;
  if (!touchscreen.tirqTouched()) return;

  TS_Point p = touchscreen.getPoint();
  if (p.z < Z_PRESSURE_MIN) return;

  data->point.x = map(p.x, 200, 3800, 0, screenWidth - 1);
  data->point.y = map(p.y, 200, 4000, 0, screenHeight - 1);
  data->point.x = constrain(data->point.x, 0, screenWidth - 1);
  data->point.y = constrain(data->point.y, 0, screenHeight - 1);
  data->state = LV_INDEV_STATE_PR;
}

void setupLVGL() {
  lv_init();
  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(0x0000);

  uint32_t bufSize = screenWidth * 40;
#ifdef CONFIG_SPIRAM_SUPPORT
  buf = psramFound() ? (lv_color_t*)ps_malloc(bufSize * sizeof(lv_color_t))
                     : (lv_color_t*)malloc(bufSize * sizeof(lv_color_t));
#else
  buf = (lv_color_t*)malloc(bufSize * sizeof(lv_color_t));
#endif
  if (!buf) return;

  lv_disp_draw_buf_init(&draw_buf, buf, NULL, bufSize);
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  disp = lv_disp_drv_register(&disp_drv);
}

static void styleCard(lv_obj_t* obj) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(COLOR_CARD), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(COLOR_CARD_BORDER), 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_border_opa(obj, LV_OPA_50, 0);
  lv_obj_set_style_radius(obj, 12, 0);
  lv_obj_set_style_pad_all(obj, 10, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void styleAccentBar(lv_obj_t* obj) {
  lv_obj_set_size(obj, 3, 72);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(COLOR_FLAT), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

void createUI() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  screen_bg = lv_obj_create(scr);
  lv_obj_set_size(screen_bg, screenWidth, screenHeight);
  lv_obj_set_style_bg_color(screen_bg, lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(screen_bg, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(screen_bg, 0, 0);
  lv_obj_set_style_border_width(screen_bg, 0, 0);
  lv_obj_set_style_pad_all(screen_bg, 0, 0);
  lv_obj_clear_flag(screen_bg, LV_OBJ_FLAG_SCROLLABLE);

  title_label = lv_label_create(screen_bg);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_NAME), 0);
  lv_obj_set_style_text_letter_space(title_label, 1, 0);
  lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 10, 6);
  lv_label_set_text(title_label, tr("Krypto", "Crypto"));

  period_label = lv_label_create(screen_bg);
  lv_obj_set_style_text_font(period_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(period_label, lv_color_hex(COLOR_NAME), 0);
  lv_obj_set_style_text_letter_space(period_label, 1, 0);
  lv_obj_align(period_label, LV_ALIGN_TOP_RIGHT, -10, 6);
  lv_label_set_text(period_label, periodLabel(statPeriod));

  header_line = lv_obj_create(screen_bg);
  lv_obj_set_size(header_line, screenWidth - 24, 1);
  lv_obj_align(header_line, LV_ALIGN_TOP_MID, 0, 26);
  lv_obj_set_style_bg_color(header_line, lv_color_hex(COLOR_CARD_BORDER), 0);
  lv_obj_set_style_bg_opa(header_line, LV_OPA_60, 0);
  lv_obj_set_style_border_width(header_line, 0, 0);
  lv_obj_set_style_radius(header_line, 0, 0);
  lv_obj_clear_flag(header_line, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < 2; i++) {
    int y = 32 + i * 102;
    row_card[i] = lv_obj_create(screen_bg);
    lv_obj_set_size(row_card[i], screenWidth - 12, 98);
    lv_obj_align(row_card[i], LV_ALIGN_TOP_MID, 0, y);
    styleCard(row_card[i]);
    lv_obj_set_style_pad_left(row_card[i], 12, 0);
    lv_obj_set_style_pad_right(row_card[i], 8, 0);
    lv_obj_set_style_pad_top(row_card[i], 8, 0);
    lv_obj_set_style_pad_bottom(row_card[i], 8, 0);

    row_accent[i] = lv_obj_create(row_card[i]);
    styleAccentBar(row_accent[i]);
    lv_obj_align(row_accent[i], LV_ALIGN_LEFT_MID, 2, 0);

    row_name[i] = lv_label_create(row_card[i]);
    lv_obj_set_style_text_font(row_name[i], &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(row_name[i], lv_color_hex(COLOR_SYMBOL), 0);
    lv_obj_align(row_name[i], LV_ALIGN_TOP_LEFT, 8, 0);
    lv_label_set_text(row_name[i], "—");

    row_chart[i] = lv_chart_create(row_card[i]);
    lv_obj_set_size(row_chart[i], 112, 56);
    lv_obj_align(row_chart[i], LV_ALIGN_TOP_RIGHT, 0, 16);
    lv_chart_set_type(row_chart[i], LV_CHART_TYPE_LINE);
    lv_chart_set_range(row_chart[i], LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    lv_chart_set_point_count(row_chart[i], SPARKLINE_LEN);
    styleSparklineChart(row_chart[i]);
    row_series[i] = lv_chart_add_series(row_chart[i], lv_color_hex(COLOR_UP), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_add_flag(row_chart[i], LV_OBJ_FLAG_HIDDEN);

    row_price[i] = lv_label_create(row_card[i]);
    lv_obj_set_style_text_font(row_price[i], priceFont(), 0);
    lv_obj_set_style_text_color(row_price[i], lv_color_hex(COLOR_NAME), 0);
    lv_obj_set_width(row_price[i], 172);
    lv_label_set_long_mode(row_price[i], LV_LABEL_LONG_CLIP);
    lv_obj_align_to(row_price[i], row_name[i], LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_label_set_text(row_price[i], DISPLAY_CURRENCY == 1 ? "$0.00" : "0.00 \xE2\x82\xAC");

    row_change[i] = lv_label_create(row_card[i]);
    lv_obj_set_style_text_font(row_change[i], &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_opa(row_change[i], LV_OPA_90, 0);
    lv_obj_align_to(row_change[i], row_price[i], LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_label_set_text(row_change[i], "+0.00%");
  }
}

void updateStatusLED(bool wifiConnected) {
  if (!STATUS_LED_ENABLED) {
    digitalWrite(STATUS_LED_RED_PIN, HIGH);
    digitalWrite(STATUS_LED_GREEN_PIN, HIGH);
    digitalWrite(STATUS_LED_BLUE_PIN, HIGH);
    return;
  }
  if (wifiConnected) {
    digitalWrite(STATUS_LED_RED_PIN, HIGH);
    digitalWrite(STATUS_LED_GREEN_PIN, LOW);
    digitalWrite(STATUS_LED_BLUE_PIN, HIGH);
  } else {
    digitalWrite(STATUS_LED_RED_PIN, LOW);
    digitalWrite(STATUS_LED_GREEN_PIN, HIGH);
    digitalWrite(STATUS_LED_BLUE_PIN, HIGH);
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("[WiFi] Verbinde...");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 25000) {
    delay(300);
    lv_task_handler();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] OK, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] FEHLER");
  }

  initSecureClient();
  updateStatusLED(WiFi.status() == WL_CONNECTED);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== CYD Ticker (nur Backend) ===");
  Serial.printf("API: https://%s/api/quotes\n", BACKEND_HOST);
  Serial.printf("Touch links=Seite | rechts=Statistik | BOOT | Auto %lus\n",
                switchInterval / 1000);

  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_RED_PIN, OUTPUT);
  pinMode(STATUS_LED_GREEN_PIN, OUTPUT);
  pinMode(STATUS_LED_BLUE_PIN, OUTPUT);
  digitalWrite(STATUS_LED_RED_PIN, HIGH);
  digitalWrite(STATUS_LED_GREEN_PIN, HIGH);
  digitalWrite(STATUS_LED_BLUE_PIN, HIGH);

  setupLVGL();

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(TOUCH_ROTATION);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchscreen_read;
  lv_indev_drv_register(&indev_drv);

  createUI();
  showWaitingLayout(tr("Lade …", "Loading…"));
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    if (strstr(BACKEND_HOST, "example.com") != nullptr) {
      debugLog("WARN: BACKEND_HOST in cyd.ino ist noch ein Platzhalter!");
    }
    fetchAllMarketData();
  }
  if (numPages > 0) {
    updatePageDisplay();
  } else {
    showWaitingLayout(WiFi.status() == WL_CONNECTED
                          ? tr("Kein Layout", "No layout")
                          : tr("Kein WLAN", "No Wi-Fi"));
  }

  lastSwitchTime = millis();
  lastLayoutRetry = millis();
}

void loop() {
  lv_task_handler();
  lv_tick_inc(2);
  updateStatusLED(WiFi.status() == WL_CONNECTED);

  handleBootButton();
  handleTouchSwitch();

  if (numPages <= 0 && WiFi.status() == WL_CONNECTED && millis() - lastLayoutRetry >= LAYOUT_RETRY_MS) {
    lastLayoutRetry = millis();
    fetchAllMarketData();
    if (numPages > 0) updatePageDisplay();
  } else if (numPages > 0 && millis() - lastSwitchTime >= switchInterval) {
    advancePage(true, "AUTO");
  }

  delay(2);
}
