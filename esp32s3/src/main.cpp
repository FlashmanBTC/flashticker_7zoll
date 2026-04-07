// ============================================================
//  FLASHTICKER 7" – BTC · Gold · Silber Preisticker
//  ESP32-S3 · Elecrow CrowPanel 7.0" (800×480)
//  LovyanGFX + LVGL 8.3 · WiFiManager · CoinGecko
// ============================================================

#define LGFX_USE_V1
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
#define NUM_PERIODS    3
// Skalierungsdivisor pro Asset damit Werte in int16 passen (BTC >32767)
static const int CHART_DIV[NUM_ASSETS] = { 10, 1, 1 };
int16_t g_chart_data[NUM_ASSETS][NUM_PERIODS][CHART_MAX_PTS];
uint8_t g_chart_cnt[NUM_ASSETS][NUM_PERIODS];
uint8_t g_chart_period = 0;  // 0=1T, 1=1W, 2=1M

// Periodenbasierte Änderung (aus Chart berechnet)
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

// ============================================================
//  LVGL UI – Referenzen
// ============================================================
lv_obj_t *lbl_time, *lbl_date;
lv_obj_t *lbl_status;
lv_obj_t *g_chart;
lv_chart_series_t *g_chart_ser;
lv_obj_t *g_btn_period[3];
lv_obj_t *g_btn_cur[3];

// Top-Card (dynamisch)
lv_obj_t *lbl_top_icon, *lbl_top_price, *lbl_top_chg, *lbl_top_sub, *lbl_top_24h;

// Bottom-Cards (dynamisch)
lv_obj_t *lbl_b1_icon, *lbl_b1_price, *lbl_b1_sub, *lbl_b1_chg;
lv_obj_t *lbl_b2_icon, *lbl_b2_price, *lbl_b2_sub, *lbl_b2_chg;

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
  lv_obj_t *scr = lv_scr_act();
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

  // ---------- TOP CARD (y=61, h=197) – dynamisches Asset ----------
  lv_obj_t *top_card = make_card(scr, 0, 61, SCR_W, 197);

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

  // ---- Period-Buttons 1T / 1W / 1M ----
  const char* period_labels[] = {"1T", "1W", "1M"};
  for (int i = 0; i < 3; i++) {
    g_btn_period[i] = lv_btn_create(top_card);
    lv_obj_set_size(g_btn_period[i], 44, 24);
    lv_obj_set_pos(g_btn_period[i], 24 + i * 50, 162);
    lv_obj_set_style_radius(g_btn_period[i], 4, 0);
    lv_obj_set_style_pad_all(g_btn_period[i], 0, 0);
    lv_obj_set_style_bg_color(g_btn_period[i], i == 0 ? lv_color_hex(0x2A2A40) : lv_color_hex(0x1A1A28), 0);
    lv_obj_set_style_border_color(g_btn_period[i], lv_color_hex(0x3A3A55), 0);
    lv_obj_set_style_border_width(g_btn_period[i], 1, 0);
    lv_obj_t *lbl = lv_label_create(g_btn_period[i]);
    lv_label_set_text(lbl, period_labels[i]);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, i == 0 ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x666677), 0);
    lv_obj_center(lbl);
    int period_idx = i;
    lv_obj_add_event_cb(g_btn_period[i], [](lv_event_t *e) {
      lv_obj_t *btn = lv_event_get_target(e);
      uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
      g_chart_period = idx;
      // Alle Buttons zurücksetzen
      for (int j = 0; j < 3; j++) {
        lv_obj_set_style_bg_color(g_btn_period[j], lv_color_hex(0x1A1A28), 0);
        lv_obj_t *lbl = lv_obj_get_child(g_btn_period[j], 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x666677), 0);
      }
      // Aktiven Button hervorheben
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A40), 0);
      lv_obj_t *lbl = lv_obj_get_child(btn, 0);
      lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
      // Perioden-Label aktualisieren
      const char *period_names[] = {"24h", "1W", "1M"};
      lv_label_set_text(lbl_top_24h, period_names[idx]);
      // Nur aktives Asset laden – schnell, kein Lag
      lv_label_set_text(lbl_status, "Lade Chart...");
      lv_timer_handler();
      fetch_chart_asset(g_active_asset, idx);
      update_chart_series();
      update_prices();
      if (g_data_ok) {
        char sbuf[48];
        snprintf(sbuf, sizeof(sbuf), "Aktualisiert: %s Uhr", g_last_update);
        lv_label_set_text(lbl_status, sbuf);
      }
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)period_idx);
  }

  // ---- Top Card Labels (dynamisch) ----
  lbl_top_icon = lv_label_create(top_card);
  lv_label_set_text(lbl_top_icon, "BITCOIN");
  lv_obj_set_style_text_color(lbl_top_icon, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_text_font(lbl_top_icon, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_top_icon, LV_ALIGN_TOP_LEFT, 24, 18);

  lbl_top_price = lv_label_create(top_card);
  lv_label_set_text(lbl_top_price, "---,---");
  lv_obj_set_style_text_color(lbl_top_price, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(lbl_top_price, &lv_font_montserrat_48, 0);
  lv_obj_align(lbl_top_price, LV_ALIGN_LEFT_MID, 24, 0);

  lbl_top_chg = lv_label_create(top_card);
  lv_label_set_text(lbl_top_chg, "---");
  lv_obj_set_style_text_color(lbl_top_chg, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(lbl_top_chg, &lv_font_montserrat_32, 0);
  lv_obj_align(lbl_top_chg, LV_ALIGN_RIGHT_MID, -24, -10);

  lbl_top_sub = lv_label_create(top_card);
  lv_label_set_text(lbl_top_sub, "");
  lv_obj_set_style_text_color(lbl_top_sub, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(lbl_top_sub, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_top_sub, LV_ALIGN_LEFT_MID, 24, 50);

  lbl_top_24h = lv_label_create(top_card);
  lv_label_set_text(lbl_top_24h, "24h");
  lv_obj_set_style_text_color(lbl_top_24h, lv_color_hex(0x555566), 0);
  lv_obj_set_style_text_font(lbl_top_24h, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_top_24h, LV_ALIGN_RIGHT_MID, -24, 20);

  // ---------- BOTTOM CARDS (y=260, h=176) – antippen wechselt Asset nach oben ----------
  lv_obj_t *b1_card = make_card(scr, 0, 260, 399, 176, 0x0F0F1E, true, true);
  lv_obj_t *b2_card = make_card(scr, 401, 260, 399, 176, 0x0F0F1E, true, true);

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
    if (g_chart_period == 1) return g_chg_w[a];
    if (g_chart_period == 2) return g_chg_m[a];
    // 1T: per-currency 24h Wert aus API
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
//  CoinGecko API abrufen
// ============================================================
bool fetchPrices() {
  if (WiFi.status() != WL_CONNECTED) return false;

  Serial.printf("Heap vor API: %d\n", ESP.getFreeHeap());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  // ---- Binance: BTC/USDT Preis ----
  http.begin(client, "https://api.binance.com/api/v3/ticker/24hr?symbol=BTCUSDT");
  http.setTimeout(15000);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("Binance Fehler: HTTP %d\n", code);
    http.end(); return false;
  }
  {
    JsonDocument doc;
    auto err = deserializeJson(doc, http.getString());
    http.end();
    if (err) { Serial.printf("Binance JSON: %s\n", err.c_str()); return false; }
    float btc_usd      = atof(doc["lastPrice"]    | "0");
    float btc_chg_pct  = atof(doc["priceChangePercent"] | "0");
    // BTC in USD – EUR und CHF via festen Kurs (wird separat aktualisiert)
    // Für jetzt: USD direkt, EUR/CHF approximiert
    g_btc[USD] = btc_usd;  // Binance liefert nur USD; EUR/CHF folgen via CoinGecko
    g_btc_chg[EUR] = g_btc_chg[CHF] = g_btc_chg[USD] = btc_chg_pct;
    Serial.printf("BTC: $%.2f (%.2f%%)\n", btc_usd, btc_chg_pct);
  }

  // ---- CoinGecko: Alle Währungen + Metalle ----
  http.begin(client,
    "https://api.coingecko.com/api/v3/simple/price"
    "?ids=bitcoin,pax-gold,kinesis-silver"
    "&vs_currencies=usd,eur,chf"
    "&include_24hr_change=true");
  http.setTimeout(15000);
  code = http.GET();
  if (code == 200) {
    JsonDocument doc;
    String body = http.getString();
    http.end();
    Serial.printf("CoinGecko OK (%d bytes)\n", body.length());
    auto err = deserializeJson(doc, body);
    if (!err) {
      // Enum: EUR=0, CHF=1, USD=2
      g_btc[EUR]     = doc["bitcoin"]["eur"]            | g_btc[EUR];
      g_btc[CHF]     = doc["bitcoin"]["chf"]            | g_btc[EUR] * 0.96f;
      g_btc[USD]     = doc["bitcoin"]["usd"]            | g_btc[EUR] * 1.09f;
      g_btc_chg[EUR] = doc["bitcoin"]["eur_24h_change"] | g_btc_chg[EUR];
      g_btc_chg[CHF] = doc["bitcoin"]["chf_24h_change"] | g_btc_chg[EUR];
      g_btc_chg[USD] = doc["bitcoin"]["usd_24h_change"] | g_btc_chg[EUR];

      g_gold[EUR]     = doc["pax-gold"]["eur"]            | 0.0f;
      g_gold[CHF]     = doc["pax-gold"]["chf"]            | 0.0f;
      g_gold[USD]     = doc["pax-gold"]["usd"]            | 0.0f;
      g_gold_chg[EUR] = doc["pax-gold"]["eur_24h_change"] | 0.0f;
      g_gold_chg[CHF] = doc["pax-gold"]["chf_24h_change"] | 0.0f;
      g_gold_chg[USD] = doc["pax-gold"]["usd_24h_change"] | 0.0f;

      g_silver[EUR]     = doc["kinesis-silver"]["eur"]            | 0.0f;
      g_silver[CHF]     = doc["kinesis-silver"]["chf"]            | 0.0f;
      g_silver[USD]     = doc["kinesis-silver"]["usd"]            | 0.0f;
      g_silver_chg[EUR] = doc["kinesis-silver"]["eur_24h_change"] | 0.0f;
      g_silver_chg[CHF] = doc["kinesis-silver"]["chf_24h_change"] | 0.0f;
      g_silver_chg[USD] = doc["kinesis-silver"]["usd_24h_change"] | 0.0f;

      Serial.printf("Gold: EUR %.2f  Silver: EUR %.4f\n", g_gold[EUR], g_silver[EUR]);
    }
  } else {
    http.end();
    Serial.printf("CoinGecko Fehler: %d – nur BTC verfuegbar\n", code);
  }

  // Wenn BTC > 0 gilt es als Erfolg (Gold/Silver optional)
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

  // Anleitung Box
  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_pos(box, 80, 100);
  lv_obj_set_size(box, 640, 280);
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
  lv_label_set_text(s2, "2.  Browser öffnen und aufrufen:");
  lv_obj_set_style_text_color(s2, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(s2, &lv_font_montserrat_18, 0);
  lv_obj_align(s2, LV_ALIGN_TOP_LEFT, 0, 124);

  lv_obj_t *ip = lv_label_create(box);
  lv_label_set_text(ip, "192.168.4.1");
  lv_obj_set_style_text_color(ip, lv_color_hex(0xF7931A), 0);
  lv_obj_set_style_text_font(ip, &lv_font_montserrat_28, 0);
  lv_obj_align(ip, LV_ALIGN_TOP_LEFT, 28, 156);

  // Warte-Status unten
  lv_obj_t *lbl_wait = lv_label_create(scr);
  lv_label_set_text(lbl_wait, "Warte auf WLAN-Verbindung...  (Timeout: 3 Min.)");
  lv_obj_set_style_text_color(lbl_wait, lv_color_hex(0x444455), 0);
  lv_obj_set_style_text_font(lbl_wait, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_wait, LV_ALIGN_BOTTOM_MID, 0, -20);

  lv_scr_load(scr);
  lv_timer_handler();
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
  char url[192];
  int code;

  if (a == ASSET_BTC) {
    // Binance Klines (BTCUSDT)
    const char *interval;
    uint8_t limit;
    switch (period) {
      case 1:  interval = "4h"; limit = 42; break;
      case 2:  interval = "1d"; limit = 30; break;
      default: interval = "1h"; limit = 24; break;
    }
    snprintf(url, sizeof(url),
      "https://api.binance.com/api/v3/klines?symbol=BTCUSDT&interval=%s&limit=%d",
      interval, limit);
    http.begin(client, url);
    http.setTimeout(10000);
    code = http.GET();
    if (code != 200) { http.end(); return; }
    JsonDocument doc;
    auto err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) return;
    int n = doc.size();
    if (n > CHART_MAX_PTS) n = CHART_MAX_PTS;
    for (int i = 0; i < n; i++) {
      float close_usd = atof(doc[i][4] | "0");
      g_chart_data[ASSET_BTC][period][i] = (int16_t)(close_usd / CHART_DIV[ASSET_BTC]);
    }
    g_chart_cnt[ASSET_BTC][period] = n;
    Serial.printf("BTC Chart P%d: %d Punkte\n", period, n);

  } else {
    // CoinGecko market_chart (pax-gold oder kinesis-silver)
    // Pause um CoinGecko Rate-Limit zu vermeiden (nach fetchPrices oder vorherigem Call)
    delay(3000);
    lv_timer_handler();
    const char *coin_id = (a == ASSET_GOLD) ? "pax-gold" : "kinesis-silver";
    int days = (period == 1) ? 7 : (period == 2) ? 30 : 1;
    snprintf(url, sizeof(url),
      "https://api.coingecko.com/api/v3/coins/%s/market_chart"
      "?vs_currency=usd&days=%d",
      coin_id, days);
    http.begin(client, url);
    http.setTimeout(15000);
    code = http.GET();
    if (code != 200) {
      Serial.printf("%s Chart P%d HTTP %d\n", coin_id, period, code);
      http.end(); return;
    }
    String body = http.getString();
    http.end();
    Serial.printf("%s Chart P%d body: %d bytes\n", coin_id, period, body.length());

    JsonDocument doc;
    auto err = deserializeJson(doc, body);
    if (err) { Serial.printf("%s JSON err: %s\n", coin_id, err.c_str()); return; }

    // Subsampling: market_chart liefert viele Punkte (bis 288 für 1T)
    JsonArray prices = doc["prices"].as<JsonArray>();
    int total = prices.size();
    Serial.printf("%s Chart P%d: %d Rohdaten\n", coin_id, period, total);
    if (total == 0) return;
    int step = max(1, total / (int)CHART_MAX_PTS);
    int n = 0;
    for (int i = 0; i < total && n < CHART_MAX_PTS; i += step) {
      float price = prices[i][1] | 0.0f;
      g_chart_data[a][period][n++] = (int16_t)(price / CHART_DIV[a]);
    }
    g_chart_cnt[a][period] = n;
    Serial.printf("%s Chart P%d: %d/%d Punkte\n", coin_id, period, n, total);
  }

  // Periodenänderung aus Chart berechnen (1W und 1M)
  uint8_t cnt = g_chart_cnt[a][period];
  if (cnt >= 2 && period > 0) {
    int16_t first = g_chart_data[a][period][0];
    int16_t last  = g_chart_data[a][period][cnt - 1];
    float chg = (first != 0) ? ((float)(last - first) / first * 100.0f) : 0.0f;
    if (period == 1) g_chg_w[a] = chg;
    else             g_chg_m[a] = chg;
    Serial.printf("Änderung P%d Asset%d: %.2f%%\n", period, (int)a, chg);
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

  // Boot-Screen anzeigen
  show_boot_screen();

  // WiFiManager – bei AP-Modus WiFi-Config-Screen zeigen
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wm.setAPCallback([](WiFiManager *wm) {
    Serial.println("AP-Modus aktiv – zeige WiFi-Screen");
    show_wifi_screen();
  });
  wm.autoConnect("FLASHTICKER", "flashticker");

  // Haupt-UI laden
  Serial.println("create_ui...");
  create_ui();
  lv_timer_handler();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WLAN: %s\n", WiFi.SSID().c_str());

    // DNS braucht kurz nach Verbindungsaufbau
    lv_label_set_text(lbl_status, "WLAN OK – warte auf DNS...");
    lv_timer_handler();
    delay(2000);

    // Explizit Google DNS setzen als Fallback
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
                IPAddress(8,8,8,8));
    delay(500);

    // NTP sync (UTC+2 für CEST)
    lv_label_set_text(lbl_status, "Uhrzeit wird synchronisiert...");
    lv_timer_handler();
    ntpClient.setTimeOffset(7200);
    ntpClient.begin();
    ntpClient.forceUpdate();

    // Kurse laden (bis zu 3 Versuche)
    lv_label_set_text(lbl_status, "Lade Kurse...");
    lv_timer_handler();

    for (int attempt = 1; attempt <= 3; attempt++) {
      Serial.printf("API Versuch %d/3...\n", attempt);
      if (fetchPrices()) {
        g_data_ok = true;
        g_last_fetch = millis();
        snprintf(g_last_update, sizeof(g_last_update),
                 "%02d:%02d", ntpClient.getHours(), ntpClient.getMinutes());
        Serial.println("Preise geladen!");
        fetch_all_charts(0);  // 1T Charts für alle Assets direkt nach Preisabruf
        break;
      }
      if (attempt < 3) {
        Serial.println("Retry in 3s...");
        delay(3000);
      }
    }

    update_prices();
    char sbuf[48];
    if (g_data_ok)
      snprintf(sbuf, sizeof(sbuf), "Aktualisiert: %s Uhr", g_last_update);
    else
      snprintf(sbuf, sizeof(sbuf), "Kein API-Zugriff – naechster Versuch in 5 Min.");
    lv_label_set_text(lbl_status, sbuf);
    lv_obj_invalidate(lv_scr_act()); // Force full redraw
    lv_timer_handler();
    delay(50);
    lv_timer_handler();
  } else {
    lv_label_set_text(lbl_status, "Kein WLAN – Bitte neu starten");
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
      fetch_all_charts(g_chart_period);  // Charts für aktive Periode neu laden
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
