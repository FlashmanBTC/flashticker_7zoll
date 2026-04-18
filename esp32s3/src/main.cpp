// ============================================================
//  FLASHTICKER 7" – BTC · Gold · Silber Preisticker
//  ESP32-S3 · Elecrow CrowPanel 7.0" (800×480)
//  LovyanGFX + LVGL 8.3 · WiFiManager · ticker.blitzi.me
// ============================================================
//
//  CHANGELOG
//  ---------------------------------------------------------
//  v3.2 (2026-04-14)
//    - 1H Chart-Periode hinzugefügt
//    - Period-Buttons in Status Bar verschoben (neben Währung)
//    - Trenner zwischen Perioden- und Währungsbuttons
//    - Hi/Lo im Vollbild unten (wo Buttons vorher waren)
//    - Dark Badge hinter Hi/Lo Labels (immer lesbar über Chart)
//    - Top Card: mehr Chartfläche ohne Buttons
//
//  v3.1 (2026-04-14)
//    - Loading Screen: lv_bar statt manuellem Resize (Balken füllt sich korrekt)
//    - Kein Flash mehr zwischen Bootscreen und Loading Screen
//    - %-Anzeige: 20pt, unterhalb des Balkens
//    - Hintergrund-Refresh (5 Min) ohne Loading Screen
//    - Bug Fix: Bottom-Cards korrekt bei Silber/Gold als Boot-Default
//
//  v3.0 (2026-04-14)
//    - Einstellungen-Screen (Zahnrad im Header)
//    - Boot-Defaults per Touch konfigurierbar:
//      Standard Asset, Waehrung, Chart-Periode, Vollbild
//    - Einstellungen werden in NVS (Flash) gespeichert
//    - Version "v3.0" auf Bootscreen und im Einstellungen-Header
//    - Period-Button Highlight Fix: zeigt gespeicherte Default-Periode
//
//  v2.0 (2026-04-14)
//    - Eigene API: ticker.blitzi.me (kein CoinGecko/Binance)
//    - Keine Rate-Limit Delays mehr beim API-Abruf
//    - Alle Chart-Perioden beim Boot vorgeladen (kein UI-Lag)
//    - Vollbild-Modus für Top-Card (72pt Preis + Hi/Lo)
//    - Custom Montserrat Fonts: 64pt und 72pt
//    - QR-Code im WiFi-Setup-Screen (WIFI:S:... Standard)
//    - WiFiManager: kein Timeout mehr
//    - Screen-Clear nach WiFi-Setup (verhindert Artefakte)
//    - "Browser öffnen" Umlaut-Bug behoben
//    - Label-Positionierung vereinheitlicht (TOP_LEFT)
//
//  v1.0 (2026-04-04)
//    - Erstveröffentlichung
//    - BTC / Gold / Silber in USD · EUR · CHF
//    - LovyanGFX + LVGL 8.3 auf CrowPanel 7.0"
//    - WiFiManager Portal · NTP Uhrzeit · Touch UI
//    - Linien-Chart mit 1T / 1W / 1M Perioden
// ============================================================

#define LGFX_USE_V1
#define FW_VERSION "3.3"
#include <Arduino.h>
#include "Flashman_logo.h"
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <PCA9557.h>
#include <driver/i2c.h>
#include <Preferences.h>

LV_FONT_DECLARE(lv_font_montserrat_64);
LV_FONT_DECLARE(lv_font_montserrat_72);


// ============================================================
//  Display-Konfiguration (CrowPanel 7.0", offiziell Elecrow)
// ============================================================
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB     _bus;
  lgfx::Panel_RGB   _panel;
  lgfx::Light_PWM   _bl;
  lgfx::Touch_GT911 _touch;

  LGFX() {
    { auto c = _panel.config();
      c.memory_width = c.panel_width  = 800;
      c.memory_height= c.panel_height = 480;
      c.offset_x = c.offset_y = 0;
      _panel.config(c); }


    { auto c = _bus.config();
      c.panel = &_panel;
      // Blue D0-D4
      c.pin_d0 =GPIO_NUM_15; c.pin_d1 =GPIO_NUM_7;
      c.pin_d2 =GPIO_NUM_6;  c.pin_d3 =GPIO_NUM_5;  c.pin_d4 =GPIO_NUM_4;
      // Green D5-D10
      c.pin_d5 =GPIO_NUM_9;  c.pin_d6 =GPIO_NUM_46;
      c.pin_d7 =GPIO_NUM_3;  c.pin_d8 =GPIO_NUM_8;
      c.pin_d9 =GPIO_NUM_16; c.pin_d10=GPIO_NUM_1;
      // Red D11-D15
      c.pin_d11=GPIO_NUM_14; c.pin_d12=GPIO_NUM_21;
      c.pin_d13=GPIO_NUM_47; c.pin_d14=GPIO_NUM_48; c.pin_d15=GPIO_NUM_45;
      // Control
      c.pin_henable=GPIO_NUM_41; c.pin_vsync=GPIO_NUM_40;
      c.pin_hsync  =GPIO_NUM_39; c.pin_pclk =GPIO_NUM_0;
      c.freq_write =12000000;
      // Timing
      c.hsync_polarity=0; c.hsync_front_porch=40;
      c.hsync_pulse_width=48; c.hsync_back_porch=40;
      c.vsync_polarity=0; c.vsync_front_porch=1;
      c.vsync_pulse_width=31; c.vsync_back_porch=13;
      c.pclk_active_neg=1; c.de_idle_high=0; c.pclk_idle_high=0;
      _bus.config(c); _panel.setBus(&_bus); }

    { auto c = _bl.config();
      c.pin_bl = GPIO_NUM_2;
      _bl.config(c); _panel.light(&_bl); }

    { auto c = _touch.config();
      c.x_min=0; c.x_max=799; c.y_min=0; c.y_max=479;
      c.pin_int=-1; c.pin_rst=-1; c.bus_shared=true; c.offset_rotation=0;
      c.i2c_port=I2C_NUM_1; c.pin_sda=GPIO_NUM_19; c.pin_scl=GPIO_NUM_20;
      c.freq=400000; c.i2c_addr=0x5D; // INT=LOW during reset → Adresse 0x5D
      _touch.config(c); _panel.setTouch(&_touch); }

    setPanel(&_panel);
  }
};

LGFX tft;
PCA9557 Out;
Preferences prefs;

// ============================================================
//  LVGL Draw-Buffer (in PSRAM um SRAM für WiFi/TLS freizugeben)
// ============================================================
#define SCR_W 800
#define SCR_H 480
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
#define LV_BUF_LINES 10

void lv_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  tft.startWrite();
  tft.pushImageDMA(area->x1, area->y1,
                   area->x2 - area->x1 + 1,
                   area->y2 - area->y1 + 1,
                   (lgfx::rgb565_t*)&color_p->full);
  tft.endWrite(); // wartet bis DMA fertig → kein Race Condition
  lv_disp_flush_ready(drv);
}

void lv_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  uint16_t x, y;
  bool pressed = tft.getTouch(&x, &y);
  data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  if (pressed) {
    data->point.x = x;
    data->point.y = y;
  }
  // Beim Loslassen point NICHT auf 0,0 setzen –
  // LVGL nutzt sonst 0,0 als Koordinate für LV_EVENT_CLICKED
}

// ============================================================
//  Flashman Logo – LVGL Image Descriptor
// ============================================================
static const lv_img_dsc_t flashman_logo_img = {
  .header = {
    .cf           = LV_IMG_CF_TRUE_COLOR,
    .always_zero  = 0,
    .reserved     = 0,
    .w            = LOGO_W,
    .h            = LOGO_H,
  },
  .data_size = (uint32_t)(LOGO_W * LOGO_H * sizeof(uint16_t)),
  .data = (const uint8_t*)logo_data,
};

// ============================================================
//  App-State
// ============================================================
enum Currency { EUR=0, CHF=1, USD=2 };
const char* CUR_LABEL[]  = { "EUR", "CHF", "USD" };
const char* CUR_SYMBOL[] = { "EUR", "CHF", "$"   };

Currency    g_cur     = EUR;
float g_btc[3]={0}, g_btc_chg[3]={0};
float g_gold[3]={0}, g_gold_chg[3]={0};   // PAX Gold → 1 Token = 1 troy oz
float g_silver[3]={0}, g_silver_chg[3]={}; // Kinesis Silver → 1 Token = 1 troy oz
bool  g_data_ok = false;
char          g_last_update[12] = "--:--";
unsigned long g_last_fetch      = 0;

#define FETCH_EVERY_MS  (5UL*60UL*1000UL)
#define TROY_OZ_G       31.1035f

// Chart-Daten: [Asset][Periode][Punkt]
#define CHART_MAX_PTS  42
#define NUM_ASSETS     3
#define NUM_PERIODS    4  // 0=1H, 1=1T, 2=1W, 3=1M
// Skalierungsdivisor pro Asset damit Werte in int16 passen (BTC >32767)
static const int CHART_DIV[NUM_ASSETS] = { 10, 1, 1 };
int16_t g_chart_data[NUM_ASSETS][NUM_PERIODS][CHART_MAX_PTS];
uint8_t g_chart_cnt[NUM_ASSETS][NUM_PERIODS];
uint8_t g_chart_period = 1;  // 0=1H, 1=1T, 2=1W, 3=1M (default 1T)

// Periodenbasierte Änderung (aus Chart berechnet)
float g_chg_h[NUM_ASSETS];  // 1H Änderung % je Asset
float g_chg_w[NUM_ASSETS];  // 1W Änderung % je Asset
float g_chg_m[NUM_ASSETS];  // 1M Änderung % je Asset

WiFiUDP    ntpUDP;
NTPClient  ntpClient(ntpUDP, "pool.ntp.org", 3600);

// ============================================================
//  Asset-State
// ============================================================
enum Asset { ASSET_BTC=0, ASSET_GOLD=1, ASSET_SILVER=2 };
Asset g_active_asset = ASSET_BTC;
Asset g_b1_asset     = ASSET_GOLD;
Asset g_b2_asset     = ASSET_SILVER;

// Boot-Defaults (NVS gespeichert, gelten ab naechstem Start)
uint8_t g_def_asset      = 0;  // 0=BTC, 1=Gold, 2=Silber
uint8_t g_def_currency   = 0;  // 0=EUR, 1=CHF, 2=USD
uint8_t g_def_period     = 1;  // 0=1H, 1=1T, 2=1W, 3=1M
bool    g_def_fullscreen = false;

// ============================================================
//  Forward declarations
// ============================================================
void update_prices();
void update_chart_series();
void fetch_chart_asset(Asset a, uint8_t period);
void fetch_all_charts(uint8_t period);
void set_active_asset(Asset a);
void show_boot_screen();
void show_wifi_screen();
void show_settings_screen(lv_obj_t *prev_scr);
void show_loading_screen();
void update_loading(int pct, const char *msg);
void toggle_fullscreen();
void update_fs_highlow();
void load_settings();
void save_settings();

// ============================================================
//  LVGL UI – Referenzen
// ============================================================
lv_obj_t *lbl_time, *lbl_date;
lv_obj_t *lbl_status;
lv_obj_t *g_chart;
lv_chart_series_t *g_chart_ser;
lv_obj_t *g_btn_period[4];
lv_obj_t *g_btn_cur[3];

// Top-Card (dynamisch)
lv_obj_t *lbl_top_icon, *lbl_top_price, *lbl_top_chg, *lbl_top_sub, *lbl_top_24h;

// Bottom-Cards (dynamisch)
lv_obj_t *lbl_b1_icon, *lbl_b1_price, *lbl_b1_sub, *lbl_b1_chg;
lv_obj_t *lbl_b2_icon, *lbl_b2_price, *lbl_b2_sub, *lbl_b2_chg;

// Vollbild-Modus
bool      g_fullscreen   = false;
lv_obj_t *g_main_scr     = nullptr;

// Loading-Screen Referenzen
static lv_obj_t *g_load_fill = nullptr;
static lv_obj_t *g_load_msg  = nullptr;
static lv_obj_t *g_load_pct  = nullptr;
lv_obj_t *g_top_card     = nullptr;
lv_obj_t *g_b1_card      = nullptr;
lv_obj_t *g_b2_card      = nullptr;
lv_obj_t *g_btn_fullscreen = nullptr;
lv_obj_t *lbl_fs_high    = nullptr;
lv_obj_t *lbl_fs_low     = nullptr;

// ============================================================
//  Hilfsfunktionen
// ============================================================
// Tausender-Trennzeichen manuell formatieren (ESP32 printf kennt kein %,)
static void fmt_price(char *buf, size_t sz, float val, const char *sym, int decimals = 3) {
  char num[24];
  if (val >= 10000) {
    uint32_t iv = (uint32_t)val;
    snprintf(num, sizeof(num), "%u,%03u", iv/1000, iv%1000);
  } else if (val >= 100) {
    snprintf(num, sizeof(num), "%.*f", decimals < 2 ? 2 : decimals, val);
  } else {
    snprintf(num, sizeof(num), "%.*f", decimals, val);
  }
  snprintf(buf, sz, "%s %s", sym, num);
}

static void fmt_change(char *buf, size_t sz, float chg) {
  snprintf(buf, sz, "%s%.2f%%", chg >= 0 ? "+" : "", chg);
}

// ============================================================
//  UI aufbauen
// ============================================================
// Hilfsfunktion: sauberes Card-Objekt ohne LVGL-Default-Theme
static lv_obj_t* make_card(lv_obj_t *parent, int x, int y, int w, int h,
                            uint32_t bg=0x0F0F1E, bool bordered=true, bool clickable=false) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  if (bordered) {
    lv_obj_set_style_border_color(obj, lv_color_hex(0x1E1E3A), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
  }
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  if (!clickable) lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  return obj;
}

void create_ui() {
  // Neuen Screen erstellen ohne ihn zu laden – bleibt im Hintergrund bis lv_scr_load()
  lv_obj_t *scr = lv_obj_create(NULL);
  g_main_scr = scr;
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A10), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // ---------- HEADER (y=0, h=60) ----------
  lv_obj_t *hdr = make_card(scr, 0, 0, SCR_W, 60);

  // Logo + Titel
  lv_obj_t *lbl_title = lv_label_create(hdr);
  lv_label_set_text(lbl_title, "FLASHTICKER");
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_28, 0);
  lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 16, 0);

  // Zeit
  lbl_time = lv_label_create(hdr);
  lv_label_set_text(lbl_time, "--:--:--");
  lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_24, 0);
  lv_obj_align(lbl_time, LV_ALIGN_RIGHT_MID, -16, -8);

  // Datum
  lbl_date = lv_label_create(hdr);
  lv_label_set_text(lbl_date, "----------");
  lv_obj_set_style_text_color(lbl_date, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_date, LV_ALIGN_RIGHT_MID, -16, 12);

  // Zahnrad-Button (Einstellungen)
  lv_obj_t *btn_gear = lv_btn_create(hdr);
  lv_obj_set_size(btn_gear, 40, 40);
  lv_obj_align(btn_gear, LV_ALIGN_LEFT_MID, 230, 0);
  lv_obj_set_style_radius(btn_gear, 6, 0);
  lv_obj_set_style_pad_all(btn_gear, 0, 0);
  lv_obj_set_style_bg_color(btn_gear, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_bg_color(btn_gear, lv_color_hex(0x2A2A40), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_gear, 0, 0);
  lv_obj_t *lbl_gear = lv_label_create(btn_gear);
  lv_label_set_text(lbl_gear, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_color(lbl_gear, lv_color_hex(0x555566), 0);
  lv_obj_center(lbl_gear);
  lv_obj_add_event_cb(btn_gear, [](lv_event_t *e) {
    show_settings_screen(lv_scr_act());
  }, LV_EVENT_CLICKED, NULL);

  // ---------- TOP CARD (y=61, h=197) – dynamisches Asset ----------
  g_top_card = make_card(scr, 0, 61, SCR_W, 197);
  lv_obj_t *top_card = g_top_card;

  // ---- Chart als Hintergrund ----
  g_chart = lv_chart_create(top_card);
  lv_obj_set_pos(g_chart, 0, 0);
  lv_obj_set_size(g_chart, SCR_W, 197);
  lv_obj_set_style_bg_opa(g_chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_chart, 0, 0);
  lv_obj_set_style_pad_all(g_chart, 0, 0);
  lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_div_line_count(g_chart, 0, 0);  // keine Grid-Linien
  lv_chart_set_point_count(g_chart, 24);
  // kein SHIFT-Modus – wir nutzen set_ext_y_array direkt
  lv_obj_clear_flag(g_chart, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(g_chart, LV_OBJ_FLAG_SCROLLABLE);

  g_chart_ser = lv_chart_add_series(g_chart, lv_color_hex(0x00C853), LV_CHART_AXIS_PRIMARY_Y);

  // Draw-Callback für transparenten Fill unter der Linie
  lv_obj_add_event_cb(g_chart, [](lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part == LV_PART_ITEMS) {
      dsc->rect_dsc->radius = 0;
      // Fill-Farbe = Linienfarbe mit 40% Transparenz
      dsc->rect_dsc->bg_color = g_chart_ser->color;
      dsc->rect_dsc->bg_opa   = LV_OPA_40;
    }
  }, LV_EVENT_DRAW_PART_BEGIN, NULL);

  // ---- Vollbild-Button (unten rechts in Top Card) ----
  g_btn_fullscreen = lv_btn_create(top_card);
  lv_obj_set_size(g_btn_fullscreen, 36, 24);
  lv_obj_set_pos(g_btn_fullscreen, SCR_W - 44, 162);
  lv_obj_set_style_radius(g_btn_fullscreen, 4, 0);
  lv_obj_set_style_pad_all(g_btn_fullscreen, 0, 0);
  lv_obj_set_style_bg_color(g_btn_fullscreen, lv_color_hex(0x1A1A28), 0);
  lv_obj_set_style_border_color(g_btn_fullscreen, lv_color_hex(0x3A3A55), 0);
  lv_obj_set_style_border_width(g_btn_fullscreen, 1, 0);
  { lv_obj_t *fl = lv_label_create(g_btn_fullscreen);
    lv_label_set_text(fl, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(fl, lv_color_hex(0x666677), 0);
    lv_obj_center(fl); }
  lv_obj_add_event_cb(g_btn_fullscreen, [](lv_event_t *e) {
    toggle_fullscreen();
  }, LV_EVENT_CLICKED, NULL);

  // ---- High/Low Labels (nur im Vollbild sichtbar, Dark Badge für Lesbarkeit) ----
  lbl_fs_high = lv_label_create(top_card);
  lv_label_set_text(lbl_fs_high, "Hi: ---");
  lv_obj_set_style_text_color(lbl_fs_high, lv_color_hex(0x00C853), 0);
  lv_obj_set_style_text_font(lbl_fs_high, &lv_font_montserrat_20, 0);
  lv_obj_set_style_bg_color(lbl_fs_high, lv_color_hex(0x0A0A10), 0);
  lv_obj_set_style_bg_opa(lbl_fs_high, LV_OPA_80, 0);
  lv_obj_set_style_pad_hor(lbl_fs_high, 8, 0);
  lv_obj_set_style_pad_ver(lbl_fs_high, 4, 0);
  lv_obj_set_style_radius(lbl_fs_high, 4, 0);
  lv_obj_add_flag(lbl_fs_high, LV_OBJ_FLAG_HIDDEN);

  lbl_fs_low = lv_label_create(top_card);
  lv_label_set_text(lbl_fs_low, "Lo: ---");
  lv_obj_set_style_text_color(lbl_fs_low, lv_color_hex(0xE53935), 0);
  lv_obj_set_style_text_font(lbl_fs_low, &lv_font_montserrat_20, 0);
  lv_obj_set_style_bg_color(lbl_fs_low, lv_color_hex(0x0A0A10), 0);
  lv_obj_set_style_bg_opa(lbl_fs_low, LV_OPA_80, 0);
  lv_obj_set_style_pad_hor(lbl_fs_low, 8, 0);
  lv_obj_set_style_pad_ver(lbl_fs_low, 4, 0);
  lv_obj_set_style_radius(lbl_fs_low, 4, 0);
  lv_obj_add_flag(lbl_fs_low, LV_OBJ_FLAG_HIDDEN);

  // ---- Top Card Labels (dynamisch) ----
  lbl_top_icon = lv_label_create(top_card);
  lv_label_set_text(lbl_top_icon, "BITCOIN");
  lv_obj_set_style_text_color(lbl_top_icon, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_text_font(lbl_top_icon, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_top_icon, LV_ALIGN_TOP_LEFT, 24, 14);

  lbl_top_price = lv_label_create(top_card);
  lv_label_set_text(lbl_top_price, "---,---");
  lv_obj_set_style_text_color(lbl_top_price, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(lbl_top_price, &lv_font_montserrat_48, 0);
  lv_obj_align(lbl_top_price, LV_ALIGN_TOP_LEFT, 24, 40);

  lbl_top_chg = lv_label_create(top_card);
  lv_label_set_text(lbl_top_chg, "---");
  lv_obj_set_style_text_color(lbl_top_chg, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(lbl_top_chg, &lv_font_montserrat_32, 0);
  lv_obj_align(lbl_top_chg, LV_ALIGN_TOP_RIGHT, -24, 46);

  lbl_top_sub = lv_label_create(top_card);
  lv_label_set_text(lbl_top_sub, "");
  lv_obj_set_style_text_color(lbl_top_sub, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(lbl_top_sub, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_top_sub, LV_ALIGN_TOP_LEFT, 24, 102);

  lbl_top_24h = lv_label_create(top_card);
  lv_label_set_text(lbl_top_24h, "24h");
  lv_obj_set_style_text_color(lbl_top_24h, lv_color_hex(0x555566), 0);
  lv_obj_set_style_text_font(lbl_top_24h, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_top_24h, LV_ALIGN_TOP_RIGHT, -24, 88);

  // ---------- BOTTOM CARDS (y=260, h=176) – antippen wechselt Asset nach oben ----------
  g_b1_card = make_card(scr, 0, 260, 399, 176, 0x0F0F1E, true, true);
  lv_obj_t *b1_card = g_b1_card;
  g_b2_card = make_card(scr, 401, 260, 399, 176, 0x0F0F1E, true, true);
  lv_obj_t *b2_card = g_b2_card;

  lv_obj_add_event_cb(b1_card, [](lv_event_t *e) {
    set_active_asset(g_b1_asset);
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(b2_card, [](lv_event_t *e) {
    set_active_asset(g_b2_asset);
  }, LV_EVENT_CLICKED, NULL);

  // ---- B1 Labels ----
  lbl_b1_icon = lv_label_create(b1_card);
  lv_label_set_text(lbl_b1_icon, "Au  GOLD");
  lv_obj_set_style_text_color(lbl_b1_icon, lv_color_hex(0xFFD700), 0);
  lv_obj_set_style_text_font(lbl_b1_icon, &lv_font_montserrat_18, 0);
  lv_obj_align(lbl_b1_icon, LV_ALIGN_TOP_LEFT, 20, 14);

  lbl_b1_price = lv_label_create(b1_card);
  lv_label_set_text(lbl_b1_price, "--- / oz");
  lv_obj_set_style_text_color(lbl_b1_price, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(lbl_b1_price, &lv_font_montserrat_28, 0);
  lv_obj_align(lbl_b1_price, LV_ALIGN_TOP_LEFT, 20, 46);

  lbl_b1_sub = lv_label_create(b1_card);
  lv_label_set_text(lbl_b1_sub, "--- / g");
  lv_obj_set_style_text_color(lbl_b1_sub, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(lbl_b1_sub, &lv_font_montserrat_18, 0);
  lv_obj_align(lbl_b1_sub, LV_ALIGN_TOP_LEFT, 20, 90);

  lbl_b1_chg = lv_label_create(b1_card);
  lv_label_set_text(lbl_b1_chg, "---");
  lv_obj_set_style_text_color(lbl_b1_chg, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(lbl_b1_chg, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_b1_chg, LV_ALIGN_TOP_LEFT, 20, 122);

  lv_obj_t *lbl_b1_24h = lv_label_create(b1_card);
  lv_label_set_text(lbl_b1_24h, "24h");
  lv_obj_set_style_text_color(lbl_b1_24h, lv_color_hex(0x555566), 0);
  lv_obj_set_style_text_font(lbl_b1_24h, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_b1_24h, LV_ALIGN_TOP_LEFT, 100, 127);

  // ---- B2 Labels ----
  lbl_b2_icon = lv_label_create(b2_card);
  lv_label_set_text(lbl_b2_icon, "Ag  SILBER");
  lv_obj_set_style_text_color(lbl_b2_icon, lv_color_hex(0xC0C0C0), 0);
  lv_obj_set_style_text_font(lbl_b2_icon, &lv_font_montserrat_18, 0);
  lv_obj_align(lbl_b2_icon, LV_ALIGN_TOP_LEFT, 20, 14);

  lbl_b2_price = lv_label_create(b2_card);
  lv_label_set_text(lbl_b2_price, "--- / oz");
  lv_obj_set_style_text_color(lbl_b2_price, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(lbl_b2_price, &lv_font_montserrat_28, 0);
  lv_obj_align(lbl_b2_price, LV_ALIGN_TOP_LEFT, 20, 46);

  lbl_b2_sub = lv_label_create(b2_card);
  lv_label_set_text(lbl_b2_sub, "--- / g");
  lv_obj_set_style_text_color(lbl_b2_sub, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(lbl_b2_sub, &lv_font_montserrat_18, 0);
  lv_obj_align(lbl_b2_sub, LV_ALIGN_TOP_LEFT, 20, 90);

  lbl_b2_chg = lv_label_create(b2_card);
  lv_label_set_text(lbl_b2_chg, "---");
  lv_obj_set_style_text_color(lbl_b2_chg, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(lbl_b2_chg, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_b2_chg, LV_ALIGN_TOP_LEFT, 20, 122);

  lv_obj_t *lbl_b2_24h = lv_label_create(b2_card);
  lv_label_set_text(lbl_b2_24h, "24h");
  lv_obj_set_style_text_color(lbl_b2_24h, lv_color_hex(0x555566), 0);
  lv_obj_set_style_text_font(lbl_b2_24h, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_b2_24h, LV_ALIGN_TOP_LEFT, 100, 127);

  // ---------- STATUS BAR (y=438, h=42) ----------
  lv_obj_t *status_bar = lv_obj_create(scr);
  lv_obj_remove_style_all(status_bar);
  lv_obj_set_pos(status_bar, 0, 438);
  lv_obj_set_size(status_bar, SCR_W, 42);
  lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x080810), 0);
  lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(status_bar, lv_color_hex(0x1E1E3A), 0);
  lv_obj_set_style_border_width(status_bar, 1, 0);
  lv_obj_set_style_border_opa(status_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(status_bar, 0, 0);
  lv_obj_set_style_pad_all(status_bar, 0, 0);
  lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

  lbl_status = lv_label_create(status_bar);
  lv_label_set_text(lbl_status, "Verbinde...");
  lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x555566), 0);
  lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_status, LV_ALIGN_LEFT_MID, 16, 0);

  // ---------- Währungsbuttons rechts in Status Bar ----------
  const char* cur_labels[] = {"EUR", "CHF", "USD"};
  for (int i = 0; i < 3; i++) {
    g_btn_cur[i] = lv_btn_create(status_bar);
    lv_obj_set_size(g_btn_cur[i], 52, 26);
    lv_obj_align(g_btn_cur[i], LV_ALIGN_RIGHT_MID, -16 - (2-i) * 58, 0);
    lv_obj_set_style_radius(g_btn_cur[i], 4, 0);
    lv_obj_set_style_pad_all(g_btn_cur[i], 0, 0);
    bool active = (i == (int)g_cur);
    lv_obj_set_style_bg_color(g_btn_cur[i], active ? lv_color_hex(0x2A2A40) : lv_color_hex(0x111120), 0);
    lv_obj_set_style_border_color(g_btn_cur[i], lv_color_hex(0x3A3A55), 0);
    lv_obj_set_style_border_width(g_btn_cur[i], 1, 0);
    lv_obj_t *lbl = lv_label_create(g_btn_cur[i]);
    lv_label_set_text(lbl, cur_labels[i]);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, active ? lv_color_hex(0xF7931A) : lv_color_hex(0x555566), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(g_btn_cur[i], [](lv_event_t *e) {
      uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
      g_cur = (Currency)idx;
      // Alle Buttons zurücksetzen
      for (int j = 0; j < 3; j++) {
        lv_obj_set_style_bg_color(g_btn_cur[j], lv_color_hex(0x111120), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(g_btn_cur[j], 0),
                                    lv_color_hex(0x555566), 0);
      }
      // Aktiven Button hervorheben
      lv_obj_set_style_bg_color(g_btn_cur[idx], lv_color_hex(0x2A2A40), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(g_btn_cur[idx], 0),
                                  lv_color_hex(0xF7931A), 0);
      update_prices();
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
  }

  // ---------- Separator zwischen Perioden- und Währungsbuttons ----------
  lv_obj_t *lbl_sep = lv_label_create(status_bar);
  lv_label_set_text(lbl_sep, "|");
  lv_obj_set_style_text_color(lbl_sep, lv_color_hex(0x2A2A40), 0);
  lv_obj_set_style_text_font(lbl_sep, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_sep, LV_ALIGN_RIGHT_MID, -192, 0);

  // ---------- Perioden-Buttons in Status Bar ----------
  const char* per_labels[] = {"1H", "1T", "1W", "1M"};
  for (int i = 0; i < NUM_PERIODS; i++) {
    g_btn_period[i] = lv_btn_create(status_bar);
    lv_obj_set_size(g_btn_period[i], 44, 26);
    lv_obj_align(g_btn_period[i], LV_ALIGN_RIGHT_MID, -212 - (3-i) * 50, 0);
    lv_obj_set_style_radius(g_btn_period[i], 4, 0);
    lv_obj_set_style_pad_all(g_btn_period[i], 0, 0);
    bool pactive = (i == (int)g_chart_period);
    lv_obj_set_style_bg_color(g_btn_period[i], pactive ? lv_color_hex(0x2A2A40) : lv_color_hex(0x111120), 0);
    lv_obj_set_style_border_color(g_btn_period[i], lv_color_hex(0x3A3A55), 0);
    lv_obj_set_style_border_width(g_btn_period[i], 1, 0);
    lv_obj_t *plbl = lv_label_create(g_btn_period[i]);
    lv_label_set_text(plbl, per_labels[i]);
    lv_obj_set_style_text_font(plbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(plbl, pactive ? lv_color_hex(0xF7931A) : lv_color_hex(0x555566), 0);
    lv_obj_center(plbl);
    lv_obj_add_event_cb(g_btn_period[i], [](lv_event_t *e) {
      uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
      g_chart_period = idx;
      for (int j = 0; j < NUM_PERIODS; j++) {
        lv_obj_set_style_bg_color(g_btn_period[j], lv_color_hex(0x111120), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(g_btn_period[j], 0), lv_color_hex(0x555566), 0);
      }
      lv_obj_set_style_bg_color(g_btn_period[idx], lv_color_hex(0x2A2A40), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(g_btn_period[idx], 0), lv_color_hex(0xF7931A), 0);
      update_chart_series();
      update_prices();
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
  }
  // 1H ausgeblendet bis API-Endpoint implementiert ist
  lv_obj_add_flag(g_btn_period[0], LV_OBJ_FLAG_HIDDEN);
}

// ============================================================
//  Preise anzeigen (aktualisiert alle Labels)
// ============================================================
void update_prices() {
  Serial.printf("update_prices() g_data_ok=%d cur=%d active=%d period=%d\n",
                g_data_ok, g_cur, (int)g_active_asset, g_chart_period);
  char buf[48];
  char oz_buf[64];
  const char *sym = CUR_SYMBOL[g_cur];
  int c = g_cur;

  // Periodenbasierte Änderung wählen (24h aus API, 1W/1M aus Chart)
  auto get_chg = [&](Asset a) -> float {
    if (g_chart_period == 0) return g_chg_h[a];   // 1H
    if (g_chart_period == 2) return g_chg_w[a];   // 1W
    if (g_chart_period == 3) return g_chg_m[a];   // 1M
    // 1T (period 1): per-currency 24h aus Chart
    if (a == ASSET_BTC)    return g_btc_chg[c];
    if (a == ASSET_GOLD)   return g_gold_chg[c];
    return g_silver_chg[c];
  };

  // Hilfsfunktion: Bottom-Card befüllen
  auto fill_bottom = [&](Asset a,
                          lv_obj_t *icon_lbl, lv_obj_t *price_lbl,
                          lv_obj_t *sub_lbl,  lv_obj_t *chg_lbl) {
    if (!g_data_ok) {
      lv_label_set_text(price_lbl, "Lade...");
      lv_label_set_text(sub_lbl, "");
      lv_label_set_text(chg_lbl, "");
      return;
    }
    float chg = get_chg(a);
    if (a == ASSET_BTC) {
      lv_label_set_text(icon_lbl, "BITCOIN");
      lv_obj_set_style_text_color(icon_lbl, lv_color_hex(0xF7931A), 0);
      fmt_price(buf, sizeof(buf), g_btc[c], sym);
      lv_label_set_text(price_lbl, buf);
      lv_label_set_text(sub_lbl, "");
    } else if (a == ASSET_GOLD) {
      lv_label_set_text(icon_lbl, "Au  GOLD");
      lv_obj_set_style_text_color(icon_lbl, lv_color_hex(0xFFD700), 0);
      fmt_price(buf, sizeof(buf), g_gold[c], sym, 2);
      snprintf(oz_buf, sizeof(oz_buf), "%s / oz", buf);
      lv_label_set_text(price_lbl, oz_buf);
      fmt_price(buf, sizeof(buf), g_gold[c] / TROY_OZ_G, sym, 2);
      snprintf(oz_buf, sizeof(oz_buf), "%s / g", buf);
      lv_label_set_text(sub_lbl, oz_buf);
    } else {
      lv_label_set_text(icon_lbl, "Ag  SILBER");
      lv_obj_set_style_text_color(icon_lbl, lv_color_hex(0xC0C0C0), 0);
      fmt_price(buf, sizeof(buf), g_silver[c], sym, 2);
      snprintf(oz_buf, sizeof(oz_buf), "%s / oz", buf);
      lv_label_set_text(price_lbl, oz_buf);
      fmt_price(buf, sizeof(buf), g_silver[c] / TROY_OZ_G, sym, 2);
      snprintf(oz_buf, sizeof(oz_buf), "%s / g", buf);
      lv_label_set_text(sub_lbl, oz_buf);
    }
    fmt_change(buf, sizeof(buf), chg);
    lv_label_set_text(chg_lbl, buf);
    lv_obj_set_style_text_color(chg_lbl,
      chg >= 0 ? lv_color_hex(0x00C853) : lv_color_hex(0xFF1744), 0);
  };

  // ---- Top Card ----
  float top_chg = 0.0f;
  if (!g_data_ok) {
    lv_label_set_text(lbl_top_price, "Lade...");
    lv_label_set_text(lbl_top_chg, "");
    lv_label_set_text(lbl_top_sub, "");
  } else {
    top_chg = get_chg(g_active_asset);
    if (g_active_asset == ASSET_BTC) {
      lv_label_set_text(lbl_top_icon, "BITCOIN");
      lv_obj_set_style_text_color(lbl_top_icon, lv_color_hex(0xF7931A), 0);
      fmt_price(buf, sizeof(buf), g_btc[c], sym);
      lv_label_set_text(lbl_top_price, buf);
      lv_label_set_text(lbl_top_sub, "");
    } else if (g_active_asset == ASSET_GOLD) {
      lv_label_set_text(lbl_top_icon, "Au  GOLD");
      lv_obj_set_style_text_color(lbl_top_icon, lv_color_hex(0xFFD700), 0);
      fmt_price(buf, sizeof(buf), g_gold[c], sym, 2);
      snprintf(oz_buf, sizeof(oz_buf), "%s / oz", buf);
      lv_label_set_text(lbl_top_price, oz_buf);
      fmt_price(buf, sizeof(buf), g_gold[c] / TROY_OZ_G, sym, 2);
      snprintf(oz_buf, sizeof(oz_buf), "%s / g", buf);
      lv_label_set_text(lbl_top_sub, oz_buf);
    } else {
      lv_label_set_text(lbl_top_icon, "Ag  SILBER");
      lv_obj_set_style_text_color(lbl_top_icon, lv_color_hex(0xC0C0C0), 0);
      fmt_price(buf, sizeof(buf), g_silver[c], sym, 2);
      snprintf(oz_buf, sizeof(oz_buf), "%s / oz", buf);
      lv_label_set_text(lbl_top_price, oz_buf);
      fmt_price(buf, sizeof(buf), g_silver[c] / TROY_OZ_G, sym, 2);
      snprintf(oz_buf, sizeof(oz_buf), "%s / g", buf);
      lv_label_set_text(lbl_top_sub, oz_buf);
    }
    fmt_change(buf, sizeof(buf), top_chg);
    lv_label_set_text(lbl_top_chg, buf);
  }

  if (g_data_ok) {
    lv_color_t chg_color = top_chg >= 0 ? lv_color_hex(0x00C853) : lv_color_hex(0xFF1744);
    lv_obj_set_style_text_color(lbl_top_chg, chg_color, 0);
    if (g_chart_ser) {
      g_chart_ser->color = chg_color;
      lv_obj_invalidate(g_chart);
    }
    update_chart_series();
  }

  // ---- Bottom Cards ----
  fill_bottom(g_b1_asset, lbl_b1_icon, lbl_b1_price, lbl_b1_sub, lbl_b1_chg);
  fill_bottom(g_b2_asset, lbl_b2_icon, lbl_b2_price, lbl_b2_sub, lbl_b2_chg);

  // ---- High/Low im Vollbild ----
  update_fs_highlow();
}

// ============================================================
//  Aktives Asset wechseln (Tap auf Bottom-Card)
// ============================================================
void set_active_asset(Asset a) {
  if (a == g_active_asset) return;

  // Slot tauschen: der Bottom-Slot der dieses Asset hatte, bekommt das alte Top-Asset
  if (g_b1_asset == a) {
    g_b1_asset = g_active_asset;
  } else {
    g_b2_asset = g_active_asset;
  }
  g_active_asset = a;

  // Lazy-Load: Chart für dieses Asset + aktuelle Periode falls noch nicht geladen
  if (g_chart_cnt[a][g_chart_period] == 0) {
    lv_label_set_text(lbl_status, "Lade Chart...");
    lv_timer_handler();
    fetch_chart_asset(a, g_chart_period);
    if (g_data_ok) {
      char sbuf[48];
      snprintf(sbuf, sizeof(sbuf), "Aktualisiert: %s Uhr", g_last_update);
      lv_label_set_text(lbl_status, sbuf);
    }
  }

  update_prices();
}

// ============================================================
//  Vollbild-Modus
// ============================================================
static void calc_high_low(float *out_hi, float *out_lo) {
  uint8_t cnt = g_chart_cnt[g_active_asset][g_chart_period];
  if (cnt == 0) { *out_hi = *out_lo = 0; return; }
  int16_t hi = g_chart_data[g_active_asset][g_chart_period][0];
  int16_t lo = hi;
  for (int i = 1; i < cnt; i++) {
    int16_t v = g_chart_data[g_active_asset][g_chart_period][i];
    if (v > hi) hi = v;
    if (v < lo) lo = v;
  }
  *out_hi = (float)hi * CHART_DIV[g_active_asset];
  *out_lo = (float)lo * CHART_DIV[g_active_asset];
}

void update_fs_highlow() {
  if (!g_fullscreen || !lbl_fs_high || !lbl_fs_low) return;
  float hi, lo;
  calc_high_low(&hi, &lo);
  char buf[32];
  const char *sym = CUR_SYMBOL[g_cur];
  // Chart-Daten sind immer in USD – Umrechnung über Preisverhältnis
  float usd_price = (g_active_asset == ASSET_BTC)  ? g_btc[USD]
                  : (g_active_asset == ASSET_GOLD)  ? g_gold[USD]
                                                    : g_silver[USD];
  float cur_price = (g_active_asset == ASSET_BTC)  ? g_btc[g_cur]
                  : (g_active_asset == ASSET_GOLD)  ? g_gold[g_cur]
                                                    : g_silver[g_cur];
  float fx = (usd_price > 0.0f) ? (cur_price / usd_price) : 1.0f;
  if (hi > 0) {
    fmt_price(buf, sizeof(buf), hi * fx, sym, g_active_asset == ASSET_BTC ? 0 : 2);
    char lbuf[48]; snprintf(lbuf, sizeof(lbuf), "Hi: %s", buf);
    lv_label_set_text(lbl_fs_high, lbuf);
  }
  if (lo > 0) {
    fmt_price(buf, sizeof(buf), lo * fx, sym, g_active_asset == ASSET_BTC ? 0 : 2);
    char lbuf[48]; snprintf(lbuf, sizeof(lbuf), "Lo: %s", buf);
    lv_label_set_text(lbl_fs_low, lbuf);
  }
}

void toggle_fullscreen() {
  if (!g_top_card || !g_b1_card || !g_b2_card) return;
  g_fullscreen = !g_fullscreen;

  if (g_fullscreen) {
    // Bottom Cards ausblenden
    lv_obj_add_flag(g_b1_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_b2_card, LV_OBJ_FLAG_HIDDEN);
    // Top Card + Chart vergrößern (2px Abstand zur Status Bar wie Bottom Cards)
    lv_obj_set_size(g_top_card, SCR_W, 375);
    lv_obj_set_size(g_chart, SCR_W, 375);
    // Font vergrößern + Labels neu positionieren
    lv_obj_set_style_text_font(lbl_top_price, &lv_font_montserrat_72, 0);
    lv_obj_align(lbl_top_icon,  LV_ALIGN_TOP_LEFT,   24,  14);
    lv_obj_align(lbl_top_price, LV_ALIGN_TOP_LEFT,   24,  40);
    lv_obj_align(lbl_top_chg,   LV_ALIGN_TOP_RIGHT, -24,  55);
    lv_obj_align(lbl_top_sub,   LV_ALIGN_TOP_LEFT,   24, 132);
    lv_obj_align(lbl_top_24h,   LV_ALIGN_TOP_RIGHT, -24, 115);
    // High/Low unten anzeigen (wo vorher Buttons waren)
    lv_obj_clear_flag(lbl_fs_high, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lbl_fs_low,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(lbl_fs_high, LV_ALIGN_TOP_LEFT,  24, 310);
    lv_obj_align(lbl_fs_low,  LV_ALIGN_TOP_LEFT, 220, 310);
    // Vollbild-Button: Icon zu X wechseln
    lv_obj_set_pos(g_btn_fullscreen, SCR_W - 44, 340);
    lv_label_set_text(lv_obj_get_child(g_btn_fullscreen, 0), LV_SYMBOL_CLOSE);
    update_fs_highlow();
  } else {
    // Bottom Cards wieder einblenden
    lv_obj_clear_flag(g_b1_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_b2_card, LV_OBJ_FLAG_HIDDEN);
    // Top Card + Chart zurücksetzen
    lv_obj_set_size(g_top_card, SCR_W, 197);
    lv_obj_set_size(g_chart, SCR_W, 197);
    // Font zurücksetzen + Labels zurück auf Normal-Positionen
    lv_obj_set_style_text_font(lbl_top_price, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_top_icon,  LV_ALIGN_TOP_LEFT,   24,  14);
    lv_obj_align(lbl_top_price, LV_ALIGN_TOP_LEFT,   24,  40);
    lv_obj_align(lbl_top_chg,   LV_ALIGN_TOP_RIGHT, -24,  46);
    lv_obj_align(lbl_top_sub,   LV_ALIGN_TOP_LEFT,   24, 102);
    lv_obj_align(lbl_top_24h,   LV_ALIGN_TOP_RIGHT, -24,  88);
    // High/Low ausblenden
    lv_obj_add_flag(lbl_fs_high, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lbl_fs_low,  LV_OBJ_FLAG_HIDDEN);
    // Vollbild-Button: Icon zu + wechseln
    lv_obj_set_pos(g_btn_fullscreen, SCR_W - 44, 162);
    lv_label_set_text(lv_obj_get_child(g_btn_fullscreen, 0), LV_SYMBOL_PLUS);
  }
}

// ============================================================
//  ticker.blitzi.me API abrufen
// ============================================================
bool fetchPrices() {
  if (WiFi.status() != WL_CONNECTED) return false;

  Serial.printf("Heap vor API: %d\n", ESP.getFreeHeap());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, "https://ticker.blitzi.me/prices/all");
  http.setTimeout(10000);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("API Fehler: HTTP %d\n", code);
    http.end(); return false;
  }

  JsonDocument doc;
  auto err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) { Serial.printf("API JSON: %s\n", err.c_str()); return false; }

  JsonObject data = doc["data"].as<JsonObject>();
  g_btc[USD]    = data["BTC_USD"]["price"] | 0.0f;
  g_btc[EUR]    = data["BTC_EUR"]["price"] | 0.0f;
  g_btc[CHF]    = data["BTC_CHF"]["price"] | 0.0f;
  g_gold[USD]   = data["XAU_USD"]["price"] | 0.0f;
  g_gold[EUR]   = data["XAU_EUR"]["price"] | 0.0f;
  g_gold[CHF]   = data["XAU_CHF"]["price"] | 0.0f;
  g_silver[USD] = data["XAG_USD"]["price"] | 0.0f;
  g_silver[EUR] = data["XAG_EUR"]["price"] | 0.0f;
  g_silver[CHF] = data["XAG_CHF"]["price"] | 0.0f;

  Serial.printf("BTC: $%.2f  Gold: $%.2f  Silver: $%.4f\n",
                g_btc[USD], g_gold[USD], g_silver[USD]);

  // 24h Change wird aus der 1d-History berechnet (in fetch_chart_asset period=0)
  return g_btc[USD] > 0;
}

// ============================================================
//  Boot-Screen
// ============================================================
void show_boot_screen() {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A10), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // Bitcoin-Orange Linie oben
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, SCR_W, 6);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  // Flashman Logo (160x160)
  lv_obj_t *img = lv_img_create(scr);
  lv_img_set_src(img, &flashman_logo_img);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, -80);

  // FLASHTICKER Titel
  lv_obj_t *lbl_logo = lv_label_create(scr);
  lv_label_set_text(lbl_logo, "FLASHTICKER");
  lv_obj_set_style_text_color(lbl_logo, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_text_font(lbl_logo, &lv_font_montserrat_48, 0);
  lv_obj_align(lbl_logo, LV_ALIGN_CENTER, 0, 65);

  // Untertitel
  lv_obj_t *lbl_sub = lv_label_create(scr);
  lv_label_set_text(lbl_sub, "BTC  |  GOLD  |  SILBER");
  lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0x555566), 0);
  lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_sub, LV_ALIGN_CENTER, 0, 118);

  // Version
  lv_obj_t *lbl_version = lv_label_create(scr);
  lv_label_set_text(lbl_version, "v" FW_VERSION);
  lv_obj_set_style_text_color(lbl_version, lv_color_hex(0x444455), 0);
  lv_obj_set_style_text_font(lbl_version, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_version, LV_ALIGN_CENTER, 0, 150);

  // Made with love
  lv_obj_t *lbl_ver = lv_label_create(scr);
  lv_label_set_text(lbl_ver, "Made with love by Flashman");
  lv_obj_set_style_text_color(lbl_ver, lv_color_hex(0x333344), 0);
  lv_obj_set_style_text_font(lbl_ver, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_ver, LV_ALIGN_BOTTOM_MID, 0, -20);

  lv_scr_load(scr);
  lv_timer_handler();
  delay(2500);
}

// ============================================================
//  WiFi-Config-Screen (AP-Modus)
// ============================================================
void show_wifi_screen() {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A10), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // Header
  lv_obj_t *hdr = lv_obj_create(scr);
  lv_obj_set_pos(hdr, 0, 0);
  lv_obj_set_size(hdr, SCR_W, 70);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0F0F1E), 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_title = lv_label_create(hdr);
  lv_label_set_text(lbl_title, "WLAN EINRICHTEN");
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_28, 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

  // Anleitung Box (links)
  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_pos(box, 30, 90);
  lv_obj_set_size(box, 470, 310);
  lv_obj_set_style_bg_color(box, lv_color_hex(0x0F0F1E), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x1E1E3A), 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_radius(box, 8, 0);
  lv_obj_set_style_pad_all(box, 24, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);

  // Schritt 1
  lv_obj_t *s1 = lv_label_create(box);
  lv_label_set_text(s1, "1.  Mit diesem WLAN verbinden:");
  lv_obj_set_style_text_color(s1, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(s1, &lv_font_montserrat_18, 0);
  lv_obj_align(s1, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *ssid = lv_label_create(box);
  lv_label_set_text(ssid, "FLASHTICKER");
  lv_obj_set_style_text_color(ssid, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(ssid, &lv_font_montserrat_28, 0);
  lv_obj_align(ssid, LV_ALIGN_TOP_LEFT, 28, 32);

  lv_obj_t *pw_lbl = lv_label_create(box);
  lv_label_set_text(pw_lbl, "Passwort:  flashticker");
  lv_obj_set_style_text_color(pw_lbl, lv_color_hex(0x666677), 0);
  lv_obj_set_style_text_font(pw_lbl, &lv_font_montserrat_18, 0);
  lv_obj_align(pw_lbl, LV_ALIGN_TOP_LEFT, 28, 74);

  // Schritt 2
  lv_obj_t *s2 = lv_label_create(box);
  lv_label_set_text(s2, "2.  Browser aufrufen:");
  lv_obj_set_style_text_color(s2, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(s2, &lv_font_montserrat_18, 0);
  lv_obj_align(s2, LV_ALIGN_TOP_LEFT, 0, 140);

  lv_obj_t *ip = lv_label_create(box);
  lv_label_set_text(ip, "192.168.4.1");
  lv_obj_set_style_text_color(ip, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_text_font(ip, &lv_font_montserrat_28, 0);
  lv_obj_align(ip, LV_ALIGN_TOP_LEFT, 28, 174);

  // QR-Code rechts (WiFi-Credentials zum Scannen)
  lv_obj_t *qr = lv_qrcode_create(scr, 200, lv_color_hex(0x0A0A10), lv_color_hex(0xFFFFFF));
  const char *wifi_qr = "WIFI:S:FLASHTICKER;T:WPA;P:flashticker;;";
  lv_qrcode_update(qr, wifi_qr, strlen(wifi_qr));
  lv_obj_set_pos(qr, 560, 110);

  lv_obj_t *qr_lbl = lv_label_create(scr);
  lv_label_set_text(qr_lbl, "WiFi scannen");
  lv_obj_set_style_text_color(qr_lbl, lv_color_hex(0x666677), 0);
  lv_obj_set_style_text_font(qr_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align_to(qr_lbl, qr, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

  // Warte-Status unten
  lv_obj_t *lbl_wait = lv_label_create(scr);
  lv_label_set_text(lbl_wait, "Warte auf WLAN-Verbindung...");
  lv_obj_set_style_text_color(lbl_wait, lv_color_hex(0x444455), 0);
  lv_obj_set_style_text_font(lbl_wait, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_wait, LV_ALIGN_BOTTOM_MID, 0, -20);

  lv_scr_load(scr);
  lv_timer_handler();
}

// ============================================================
//  Loading-Screen (wird über Haupt-UI gelegt während Daten laden)
// ============================================================
void show_loading_screen() {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A10), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // Orange Linie oben (wie Bootscreen)
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, SCR_W, 6);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  // Logo
  lv_obj_t *img = lv_img_create(scr);
  lv_img_set_src(img, &flashman_logo_img);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, -90);

  // Titel
  lv_obj_t *lbl_logo = lv_label_create(scr);
  lv_label_set_text(lbl_logo, "FLASHTICKER");
  lv_obj_set_style_text_color(lbl_logo, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_text_font(lbl_logo, &lv_font_montserrat_48, 0);
  lv_obj_align(lbl_logo, LV_ALIGN_CENTER, 0, 55);

  // Status-Meldung (wird via update_loading gesetzt)
  g_load_msg = lv_label_create(scr);
  lv_label_set_text(g_load_msg, "Starte...");
  lv_obj_set_style_text_color(g_load_msg, lv_color_hex(0x777788), 0);
  lv_obj_set_style_text_font(g_load_msg, &lv_font_montserrat_18, 0);
  lv_obj_align(g_load_msg, LV_ALIGN_CENTER, 0, 108);

  // Progress-Bar (lv_bar – LVGL-native, füllt sich korrekt)
  g_load_fill = lv_bar_create(scr);
  lv_obj_set_pos(g_load_fill, 100, 385);
  lv_obj_set_size(g_load_fill, 600, 14);
  lv_bar_set_range(g_load_fill, 0, 100);
  lv_bar_set_value(g_load_fill, 0, LV_ANIM_OFF);
  // Hintergrund (Track)
  lv_obj_set_style_bg_color(g_load_fill, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_load_fill, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(g_load_fill, 7, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_load_fill, 0, LV_PART_MAIN);
  // Füllfarbe (Indicator)
  lv_obj_set_style_bg_color(g_load_fill, lv_color_hex(0xF7931A), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(g_load_fill, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_load_fill, 7, LV_PART_INDICATOR);

  // Prozent-Anzeige (unterhalb des Balkens)
  g_load_pct = lv_label_create(scr);
  lv_label_set_text(g_load_pct, "0%");
  lv_obj_set_style_text_color(g_load_pct, lv_color_hex(0x777788), 0);
  lv_obj_set_style_text_font(g_load_pct, &lv_font_montserrat_20, 0);
  lv_obj_align(g_load_pct, LV_ALIGN_CENTER, 0, 175);

  lv_scr_load(scr);
  lv_timer_handler();
}

void update_loading(int pct, const char *msg) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  if (g_load_fill) lv_bar_set_value(g_load_fill, pct, LV_ANIM_OFF);
  if (g_load_msg)  lv_label_set_text(g_load_msg, msg);
  if (g_load_pct) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(g_load_pct, buf);
  }
  lv_timer_handler();
}

// ============================================================
//  NVS Settings: laden / speichern
// ============================================================
void load_settings() {
  prefs.begin("flashticker", true);
  g_def_asset      = prefs.getUChar("def_asset",    0);
  g_def_currency   = prefs.getUChar("def_currency", 0);
  g_def_period     = prefs.getUChar("def_period",   1);
  g_def_fullscreen = prefs.getBool ("def_fs",       false);
  prefs.end();
  // Bounds-Check
  if (g_def_asset    > 2) g_def_asset    = 0;
  if (g_def_currency > 2) g_def_currency = 0;
  if (g_def_period == 0 || g_def_period > 3) g_def_period = 1;
  // Auf Runtime-Globals anwenden
  g_active_asset = (Asset)g_def_asset;
  g_cur          = (Currency)g_def_currency;
  g_chart_period = g_def_period;
  // Bottom-Cards: die anderen zwei Assets in fixer Reihenfolge
  const Asset all3[] = {ASSET_BTC, ASSET_GOLD, ASSET_SILVER};
  int bi = 0;
  Asset bottom[2];
  for (int i = 0; i < 3; i++) {
    if (all3[i] != g_active_asset) bottom[bi++] = all3[i];
  }
  g_b1_asset = bottom[0];
  g_b2_asset = bottom[1];
}

void save_settings() {
  prefs.begin("flashticker", false);
  prefs.putUChar("def_asset",    g_def_asset);
  prefs.putUChar("def_currency", g_def_currency);
  prefs.putUChar("def_period",   g_def_period);
  prefs.putBool ("def_fs",       g_def_fullscreen);
  prefs.end();
}

// ============================================================
//  Einstellungen-Screen
// ============================================================
static lv_obj_t *s_asset_btns[3];
static lv_obj_t *s_cur_btns[3];
static lv_obj_t *s_period_btns[4];
static lv_obj_t *s_fs_btns[2];

static void settings_highlight(lv_obj_t **btns, int count, int active) {
  for (int i = 0; i < count; i++) {
    bool on = (i == active);
    lv_obj_set_style_bg_color(btns[i],
      on ? lv_color_hex(0x2A2A40) : lv_color_hex(0x111120), 0);
    lv_obj_set_style_border_color(btns[i],
      on ? lv_color_hex(0xF7931A) : lv_color_hex(0x3A3A55), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(btns[i], 0),
      on ? lv_color_hex(0xF7931A) : lv_color_hex(0x555566), 0);
  }
}

void show_settings_screen(lv_obj_t *prev_scr) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A10), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // Header
  lv_obj_t *hdr = make_card(scr, 0, 0, SCR_W, 60);

  lv_obj_t *btn_back = lv_btn_create(hdr);
  lv_obj_set_size(btn_back, 110, 40);
  lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 10, 0);
  lv_obj_set_style_radius(btn_back, 6, 0);
  lv_obj_set_style_pad_all(btn_back, 0, 0);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_border_width(btn_back, 0, 0);
  lv_obj_t *lbl_b = lv_label_create(btn_back);
  lv_label_set_text(lbl_b, LV_SYMBOL_LEFT "  Zurueck");
  lv_obj_set_style_text_color(lbl_b, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_text_font(lbl_b, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl_b);
  lv_obj_add_event_cb(btn_back, [](lv_event_t *e) {
    lv_obj_t *prev = (lv_obj_t*)lv_event_get_user_data(e);
    lv_scr_load(prev);
  }, LV_EVENT_CLICKED, prev_scr);

  lv_obj_t *lbl_title = lv_label_create(hdr);
  lv_label_set_text(lbl_title, "EINSTELLUNGEN");
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_28, 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *lbl_ver = lv_label_create(hdr);
  lv_label_set_text(lbl_ver, "v" FW_VERSION);
  lv_obj_set_style_text_color(lbl_ver, lv_color_hex(0x444455), 0);
  lv_obj_set_style_text_font(lbl_ver, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_ver, LV_ALIGN_RIGHT_MID, -16, 0);

  // Helper: Settings-Button erstellen
  auto make_sbtn = [](lv_obj_t *parent, const char *text,
                      lv_align_t align, int x_ofs, bool active) -> lv_obj_t* {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 96, 46);
    lv_obj_align(btn, align, x_ofs, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, active ? lv_color_hex(0x2A2A40) : lv_color_hex(0x111120), 0);
    lv_obj_set_style_border_color(btn, active ? lv_color_hex(0xF7931A) : lv_color_hex(0x3A3A55), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, active ? lv_color_hex(0xF7931A) : lv_color_hex(0x555566), 0);
    lv_obj_center(lbl);
    return btn;
  };

  // Zeile 0: Standard Asset
  lv_obj_t *row0 = make_card(scr, 40, 75, 720, 82, 0x0F0F1E, true, false);
  lv_obj_set_style_radius(row0, 8, 0);
  lv_obj_t *lbl0 = lv_label_create(row0);
  lv_label_set_text(lbl0, "Standard Asset");
  lv_obj_set_style_text_color(lbl0, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(lbl0, &lv_font_montserrat_18, 0);
  lv_obj_align(lbl0, LV_ALIGN_LEFT_MID, 20, 0);
  const char *asset_labels[] = {"BTC", "GOLD", "SILBER"};
  for (int i = 0; i < 3; i++) {
    s_asset_btns[i] = make_sbtn(row0, asset_labels[i],
      LV_ALIGN_RIGHT_MID, -16 - (2-i)*106, i == (int)g_def_asset);
    lv_obj_add_event_cb(s_asset_btns[i], [](lv_event_t *e) {
      uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
      g_def_asset = idx;
      save_settings();
      settings_highlight(s_asset_btns, 3, idx);
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
  }

  // Zeile 1: Waehrung
  lv_obj_t *row1 = make_card(scr, 40, 170, 720, 82, 0x0F0F1E, true, false);
  lv_obj_set_style_radius(row1, 8, 0);
  lv_obj_t *lbl1 = lv_label_create(row1);
  lv_label_set_text(lbl1, "Waehrung");
  lv_obj_set_style_text_color(lbl1, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(lbl1, &lv_font_montserrat_18, 0);
  lv_obj_align(lbl1, LV_ALIGN_LEFT_MID, 20, 0);
  const char *cur_labels[] = {"EUR", "CHF", "USD"};
  for (int i = 0; i < 3; i++) {
    s_cur_btns[i] = make_sbtn(row1, cur_labels[i],
      LV_ALIGN_RIGHT_MID, -16 - (2-i)*106, i == (int)g_def_currency);
    lv_obj_add_event_cb(s_cur_btns[i], [](lv_event_t *e) {
      uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
      g_def_currency = idx;
      save_settings();
      settings_highlight(s_cur_btns, 3, idx);
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
  }

  // Zeile 2: Chart Periode
  lv_obj_t *row2 = make_card(scr, 40, 265, 720, 82, 0x0F0F1E, true, false);
  lv_obj_set_style_radius(row2, 8, 0);
  lv_obj_t *lbl2 = lv_label_create(row2);
  lv_label_set_text(lbl2, "Chart Periode");
  lv_obj_set_style_text_color(lbl2, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(lbl2, &lv_font_montserrat_18, 0);
  lv_obj_align(lbl2, LV_ALIGN_LEFT_MID, 20, 0);
  const char *period_labels[] = {"1H", "1T", "1W", "1M"};
  for (int i = 0; i < 4; i++) {
    s_period_btns[i] = make_sbtn(row2, period_labels[i],
      LV_ALIGN_RIGHT_MID, -16 - (3-i)*106, i == (int)g_def_period);
    lv_obj_add_event_cb(s_period_btns[i], [](lv_event_t *e) {
      uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
      g_def_period = idx;
      save_settings();
      settings_highlight(s_period_btns, 4, idx);
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
  }
  // 1H ausgeblendet bis API-Endpoint implementiert ist
  lv_obj_add_flag(s_period_btns[0], LV_OBJ_FLAG_HIDDEN);

  // Zeile 3: Vollbild Start
  lv_obj_t *row3 = make_card(scr, 40, 360, 720, 82, 0x0F0F1E, true, false);
  lv_obj_set_style_radius(row3, 8, 0);
  lv_obj_t *lbl3 = lv_label_create(row3);
  lv_label_set_text(lbl3, "Vollbild Start");
  lv_obj_set_style_text_color(lbl3, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(lbl3, &lv_font_montserrat_18, 0);
  lv_obj_align(lbl3, LV_ALIGN_LEFT_MID, 20, 0);
  const char *fs_labels[] = {"AN", "AUS"};
  for (int i = 0; i < 2; i++) {
    s_fs_btns[i] = make_sbtn(row3, fs_labels[i],
      LV_ALIGN_RIGHT_MID, -16 - (1-i)*106, i == (g_def_fullscreen ? 0 : 1));
    lv_obj_add_event_cb(s_fs_btns[i], [](lv_event_t *e) {
      uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
      g_def_fullscreen = (idx == 0);
      save_settings();
      settings_highlight(s_fs_btns, 2, idx);
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
  }

  // Hinweis unten
  lv_obj_t *lbl_note = lv_label_create(scr);
  lv_label_set_text(lbl_note, LV_SYMBOL_SAVE "  Einstellungen werden sofort gespeichert und gelten ab dem naechsten Start.");
  lv_obj_set_style_text_color(lbl_note, lv_color_hex(0x444455), 0);
  lv_obj_set_style_text_font(lbl_note, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_note, LV_ALIGN_BOTTOM_MID, 0, -16);

  lv_scr_load(scr);
}

// ============================================================
//  Chart: Daten der aktuellen Währung in die Serie laden
// ============================================================
void update_chart_series() {
  if (!g_chart || !g_chart_ser) return;
  uint8_t cnt = g_chart_cnt[g_active_asset][g_chart_period];
  if (cnt == 0) {
    lv_chart_set_point_count(g_chart, 0);
    lv_chart_refresh(g_chart);
    return;
  }
  int16_t *data = g_chart_data[g_active_asset][g_chart_period];
  lv_chart_set_point_count(g_chart, cnt);
  lv_chart_set_ext_y_array(g_chart, g_chart_ser, data);

  // Y-Range: Min/Max mit 10% Puffer
  int16_t mn = data[0], mx = data[0];
  for (int i = 1; i < cnt; i++) {
    if (data[i] < mn) mn = data[i];
    if (data[i] > mx) mx = data[i];
  }
  int16_t pad = (mx - mn) / 10;
  if (pad < 1) pad = 1;
  lv_chart_set_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, mn - pad, mx + pad);
  lv_chart_refresh(g_chart);
}

// ============================================================
//  Chart: Ein Asset / eine Periode laden
// ============================================================
void fetch_chart_asset(Asset a, uint8_t period) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  char url[128];

  const char *asset_str  = (a == ASSET_BTC) ? "BTC" : (a == ASSET_GOLD) ? "XAU" : "XAG";
  const char *period_str = (period == 1) ? "1d" : (period == 2) ? "1w" : (period == 3) ? "1m" : "1h";

  snprintf(url, sizeof(url), "https://ticker.blitzi.me/history/%s/USD/%s",
           asset_str, period_str);

  http.begin(client, url);
  http.setTimeout(10000);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("%s Chart P%d HTTP %d\n", asset_str, period, code);
    http.end(); return;
  }

  JsonDocument doc;
  auto err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) { Serial.printf("%s JSON err: %s\n", asset_str, err.c_str()); return; }

  JsonArray prices = doc["data"].as<JsonArray>();
  int total = prices.size();
  if (total == 0) return;

  int n = 0;
  int step = max(1, total / (int)CHART_MAX_PTS);
  for (int i = 0; i < total && n < CHART_MAX_PTS; i += step) {
    float price = prices[i]["price"] | 0.0f;
    g_chart_data[a][period][n++] = (int16_t)(price / CHART_DIV[a]);
  }
  g_chart_cnt[a][period] = n;
  Serial.printf("%s Chart P%d: %d Punkte\n", asset_str, period, n);

  // Änderung aus Chart berechnen (1T → 24h-Change, 1W/1M → Periodenänderung)
  if (n >= 2) {
    int16_t first = g_chart_data[a][period][0];
    int16_t last  = g_chart_data[a][period][n - 1];
    float chg = (first != 0) ? ((float)(last - first) / first * 100.0f) : 0.0f;
    if (period == 0) {
      g_chg_h[a] = chg;  // 1H Änderung
    } else if (period == 1) {
      // 1T: 24h-Change in alle Währungsslots schreiben
      if (a == ASSET_BTC)    { g_btc_chg[EUR]    = g_btc_chg[CHF]    = g_btc_chg[USD]    = chg; }
      if (a == ASSET_GOLD)   { g_gold_chg[EUR]   = g_gold_chg[CHF]   = g_gold_chg[USD]   = chg; }
      if (a == ASSET_SILVER) { g_silver_chg[EUR] = g_silver_chg[CHF] = g_silver_chg[USD] = chg; }
    } else if (period == 2) {
      g_chg_w[a] = chg;  // 1W Änderung
    } else {
      g_chg_m[a] = chg;  // 1M Änderung
    }
    Serial.printf("%s Änderung P%d: %.2f%%\n", asset_str, period, chg);
  }
}

// ============================================================
//  Chart: Alle 3 Assets für eine Periode laden
// ============================================================
void fetch_all_charts(uint8_t period) {
  lv_label_set_text(lbl_status, "Lade Charts...");
  lv_timer_handler();
  for (int a = 0; a < NUM_ASSETS; a++) {
    fetch_chart_asset((Asset)a, period);
    lv_timer_handler();
  }
  update_chart_series();
  update_prices();
  // Status zurücksetzen
  if (g_data_ok) {
    char sbuf[48];
    snprintf(sbuf, sizeof(sbuf), "Aktualisiert: %s Uhr", g_last_update);
    lv_label_set_text(lbl_status, sbuf);
  }
}

// ============================================================
//  Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== FLASHTICKER 7\" ===");

  // GPIO-Init (laut Elecrow offiziell für CrowPanel 5"/7")
  for (int pin : {38, 17, 18, 42}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  // Touch-Reset via PCA9557
  Wire.begin(19, 20);
  Out.reset();
  Out.setMode(IO_OUTPUT);
  Out.setState(IO0, IO_LOW);
  Out.setState(IO1, IO_LOW);
  delay(20);
  Out.setState(IO0, IO_HIGH);
  delay(100);
  Out.setMode(IO1, IO_INPUT);

  // Display
  tft.init();
  tft.initDMA();
  tft.startWrite();
  tft.fillScreen(TFT_BLACK);
  tft.endWrite();
  tft.setBrightness(255);
  Serial.println("Display OK");

  // Draw-Buffer in DMA-fähigem internem SRAM (wie im 5" Beispiel)
  size_t buf_size = SCR_W * LV_BUF_LINES * sizeof(lv_color_t);
  buf1 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  buf2 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!buf1 || !buf2) {
    Serial.println("DMA Alloc FAILED!");
    while(1) delay(1000);
  }
  Serial.printf("DMA Buffer OK: 2x%d bytes, Heap frei: %d\n", buf_size, ESP.getFreeHeap());

  // LVGL
  Serial.println("lv_init...");     Serial.flush();
  lv_init();
  Serial.println("draw_buf_init..."); Serial.flush();
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCR_W * LV_BUF_LINES);

  static lv_disp_drv_t drv;
  lv_disp_drv_init(&drv);
  drv.hor_res      = SCR_W;
  drv.ver_res      = SCR_H;
  drv.flush_cb = lv_flush_cb;
  drv.draw_buf = &draw_buf;
  Serial.println("disp_register..."); Serial.flush();
  lv_disp_drv_register(&drv);

  static lv_indev_drv_t idrv;
  lv_indev_drv_init(&idrv);
  idrv.type    = LV_INDEV_TYPE_POINTER;
  idrv.read_cb = lv_touch_cb;
  lv_indev_drv_register(&idrv);

  // NVS-Settings laden (vor Boot-Screen damit Defaults korrekt gesetzt sind)
  load_settings();
  Serial.printf("Settings: asset=%d cur=%d period=%d fs=%d\n",
                g_def_asset, g_def_currency, g_def_period, g_def_fullscreen);

  // Boot-Screen anzeigen
  show_boot_screen();

  // WiFiManager – bei AP-Modus WiFi-Config-Screen zeigen (kein Timeout)
  WiFiManager wm;
  wm.setAPCallback([](WiFiManager *wm) {
    Serial.println("AP-Modus aktiv – zeige WiFi-Screen");
    show_wifi_screen();
  });
  wm.autoConnect("FLASHTICKER", "flashticker");

  // Screen leeren bevor Haupt-UI aufgebaut wird (verhindert WiFi-Screen-Artefakte)
  lv_obj_t *blank = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(blank, lv_color_hex(0x0A0A10), 0);
  lv_obj_set_style_bg_opa(blank, LV_OPA_COVER, 0);
  lv_scr_load(blank);
  lv_timer_handler();

  // Haupt-UI im Hintergrund aufbauen (wird erst nach dem Laden sichtbar)
  Serial.println("create_ui...");
  create_ui();  // setzt g_main_scr intern, kein lv_scr_load
  lv_timer_handler();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WLAN: %s\n", WiFi.SSID().c_str());

    // Loading-Screen anzeigen (Hauptscreen im Hintergrund)
    show_loading_screen();

    // DNS-Wartezeit
    update_loading(5, "WLAN OK - warte auf DNS...");
    delay(2000);
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
                IPAddress(8,8,8,8));
    delay(500);

    // NTP sync
    update_loading(12, "Uhrzeit synchronisieren...");
    ntpClient.setTimeOffset(7200);
    ntpClient.begin();
    ntpClient.forceUpdate();

    // Kurse laden (bis zu 3 Versuche)
    update_loading(20, "Lade Kurse...");
    for (int attempt = 1; attempt <= 3; attempt++) {
      Serial.printf("API Versuch %d/3...\n", attempt);
      if (fetchPrices()) {
        g_data_ok = true;
        g_last_fetch = millis();
        snprintf(g_last_update, sizeof(g_last_update),
                 "%02d:%02d", ntpClient.getHours(), ntpClient.getMinutes());
        Serial.println("Preise geladen!");
        break;
      }
      if (attempt < 3) {
        Serial.println("Retry in 3s...");
        delay(3000);
      }
    }

    // Charts laden: 9 Fetches (3 Assets × 3 Perioden), Fortschritt 25% → 95%
    if (g_data_ok) {
      const int total = NUM_ASSETS * NUM_PERIODS;
      int done = 0;
      for (uint8_t period = 0; period < NUM_PERIODS; period++) {
        if (period == 0) { done += NUM_ASSETS; continue; }  // 1H: API noch nicht implementiert
        for (int a = 0; a < NUM_ASSETS; a++) {
          char msg[40];
          snprintf(msg, sizeof(msg), "Lade Charts: %d / %d", done + 1, total);
          update_loading(25 + (done * 70) / total, msg);
          fetch_chart_asset((Asset)a, period);
          lv_timer_handler();
          done++;
        }
      }
    }

    // Fertig – kurz anzeigen, dann Hauptscreen
    update_loading(100, "Bereit!");
    lv_timer_handler();
    delay(700);

    // Vollbild-Default anwenden BEVOR wir zurückwechseln
    if (g_def_fullscreen) toggle_fullscreen();

    lv_scr_load(g_main_scr);
    lv_timer_handler();

    // Hauptscreen mit geladenen Daten befüllen
    update_prices();
    update_chart_series();
    char sbuf[48];
    if (g_data_ok)
      snprintf(sbuf, sizeof(sbuf), "Aktualisiert: %s Uhr", g_last_update);
    else
      snprintf(sbuf, sizeof(sbuf), "Kein API-Zugriff - naechster Versuch in 5 Min.");
    lv_label_set_text(lbl_status, sbuf);
    lv_obj_invalidate(lv_scr_act());
    lv_timer_handler();
    delay(50);
    lv_timer_handler();
  } else {
    lv_label_set_text(lbl_status, "Kein WLAN - Bitte neu starten");
    lv_timer_handler();
  }

  Serial.println("Setup fertig.");
}

// ============================================================
//  Loop
// ============================================================
void loop() {
  lv_timer_handler();

  // Zeit aktualisieren (jede Sekunde)
  static unsigned long last_time_update = 0;
  if (millis() - last_time_update >= 1000) {
    last_time_update = millis();

    if (WiFi.status() == WL_CONNECTED) {
      ntpClient.update();

      char tbuf[12];
      snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d",
               ntpClient.getHours(),
               ntpClient.getMinutes(),
               ntpClient.getSeconds());

      // Datum – vollständig: "Sonntag, 05. April 2026"
      const char* days[]   = {"Sonntag","Montag","Dienstag","Mittwoch",
                               "Donnerstag","Freitag","Samstag"};
      const char* months[] = {"","Januar","Februar","Maerz","April","Mai","Juni",
                               "Juli","August","September","Oktober","November","Dezember"};
      time_t raw = ntpClient.getEpochTime();
      struct tm *t = gmtime(&raw);
      char dbuf2[40];
      snprintf(dbuf2, sizeof(dbuf2), "%s, %02d. %s %04d",
               days[t->tm_wday], t->tm_mday, months[t->tm_mon+1], t->tm_year+1900);
      lv_label_set_text(lbl_date, dbuf2);

      lv_label_set_text(lbl_time, tbuf);
    }
  }

  // Kurse alle 5 Minuten aktualisieren
  if (g_data_ok && millis() - g_last_fetch >= FETCH_EVERY_MS) {
    lv_label_set_text(lbl_status, "Aktualisiere Kurse...");
    lv_timer_handler();
    if (fetchPrices()) {
      g_last_fetch = millis();
      snprintf(g_last_update, sizeof(g_last_update),
               "%02d:%02d", ntpClient.getHours(), ntpClient.getMinutes());
      for (uint8_t p = 0; p < NUM_PERIODS; p++) fetch_all_charts(p);  // alle Perioden neu laden
    }
  }

  // Status-Text einmal setzen wenn Daten geladen wurden
  static bool status_shown = false;
  if (g_data_ok && !status_shown) {
    static char sbuf[48];
    snprintf(sbuf, sizeof(sbuf), "Aktualisiert: %s Uhr", g_last_update);
    lv_label_set_text(lbl_status, sbuf);
    status_shown = true;
  }
  if (g_data_ok && millis() - g_last_fetch < 2000) {
    // Kurz nach Refresh Status aktualisieren
    static char sbuf[48];
    snprintf(sbuf, sizeof(sbuf), "Aktualisiert: %s Uhr", g_last_update);
    lv_label_set_text(lbl_status, sbuf);
  }

  delay(5);
}
