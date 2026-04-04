// ──────────────────────────────────────────
// FLASHTICKER 7" v1.0
// Hardware: Elecrow CrowPanel 7.0 – ESP32-S3 / 800x480
// Changelog:
//   v1.0 – Port von CYD v1.7, RGB Display, LVGL, GT911 Touch
// ──────────────────────────────────────────
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <qrcode.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include "logo.h"

// ──────────────────────────────────────────
// Konfiguration
// ──────────────────────────────────────────
#define AP_NAME         "FLASHTICKER"
#define AP_PASSWORD     "flashticker"
#define NTP_SERVER      "pool.ntp.org"
#define NTP_OFFSET_SEC  7200        // UTC+2
#define REFRESH_MS      (5UL * 60UL * 1000UL)

// ──────────────────────────────────────────
// Display Pins – CrowPanel 7.0
// ──────────────────────────────────────────
#define SCR_W   800
#define SCR_H   480

#define TFT_DE    40
#define TFT_VSYNC 41
#define TFT_HSYNC 39
#define TFT_PCLK  42
#define TFT_R0    45
#define TFT_R1    48
#define TFT_R2    47
#define TFT_R3    21
#define TFT_R4    14
#define TFT_G0     5
#define TFT_G1     6
#define TFT_G2     7
#define TFT_G3    15
#define TFT_G4    16
#define TFT_G5     4
#define TFT_B0     8
#define TFT_B1     3
#define TFT_B2    46
#define TFT_B3     9
#define TFT_B4     1

// Touch GT911 – I2C
#define TOUCH_SDA  19
#define TOUCH_SCL  20
#define TOUCH_INT  38
#define GT911_ADDR 0x5D   // alternativ: 0x14

// ──────────────────────────────────────────
// Farben RGB565
// ──────────────────────────────────────────
#define COL_BG      lv_color_hex(0x080810)
#define COL_HEADER  lv_color_hex(0x0C0C1C)
#define COL_ORANGE  lv_color_hex(0xFB8000)
#define COL_WHITE   lv_color_hex(0xFFFFFF)
#define COL_GREY1   lv_color_hex(0xAAAAAA)
#define COL_GREY2   lv_color_hex(0x787878)
#define COL_LABEL   lv_color_hex(0x404040)
#define COL_UP      lv_color_hex(0x00DD00)
#define COL_DN      lv_color_hex(0xFF2222)
#define COL_DIV     lv_color_hex(0x181830)

// ──────────────────────────────────────────
// Modi: 0-2 = oz, 3-5 = Gramm
// ──────────────────────────────────────────
uint8_t currencyMode = 0;
const char* modeLabels[]     = {"USD/oz","EUR/oz","CHF/oz","USD/g","EUR/g","CHF/g"};
const char* currencySymbols[] = {"$","EUR ","CHF ","$","EUR ","CHF "};
#define TROY_OZ_TO_G 31.1035f

// ──────────────────────────────────────────
// Preisdaten
// ──────────────────────────────────────────
float btc_usd, btc_eur, btc_chf, btc_chg;
float gold_usd, gold_eur, gold_chf, gold_chg;
float silver_usd, silver_eur, silver_chf, silver_chg;
unsigned long lastRefresh = 0;
bool dataValid = false;

// ──────────────────────────────────────────
// Hardware Objekte
// ──────────────────────────────────────────
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
  TFT_R0, TFT_R1, TFT_R2, TFT_R3, TFT_R4,
  TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
  TFT_B0, TFT_B1, TFT_B2, TFT_B3, TFT_B4,
  1 /* hsync_polarity */, 8 /* hsync_front_porch */,
  4 /* hsync_pulse_width */, 43 /* hsync_back_porch */,
  1 /* vsync_polarity */, 8 /* vsync_front_porch */,
  4 /* vsync_pulse_width */, 12 /* vsync_back_porch */
);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  SCR_W, SCR_H, rgbpanel, 0 /* rotation */, true /* auto_flush */
);

WiFiUDP    ntpUDP;
NTPClient  timeClient(ntpUDP, NTP_SERVER, NTP_OFFSET_SEC, 60000);

// ──────────────────────────────────────────
// LVGL Buffer & Callbacks
// ──────────────────────────────────────────
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCR_W * 20];
static lv_color_t buf2[SCR_W * 20];

void lvgl_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
  lv_disp_flush_ready(disp);
}

// ──────────────────────────────────────────
// GT911 Touch – einfaches I2C Register-Read
// ──────────────────────────────────────────
struct TouchPoint { int16_t x, y; bool pressed; };

TouchPoint readTouch() {
  TouchPoint tp = {0, 0, false};
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81); Wire.write(0x4E);  // Status Register
  Wire.endTransmission(false);
  Wire.requestFrom(GT911_ADDR, (uint8_t)1);
  if (!Wire.available()) return tp;
  uint8_t status = Wire.read();

  // Status-Register löschen
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81); Wire.write(0x4E);
  Wire.write(0x00);
  Wire.endTransmission();

  uint8_t touchCount = status & 0x0F;
  if (touchCount == 0 || touchCount > 5) return tp;

  // Ersten Touch-Punkt lesen
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81); Wire.write(0x50);
  Wire.endTransmission(false);
  Wire.requestFrom(GT911_ADDR, (uint8_t)7);
  if (Wire.available() < 7) return tp;

  Wire.read(); // track id
  uint8_t xl = Wire.read(); uint8_t xh = Wire.read();
  uint8_t yl = Wire.read(); uint8_t yh = Wire.read();
  Wire.read(); Wire.read(); // size

  tp.x = (int16_t)(xl | ((xh & 0x0F) << 8));
  tp.y = (int16_t)(yl | ((yh & 0x0F) << 8));
  tp.pressed = true;
  return tp;
}

void lvgl_touch_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  TouchPoint tp = readTouch();
  if (tp.pressed) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = tp.x;
    data->point.y = tp.y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ──────────────────────────────────────────
// LVGL UI Objekte
// ──────────────────────────────────────────
lv_obj_t *lbl_title, *lbl_mode, *lbl_time, *lbl_date;
lv_obj_t *lbl_btc_price, *lbl_btc_s1, *lbl_btc_s2, *lbl_btc_chg;
lv_obj_t *lbl_gold_price, *lbl_gold_s1, *lbl_gold_s2, *lbl_gold_chg;
lv_obj_t *lbl_silver_price, *lbl_silver_s1, *lbl_silver_s2, *lbl_silver_chg;
lv_obj_t *lbl_countdown;
lv_obj_t *lbl_btc_name, *lbl_gold_name, *lbl_silver_name;

// ──────────────────────────────────────────
// Bootlogo via Arduino_GFX (vor LVGL Init)
// ──────────────────────────────────────────
void showBootLogo() {
  gfx->fillScreen(0x0841);
  int x = (SCR_W - LOGO_W) / 2;
  int y = (SCR_H - LOGO_H) / 2;
  gfx->draw16bitBeRGBBitmap(x, y, (uint16_t*)logo_data, LOGO_W, LOGO_H);
  delay(2500);
}

// ──────────────────────────────────────────
// HTTP GET
// ──────────────────────────────────────────
String httpGet(const char* url) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(10000);
  http.addHeader("User-Agent", "ESP32S3-Ticker/1.0");
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    http.end();
    return body;
  }
  http.end();
  return "";
}

// ──────────────────────────────────────────
// Kurse abrufen
// ──────────────────────────────────────────
bool fetchPrices() {
  String raw = httpGet(
    "https://api.coingecko.com/api/v3/simple/price"
    "?ids=bitcoin,pax-gold,kinesis-silver&vs_currencies=usd,eur,chf"
    "&include_24hr_change=true"
  );
  if (raw.isEmpty()) return false;

  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return false;

  btc_usd    = doc["bitcoin"]["usd"]                   | 0.0f;
  btc_eur    = doc["bitcoin"]["eur"]                   | 0.0f;
  btc_chf    = doc["bitcoin"]["chf"]                   | 0.0f;
  btc_chg    = doc["bitcoin"]["usd_24h_change"]        | 0.0f;
  gold_usd   = doc["pax-gold"]["usd"]                  | 0.0f;
  gold_eur   = doc["pax-gold"]["eur"]                  | 0.0f;
  gold_chf   = doc["pax-gold"]["chf"]                  | 0.0f;
  gold_chg   = doc["pax-gold"]["usd_24h_change"]       | 0.0f;
  silver_usd = doc["kinesis-silver"]["usd"]            | 0.0f;
  silver_eur = doc["kinesis-silver"]["eur"]            | 0.0f;
  silver_chf = doc["kinesis-silver"]["chf"]            | 0.0f;
  silver_chg = doc["kinesis-silver"]["usd_24h_change"] | 0.0f;

  return btc_usd > 100;
}

// ──────────────────────────────────────────
// Preis-Formatierung
// ──────────────────────────────────────────
String fmtPrice(float val, bool cents) {
  if (val == 0) return "--";
  if (cents) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", val);
    return String(buf);
  }
  long v = (long)round(val);
  if (v >= 1000) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld,%03ld", v / 1000, v % 1000);
    return String(buf);
  }
  return String(v);
}

String fmtChange(float chg) {
  char buf[12];
  snprintf(buf, sizeof(buf), chg >= 0 ? "+%.2f%%" : "%.2f%%", chg);
  return String(buf);
}

void getPrices(float usd, float eur, float chf, bool isBtc,
               float& main, float& s1, float& s2,
               const char*& s1label, const char*& s2label) {
  float u = (!isBtc && currencyMode >= 3) ? usd / TROY_OZ_TO_G : usd;
  float e = (!isBtc && currencyMode >= 3) ? eur / TROY_OZ_TO_G : eur;
  float c = (!isBtc && currencyMode >= 3) ? chf / TROY_OZ_TO_G : chf;
  switch (currencyMode % 3) {
    case 0: main=u; s1=e; s2=c; s1label="EUR"; s2label="CHF"; break;
    case 1: main=e; s1=u; s2=c; s1label="USD"; s2label="CHF"; break;
    case 2: main=c; s1=u; s2=e; s1label="USD"; s2label="EUR"; break;
  }
}

// ──────────────────────────────────────────
// UI aufbauen
// ──────────────────────────────────────────
void buildUI() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, COL_BG, 0);

  // ── Header ─────────────────────────────
  lv_obj_t *header = lv_obj_create(scr);
  lv_obj_set_size(header, SCR_W, 50);
  lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(header, COL_HEADER, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(header, COL_ORANGE, LV_PART_MAIN);
  lv_obj_set_style_border_width(header, 2, LV_PART_MAIN);

  lbl_title = lv_label_create(header);
  lv_label_set_text(lbl_title, "FLASHTICKER");
  lv_obj_set_style_text_color(lbl_title, COL_ORANGE, 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
  lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 12, 0);

  lbl_mode = lv_label_create(header);
  lv_label_set_text(lbl_mode, modeLabels[currencyMode]);
  lv_obj_set_style_text_color(lbl_mode, COL_GREY2, 0);
  lv_obj_set_style_text_font(lbl_mode, &lv_font_montserrat_16, 0);
  lv_obj_align_to(lbl_mode, lbl_title, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  lbl_time = lv_label_create(header);
  lv_label_set_text(lbl_time, "--:--");
  lv_obj_set_style_text_color(lbl_time, COL_WHITE, 0);
  lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_32, 0);
  lv_obj_align(lbl_time, LV_ALIGN_RIGHT_MID, -12, 0);

  lbl_date = lv_label_create(header);
  lv_label_set_text(lbl_date, "");
  lv_obj_set_style_text_color(lbl_date, COL_GREY2, 0);
  lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_20, 0);
  lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_LEFT_MID, -10, 0);

  // ── Rows ───────────────────────────────
  // Layout: 3 Zeilen, je 1/3 der restlichen Höhe
  // Spalten: Name | Hauptpreis | Stack (s1/s2) | Change%
  const char *names[] = {"BTC", "GOLD", "SILVER"};
  lv_obj_t **price_lbls[]  = {&lbl_btc_price,  &lbl_gold_price,  &lbl_silver_price};
  lv_obj_t **s1_lbls[]     = {&lbl_btc_s1,     &lbl_gold_s1,     &lbl_silver_s1};
  lv_obj_t **s2_lbls[]     = {&lbl_btc_s2,     &lbl_gold_s2,     &lbl_silver_s2};
  lv_obj_t **chg_lbls[]    = {&lbl_btc_chg,    &lbl_gold_chg,    &lbl_silver_chg};
  lv_obj_t **name_lbls[]   = {&lbl_btc_name,   &lbl_gold_name,   &lbl_silver_name};

  int rowH = (SCR_H - 50 - 24) / 3;  // 50=header, 24=footer

  for (int i = 0; i < 3; i++) {
    int rowY = 50 + i * rowH;

    // Trennlinie
    lv_obj_t *line = lv_obj_create(scr);
    lv_obj_set_size(line, SCR_W, 1);
    lv_obj_set_pos(line, 0, rowY + rowH - 1);
    lv_obj_set_style_bg_color(line, COL_DIV, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);

    // Asset-Name
    *name_lbls[i] = lv_label_create(scr);
    lv_label_set_text(*name_lbls[i], names[i]);
    lv_obj_set_style_text_color(*name_lbls[i], COL_LABEL, 0);
    lv_obj_set_style_text_font(*name_lbls[i], &lv_font_montserrat_24, 0);
    lv_obj_set_pos(*name_lbls[i], 16, rowY + rowH/2 - 14);

    // Hauptpreis
    *price_lbls[i] = lv_label_create(scr);
    lv_label_set_text(*price_lbls[i], "--");
    lv_obj_set_style_text_color(*price_lbls[i], COL_WHITE, 0);
    lv_obj_set_style_text_font(*price_lbls[i], &lv_font_montserrat_48, 0);
    lv_obj_set_pos(*price_lbls[i], 110, rowY + rowH/2 - 28);

    // Stack s1 (oben)
    *s1_lbls[i] = lv_label_create(scr);
    lv_label_set_text(*s1_lbls[i], "");
    lv_obj_set_style_text_color(*s1_lbls[i], COL_GREY1, 0);
    lv_obj_set_style_text_font(*s1_lbls[i], &lv_font_montserrat_20, 0);
    lv_obj_set_pos(*s1_lbls[i], 560, rowY + rowH/2 - 26);

    // Stack s2 (unten)
    *s2_lbls[i] = lv_label_create(scr);
    lv_label_set_text(*s2_lbls[i], "");
    lv_obj_set_style_text_color(*s2_lbls[i], COL_GREY2, 0);
    lv_obj_set_style_text_font(*s2_lbls[i], &lv_font_montserrat_20, 0);
    lv_obj_set_pos(*s2_lbls[i], 560, rowY + rowH/2 + 2);

    // Kursänderung %
    *chg_lbls[i] = lv_label_create(scr);
    lv_label_set_text(*chg_lbls[i], "");
    lv_obj_set_style_text_color(*chg_lbls[i], COL_GREY2, 0);
    lv_obj_set_style_text_font(*chg_lbls[i], &lv_font_montserrat_32, 0);
    lv_obj_align(*chg_lbls[i], LV_ALIGN_TOP_RIGHT, -16, rowY + rowH/2 - 18);
  }

  // ── Footer ─────────────────────────────
  lbl_countdown = lv_label_create(scr);
  lv_label_set_text(lbl_countdown, "");
  lv_obj_set_style_text_color(lbl_countdown, COL_DIV, 0);
  lv_obj_set_style_text_font(lbl_countdown, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_countdown, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
}

// ──────────────────────────────────────────
// UI aktualisieren
// ──────────────────────────────────────────
void updateRow(lv_obj_t *price_lbl, lv_obj_t *s1_lbl, lv_obj_t *s2_lbl,
               lv_obj_t *chg_lbl,
               float usd, float eur, float chf, float chg,
               bool cents, bool isBtc) {
  float main, s1, s2;
  const char *s1label, *s2label;
  getPrices(usd, eur, chf, isBtc, main, s1, s2, s1label, s2label);

  String sym = String(currencySymbols[currencyMode]);
  String priceStr = sym + fmtPrice(main, cents);
  lv_label_set_text(price_lbl, priceStr.c_str());

  char buf[32];
  snprintf(buf, sizeof(buf), "%s %s", s1label, fmtPrice(s1, cents).c_str());
  lv_label_set_text(s1_lbl, buf);
  snprintf(buf, sizeof(buf), "%s %s", s2label, fmtPrice(s2, cents).c_str());
  lv_label_set_text(s2_lbl, buf);

  String chgStr = fmtChange(chg);
  lv_label_set_text(chg_lbl, chgStr.c_str());
  lv_obj_set_style_text_color(chg_lbl, (chg >= 0) ? COL_UP : COL_DN, 0);
}

void updateUI() {
  bool isGram = (currencyMode >= 3);
  lv_label_set_text(lbl_mode, modeLabels[currencyMode]);

  updateRow(lbl_btc_price,    lbl_btc_s1,    lbl_btc_s2,    lbl_btc_chg,
            btc_usd,    btc_eur,    btc_chf,    btc_chg,    false,  true);
  updateRow(lbl_gold_price,   lbl_gold_s1,   lbl_gold_s2,   lbl_gold_chg,
            gold_usd,   gold_eur,   gold_chf,   gold_chg,   isGram, false);
  updateRow(lbl_silver_price, lbl_silver_s1, lbl_silver_s2, lbl_silver_chg,
            silver_usd, silver_eur, silver_chf, silver_chg, true,   false);
}

void updateHeader() {
  String t = timeClient.getFormattedTime().substring(0, 5);
  lv_label_set_text(lbl_time, t.c_str());

  time_t epoch = timeClient.getEpochTime();
  struct tm* ti = localtime(&epoch);
  char dateBuf[20];
  const char* days[] = {"So","Mo","Di","Mi","Do","Fr","Sa"};
  snprintf(dateBuf, sizeof(dateBuf), "%s %02d.%02d.%04d",
    days[ti->tm_wday], ti->tm_mday, ti->tm_mon+1, ti->tm_year+1900);
  lv_label_set_text(lbl_date, dateBuf);
}

void updateCountdown() {
  unsigned long elapsed = millis() - lastRefresh;
  unsigned long rem = (elapsed < REFRESH_MS) ? REFRESH_MS - elapsed : 0;
  unsigned long secs = rem / 1000;
  char buf[30];
  snprintf(buf, sizeof(buf), "refresh in %lu:%02lu", secs / 60, secs % 60);
  lv_label_set_text(lbl_countdown, buf);
}

// ──────────────────────────────────────────
// WiFiManager Setup-Screen (LVGL)
// ──────────────────────────────────────────
void showWifiSetupScreen(WiFiManager* wm) {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl, "WLAN Setup");
  lv_obj_set_style_text_color(lbl, COL_ORANGE, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_40, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 20, 20);

  lv_obj_t *lbl2 = lv_label_create(scr);
  lv_label_set_text(lbl2, "Hotspot: " AP_NAME "\nPasswort: " AP_PASSWORD "\n\nBrowser: 192.168.4.1");
  lv_obj_set_style_text_color(lbl2, COL_WHITE, 0);
  lv_obj_set_style_text_font(lbl2, &lv_font_montserrat_24, 0);
  lv_obj_align(lbl2, LV_ALIGN_TOP_LEFT, 20, 90);
}

// ──────────────────────────────────────────
// Touch Callback: Modus wechseln
// ──────────────────────────────────────────
static bool touchWasPressed = false;

void handleTouch() {
  TouchPoint tp = readTouch();
  if (tp.pressed && !touchWasPressed) {
    touchWasPressed = true;
    currencyMode = (currencyMode + 1) % 6;
    if (dataValid) updateUI();
  } else if (!tp.pressed) {
    touchWasPressed = false;
  }
}

// ──────────────────────────────────────────
// WiFi verbinden
// ──────────────────────────────────────────
void connectWiFi() {
  WiFiManager wm;
  wm.setConnectTimeout(30);
  wm.setConfigPortalTimeout(180);
  wm.setAPCallback([](WiFiManager* wm) {
    showWifiSetupScreen(wm);
  });

  if (!wm.autoConnect(AP_NAME, AP_PASSWORD)) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "WLAN Fehler!\nNeustart...");
    lv_obj_set_style_text_color(lbl, COL_DN, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
    lv_obj_center(lbl);
    lv_task_handler();
    delay(3000);
    ESP.restart();
  }
}

// ──────────────────────────────────────────
// Setup
// ──────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Display init
  gfx->begin();
  gfx->fillScreen(BLACK);

  // Bootlogo (direkt via GFX, vor LVGL)
  showBootLogo();

  // I2C für Touch
  Wire.begin(TOUCH_SDA, TOUCH_SCL);

  // LVGL init
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCR_W * 20);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCR_W;
  disp_drv.ver_res = SCR_H;
  disp_drv.flush_cb = lvgl_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = lvgl_touch_cb;
  lv_indev_drv_register(&indev_drv);

  // WiFi
  connectWiFi();

  // NTP + Preise
  timeClient.begin();
  timeClient.update();

  // UI aufbauen
  buildUI();

  if (fetchPrices()) {
    dataValid = true;
    lastRefresh = millis();
    updateUI();
  }
  updateHeader();
}

// ──────────────────────────────────────────
// Loop
// ──────────────────────────────────────────
void loop() {
  lv_task_handler();
  handleTouch();

  unsigned long now = millis();

  // NTP + Header jede Minute
  static unsigned long lastHdr = 0;
  if (now - lastHdr >= 60000UL) {
    timeClient.update();
    updateHeader();
    lastHdr = now;
  }

  // Countdown jede Sekunde
  static unsigned long lastCd = 0;
  if (now - lastCd >= 1000UL) {
    if (dataValid) updateCountdown();
    lastCd = now;
  }

  // Stilles Refresh alle 5 Min
  if (now - lastRefresh >= REFRESH_MS) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      delay(3000);
    }
    if (fetchPrices()) {
      dataValid = true;
      lastRefresh = millis();
      updateUI();
    } else {
      lastRefresh = millis() - REFRESH_MS + 60000UL;
    }
  }

  delay(5);
}
