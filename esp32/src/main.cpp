#include <Arduino.h>
// ──────────────────────────────────────────
// FLASHTICKER v1.7
// Changelog:
//   v1.0 - Initiale Version, BTC/Gold/Silver, Touch Währungswechsel
//   v1.1 - FLASHTICKER Name, Datum gleiche Grösse wie Uhrzeit, Grid-Layout
//   v1.2 - Spalten fix: Stack und % klar getrennt, % vertikal zentriert
//   v1.3 - Stack weiter rechts, % Spalte schmaler (max zweistellig)
//   v1.4 - WiFiManager: Captive Portal, kein hardcodiertes WLAN
//   v1.5 - Bootlogo, QR Code im WiFi Setup, stilles Kurs-Refresh
//   v1.6 - RGB LED: Blau=laden, Orange=OK, Rot=Fehler
//   v1.7 - LED PWM Dimming für korrekte Farben
// ──────────────────────────────────────────
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <qrcode.h>
#include "logo.h"

// ──────────────────────────────────────────
// Konfiguration
// ──────────────────────────────────────────
#define AP_NAME             "FLASHTICKER"
#define AP_PASSWORD         "flashticker"
#define NTP_SERVER          "pool.ntp.org"
#define NTP_OFFSET_SEC      7200
#define REFRESH_MS          (5UL * 60UL * 1000UL)
#define DISPLAY_ROTATION    1

// ──────────────────────────────────────────
// RGB LED (active-low, PWM gedimmt)
// ──────────────────────────────────────────
#define LED_R   4
#define LED_G  16
#define LED_B  17

#define LED_CH_R  0
#define LED_CH_G  1
#define LED_CH_B  2
#define LED_FREQ  5000
#define LED_RES   8   // 8-bit = 0-255

void ledInit() {
  ledcSetup(LED_CH_R, LED_FREQ, LED_RES);
  ledcSetup(LED_CH_G, LED_FREQ, LED_RES);
  ledcSetup(LED_CH_B, LED_FREQ, LED_RES);
  ledcAttachPin(LED_R, LED_CH_R);
  ledcAttachPin(LED_G, LED_CH_G);
  ledcAttachPin(LED_B, LED_CH_B);
}

// active-low: 0=voll an, 255=aus
void ledSet(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(LED_CH_R, 255 - r);
  ledcWrite(LED_CH_G, 255 - g);
  ledcWrite(LED_CH_B, 255 - b);
}

void ledOff()    { ledSet(0,   0,   0);   }
void ledBlue()   { ledSet(0,   0,   180); }
void ledOrange() { ledSet(255, 80,  0);   } // Rot voll, Grün gedimmt
void ledRed()    { ledSet(255, 0,   0);   }

// Touch Pins CYD (eigener HSPI Bus)
#define TOUCH_CS    33
#define TOUCH_CLK   25
#define TOUCH_MOSI  32
#define TOUCH_MISO  39

// ──────────────────────────────────────────
// Display
// ──────────────────────────────────────────
#define SCR_W   320
#define SCR_H   240
#define HDR_H   28
#define FTR_H   14
#define ROW_H   ((SCR_H - HDR_H - FTR_H) / 3)

// ──────────────────────────────────────────
// Farben
// ──────────────────────────────────────────
#define COL_BG      0x0841
#define COL_HEADER  0x0861
#define COL_ORANGE  0xFB00
#define COL_WHITE   TFT_WHITE
#define COL_GREY1   0xAD55
#define COL_GREY2   0x7BEF
#define COL_LABEL   0x4208
#define COL_UP      0x07E0
#define COL_DN      0xF800
#define COL_DIV     0x18C3

// ──────────────────────────────────────────
// Modi: 0-2 = oz, 3-5 = Gramm
// ──────────────────────────────────────────
uint8_t currencyMode = 0;
const char* modeLabels[]   = {"USD/oz","EUR/oz","CHF/oz","USD/g","EUR/g","CHF/g"};
const char* currencySymbols[] = {"$","EUR ","CHF ","$","EUR ","CHF "};
#define TROY_OZ_TO_G 31.1035f

// ──────────────────────────────────────────
// Globals
// ──────────────────────────────────────────
TFT_eSPI    tft = TFT_eSPI();
SPIClass    touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS);
WiFiUDP     ntpUDP;
NTPClient   timeClient(ntpUDP, NTP_SERVER, NTP_OFFSET_SEC, 60000);

float btc_usd, btc_eur, btc_chf, btc_chg;
float gold_usd, gold_eur, gold_chf, gold_chg;
float silver_usd, silver_eur, silver_chf, silver_chg;

unsigned long lastRefresh = 0;
bool dataValid = false;

// ──────────────────────────────────────────
// Bootlogo
// ──────────────────────────────────────────
void showBootLogo() {
  tft.fillScreen(COL_BG);
  int x = (SCR_W - LOGO_W) / 2;
  int y = (SCR_H - LOGO_H) / 2;
  uint16_t lineBuf[LOGO_W];
  for (int row = 0; row < LOGO_H; row++) {
    for (int col = 0; col < LOGO_W; col++) {
      lineBuf[col] = pgm_read_word(&logo_data[row * LOGO_W + col]);
    }
    tft.pushImage(x, y + row, LOGO_W, 1, lineBuf);
  }
  delay(2500);
}

// ──────────────────────────────────────────
// QR Code zeichnen
// ──────────────────────────────────────────
void drawQRCode(const char* text, int x, int y, int scale) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, text);

  for (int row = 0; row < qrcode.size; row++) {
    for (int col = 0; col < qrcode.size; col++) {
      uint16_t col565 = qrcode_getModule(&qrcode, col, row) ? TFT_BLACK : TFT_WHITE;
      tft.fillRect(x + col * scale, y + row * scale, scale, scale, col565);
    }
  }
}

// ──────────────────────────────────────────
// WiFi via WiFiManager
// ──────────────────────────────────────────
void connectWiFi() {
  WiFiManager wm;
  wm.setConnectTimeout(30);
  wm.setConfigPortalTimeout(180);

  wm.setAPCallback([](WiFiManager* wm) {
    tft.fillScreen(TFT_BLACK);
    // Titel
    tft.setTextColor(COL_ORANGE, TFT_BLACK);
    tft.drawString("WLAN Setup", 8, 6, 2);
    // Infos
    tft.setTextColor(COL_WHITE, TFT_BLACK);
    tft.drawString("Hotspot:", 8, 30, 1);
    tft.setTextColor(COL_UP, TFT_BLACK);
    tft.drawString(AP_NAME, 8, 42, 2);
    tft.setTextColor(COL_WHITE, TFT_BLACK);
    tft.drawString("PW: " AP_PASSWORD, 8, 62, 1);
    tft.drawString("192.168.4.1", 8, 76, 1);
    // QR Code (WIFI:S:FLASHTICKER;T:WPA;P:flashticker;;)
    drawQRCode("WIFI:S:FLASHTICKER;T:WPA;P:flashticker;;", 165, 10, 3);
    tft.setTextColor(COL_GREY2, TFT_BLACK);
    tft.drawString("Scan zum Verbinden", 8, 210, 1);
  });

  if (!wm.autoConnect(AP_NAME, AP_PASSWORD)) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(COL_DN, TFT_BLACK);
    tft.drawString("WLAN Fehler!", 10, 100, 2);
    tft.drawString("Neustart...", 10, 125, 2);
    delay(3000);
    ESP.restart();
  }

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(COL_UP, TFT_BLACK);
  tft.drawString("Verbunden!", 10, 80, 2);
  tft.setTextColor(COL_GREY2, TFT_BLACK);
  tft.drawString(WiFi.SSID().c_str(), 10, 105, 2);
  delay(800);
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
  http.addHeader("User-Agent", "ESP32-Ticker/1.5");
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
// Kurse abrufen – still im Hintergrund
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

  btc_usd    = doc["bitcoin"]["usd"]                    | 0.0f;
  btc_eur    = doc["bitcoin"]["eur"]                    | 0.0f;
  btc_chf    = doc["bitcoin"]["chf"]                    | 0.0f;
  btc_chg    = doc["bitcoin"]["usd_24h_change"]         | 0.0f;
  gold_usd   = doc["pax-gold"]["usd"]                   | 0.0f;
  gold_eur   = doc["pax-gold"]["eur"]                   | 0.0f;
  gold_chf   = doc["pax-gold"]["chf"]                   | 0.0f;
  gold_chg   = doc["pax-gold"]["usd_24h_change"]        | 0.0f;
  silver_usd = doc["kinesis-silver"]["usd"]             | 0.0f;
  silver_eur = doc["kinesis-silver"]["eur"]             | 0.0f;
  silver_chf = doc["kinesis-silver"]["chf"]             | 0.0f;
  silver_chg = doc["kinesis-silver"]["usd_24h_change"]  | 0.0f;

  return btc_usd > 100;
}

// ──────────────────────────────────────────
// Formatierung
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
// Zeichnen
// ──────────────────────────────────────────
void drawHeader() {
  tft.fillRect(0, 0, SCR_W, HDR_H, COL_HEADER);
  tft.drawFastHLine(0, HDR_H, SCR_W, COL_ORANGE);

  tft.setTextColor(COL_ORANGE, COL_HEADER);
  tft.drawString("FLASHTICKER", 8, 7, 2);

  int mlw = tft.textWidth("FLASHTICKER", 2) + 12;
  tft.setTextColor(COL_GREY2, COL_HEADER);
  tft.drawString(modeLabels[currencyMode], mlw, 10, 1);

  String t = timeClient.getFormattedTime().substring(0, 5);
  tft.setTextColor(COL_WHITE, COL_HEADER);
  int tw = tft.textWidth(t, 2);
  tft.drawString(t, SCR_W - tw - 8, 6, 2);

  time_t epoch = timeClient.getEpochTime();
  struct tm* ti = localtime(&epoch);
  char dateBuf[20];
  const char* days[] = {"So","Mo","Di","Mi","Do","Fr","Sa"};
  snprintf(dateBuf, sizeof(dateBuf), "%s %02d.%02d.%04d",
    days[ti->tm_wday], ti->tm_mday, ti->tm_mon+1, ti->tm_year+1900);
  tft.setTextColor(COL_GREY2, COL_HEADER);
  int dw = tft.textWidth(dateBuf, 2);
  tft.drawString(dateBuf, SCR_W - tw - 8 - dw - 6, 6, 2);
}

void drawRow(int y, const char* label,
             float usd, float eur, float chf, float chg, bool cents, bool isBtc = false) {
  tft.fillRect(0, y, SCR_W, ROW_H, COL_BG);
  tft.drawFastHLine(0, y + ROW_H - 1, SCR_W, COL_DIV);

  int midY = y + ROW_H / 2;

  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString(label, 8, midY - 8, 2);

  float main, s1, s2;
  const char* s1label; const char* s2label;
  getPrices(usd, eur, chf, isBtc, main, s1, s2, s1label, s2label);

  String sym = String(currencySymbols[currencyMode]);
  String mainStr = sym + fmtPrice(main, cents);
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.drawString(mainStr, 52, midY - 13, 4);

  int stackRight = 264;
  String s1Str = String(s1label) + " " + fmtPrice(s1, cents);
  String s2Str = String(s2label) + " " + fmtPrice(s2, cents);
  tft.setTextColor(COL_GREY1, COL_BG);
  tft.drawString(s1Str, stackRight - tft.textWidth(s1Str, 1), midY - 13, 1);
  tft.setTextColor(COL_GREY2, COL_BG);
  tft.drawString(s2Str, stackRight - tft.textWidth(s2Str, 1), midY - 3, 1);

  String chgStr = fmtChange(chg);
  tft.setTextColor((chg >= 0) ? COL_UP : COL_DN, COL_BG);
  int chgW = tft.textWidth(chgStr, 2);
  tft.drawString(chgStr, SCR_W - chgW - 4, midY - 8, 2);
}

void drawFooter(unsigned long msRemaining) {
  int y = HDR_H + 3 * ROW_H;
  tft.fillRect(0, y, SCR_W, FTR_H, COL_BG);
  unsigned long secs = msRemaining / 1000;
  char buf[30];
  snprintf(buf, sizeof(buf), "refresh in %lu:%02lu", secs / 60, secs % 60);
  tft.setTextColor(COL_DIV, COL_BG);
  int bw = tft.textWidth(buf, 1);
  tft.drawString(buf, SCR_W - bw - 4, y + 2, 1);
}

void drawAll() {
  tft.fillScreen(COL_BG);
  drawHeader();
  int startY = HDR_H + 1;
  bool isGram = (currencyMode >= 3);
  drawRow(startY + 0 * ROW_H, "BTC",    btc_usd,    btc_eur,    btc_chf,    btc_chg,    false, true);
  drawRow(startY + 1 * ROW_H, "GOLD",   gold_usd,   gold_eur,   gold_chf,   gold_chg,   isGram);
  drawRow(startY + 2 * ROW_H, "SILVER", silver_usd, silver_eur, silver_chf, silver_chg, true);
  unsigned long elapsed = millis() - lastRefresh;
  unsigned long rem = (elapsed < REFRESH_MS) ? REFRESH_MS - elapsed : 0;
  drawFooter(rem);
}

void updateCountdown() {
  unsigned long elapsed = millis() - lastRefresh;
  unsigned long rem = (elapsed < REFRESH_MS) ? REFRESH_MS - elapsed : 0;
  drawFooter(rem);
}

// ──────────────────────────────────────────
// Setup & Loop
// ──────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // LED initialisieren
  ledInit();
  ledBlue(); // Blau = Laden/Startup

  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(TFT_BLACK);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Touch
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSPI);
  touch.setRotation(DISPLAY_ROTATION);

  // Bootlogo
  showBootLogo();

  // WiFi
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    ledBlue(); // Noch laden
    timeClient.begin();
    timeClient.update();
    if (fetchPrices()) {
      dataValid = true;
      lastRefresh = millis();
      ledOrange(); // Alles OK
      drawAll();
    } else {
      ledRed(); // API Fehler
      drawAll();
    }
  } else {
    ledRed(); // WLAN Fehler
  }
}

void loop() {
  timeClient.update();
  unsigned long now = millis();

  // Touch – Modus wechseln
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    if (p.z > 200) {
      currencyMode = (currencyMode + 1) % 6;
      if (dataValid) drawAll();
      while (touch.touched()) delay(10);
      delay(200);
    }
  }

  // Stilles Refresh alle 5 Min
  if (now - lastRefresh >= REFRESH_MS) {
    ledBlue(); // Laden
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      delay(3000);
    }
    if (fetchPrices()) {
      dataValid = true;
      lastRefresh = millis();
      ledOrange(); // OK
      drawAll();
    } else {
      ledRed(); // Fehler
      lastRefresh = millis() - REFRESH_MS + 60000UL;
    }
  }

  // Header jede Minute
  static unsigned long lastHdr = 0;
  if (now - lastHdr >= 60000UL) {
    if (dataValid) drawHeader();
    lastHdr = now;
  }

  // Countdown jede Sekunde
  static unsigned long lastCd = 0;
  if (now - lastCd >= 1000UL) {
    if (dataValid) updateCountdown();
    lastCd = now;
  }

  delay(100);
}
