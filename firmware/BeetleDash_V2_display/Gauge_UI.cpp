#include "Gauge_UI.h"
#include "Config.h"
#include <lvgl.h>
#include <math.h>

// ---------- Palette (matches the phone dashboard) ----------
#define COL_BG      0x0b0f14
#define COL_CARD    0x141b24
#define COL_TEXT    0xe8eef5
#define COL_MUTED   0x7d8ea0
#define COL_FAINT   0x5f7183
#define COL_RED     0xe5484d
#define COL_AMBER   0xf5a623
#define COL_GREEN   0x3fb950

#define SCREEN_COUNT 5

static gauge_data_provider_t dataProvider = NULL;

static lv_obj_t *tileview;
static lv_obj_t *dots[SCREEN_COUNT];

// Fuel screen
static lv_obj_t *fuelArc, *fuelValue;
// Speed screen
static lv_obj_t *speedValue, *speedKmh, *speedSats;
// Volts screen
static lv_obj_t *voltArc, *voltValue, *voltStatus;
// Clock screen
static lv_obj_t *clockValue, *clockSub;
// Compass screen
static lv_obj_t *compassValue, *compassCardinal, *compassCardLabels[8];

static const char *CARDINALS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

// ============================================================
//  Small helpers
// ============================================================
static lv_obj_t *make_tile(int idx)
{
  lv_obj_t *tile = lv_tileview_add_tile(tileview, idx, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(tile, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  return tile;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            lv_align_t align, lv_coord_t x, lv_coord_t y, const char *text)
{
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  lv_obj_align(l, align, x, y);
  return l;
}

// Blow a value label up 2x around its center — biggest built-in font is 48 pt,
// which is too small to glance at from the driver's seat.
static void make_huge(lv_obj_t *label)
{
  lv_obj_set_style_transform_pivot_x(label, lv_pct(50), 0);
  lv_obj_set_style_transform_pivot_y(label, lv_pct(50), 0);
  lv_obj_set_style_transform_zoom(label, 512, 0);
}

static uint32_t volt_color(float v)
{
  if (v < VOLT_LOW_BELOW)  return COL_RED;
  if (v > VOLT_HIGH_ABOVE) return COL_AMBER;
  return COL_GREEN;
}

static uint32_t fuel_color(float pct)
{
  if (pct < 15.0f) return COL_RED;
  if (pct < 30.0f) return COL_AMBER;
  return COL_GREEN;
}

// ============================================================
//  Screen builders
// ============================================================
static void build_fuel(lv_obj_t *tile)
{
  make_label(tile, &lv_font_montserrat_20, COL_MUTED, LV_ALIGN_TOP_MID, 0, 56, "FUEL");

  fuelArc = lv_arc_create(tile);
  lv_obj_set_size(fuelArc, 360, 360);
  lv_obj_center(fuelArc);
  lv_arc_set_rotation(fuelArc, 135);
  lv_arc_set_bg_angles(fuelArc, 0, 270);
  lv_arc_set_range(fuelArc, 0, 100);
  lv_arc_set_value(fuelArc, 0);
  lv_obj_remove_style(fuelArc, NULL, LV_PART_KNOB);          // gauge, not a slider
  lv_obj_clear_flag(fuelArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(fuelArc, 22, LV_PART_MAIN);
  lv_obj_set_style_arc_width(fuelArc, 22, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(fuelArc, lv_color_hex(COL_CARD), LV_PART_MAIN);
  lv_obj_set_style_arc_color(fuelArc, lv_color_hex(COL_GREEN), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(fuelArc, true, LV_PART_INDICATOR);

  // E / F at the arc ends (bottom-left / bottom-right of the 270° sweep)
  make_label(tile, &lv_font_montserrat_20, COL_FAINT, LV_ALIGN_CENTER, -105, 148, "E");
  make_label(tile, &lv_font_montserrat_20, COL_FAINT, LV_ALIGN_CENTER, 105, 148, "F");

  fuelValue = make_label(tile, &lv_font_montserrat_48, COL_TEXT, LV_ALIGN_CENTER, 0, -10, "--");
  make_huge(fuelValue);
  make_label(tile, &lv_font_montserrat_20, COL_MUTED, LV_ALIGN_CENTER, 0, 64, "%");
}

static void build_speed(lv_obj_t *tile)
{
  make_label(tile, &lv_font_montserrat_20, COL_MUTED, LV_ALIGN_TOP_MID, 0, 56, "SPEED");
  speedValue = make_label(tile, &lv_font_montserrat_48, COL_TEXT, LV_ALIGN_CENTER, 0, -40, "--");
  make_huge(speedValue);
  make_label(tile, &lv_font_montserrat_24, COL_MUTED, LV_ALIGN_CENTER, 0, 36, "mph");
  speedKmh = make_label(tile, &lv_font_montserrat_20, COL_FAINT, LV_ALIGN_CENTER, 0, 76, "-- km/h");
  speedSats = make_label(tile, &lv_font_montserrat_14, COL_FAINT, LV_ALIGN_BOTTOM_MID, 0, -70, "acquiring fix...");
}

static void build_volts(lv_obj_t *tile)
{
  make_label(tile, &lv_font_montserrat_20, COL_MUTED, LV_ALIGN_TOP_MID, 0, 56, "BATTERY");

  voltArc = lv_arc_create(tile);
  lv_obj_set_size(voltArc, 360, 360);
  lv_obj_center(voltArc);
  lv_arc_set_rotation(voltArc, 135);
  lv_arc_set_bg_angles(voltArc, 0, 270);
  lv_arc_set_range(voltArc, 90, 160);          // 9.0 V .. 16.0 V (x10)
  lv_arc_set_value(voltArc, 90);
  lv_obj_remove_style(voltArc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(voltArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(voltArc, 22, LV_PART_MAIN);
  lv_obj_set_style_arc_width(voltArc, 22, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(voltArc, lv_color_hex(COL_CARD), LV_PART_MAIN);
  lv_obj_set_style_arc_color(voltArc, lv_color_hex(COL_GREEN), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(voltArc, true, LV_PART_INDICATOR);

  voltValue = make_label(tile, &lv_font_montserrat_48, COL_TEXT, LV_ALIGN_CENTER, 0, -10, "--.-");
  make_huge(voltValue);
  make_label(tile, &lv_font_montserrat_20, COL_MUTED, LV_ALIGN_CENTER, 0, 64, "V");
  voltStatus = make_label(tile, &lv_font_montserrat_24, COL_GREEN, LV_ALIGN_CENTER, 0, 110, "");
}

static void build_clock(lv_obj_t *tile)
{
  make_label(tile, &lv_font_montserrat_20, COL_MUTED, LV_ALIGN_TOP_MID, 0, 56, "CLOCK");
  clockValue = make_label(tile, &lv_font_montserrat_48, COL_TEXT, LV_ALIGN_CENTER, 0, -10, "--:--");
  make_huge(clockValue);
  clockSub = make_label(tile, &lv_font_montserrat_14, COL_FAINT, LV_ALIGN_CENTER, 0, 75, "waiting for GPS time");
}

static void build_compass(lv_obj_t *tile)
{
  make_label(tile, &lv_font_montserrat_20, COL_MUTED, LV_ALIGN_TOP_MID, 0, 56, "COMPASS");

  // Fixed lubber line at 12 o'clock; the cardinal ring rotates beneath it.
  lv_obj_t *marker = lv_obj_create(tile);
  lv_obj_set_size(marker, 6, 26);
  lv_obj_set_style_bg_color(marker, lv_color_hex(COL_RED), 0);
  lv_obj_set_style_border_width(marker, 0, 0);
  lv_obj_set_style_radius(marker, 3, 0);
  lv_obj_align(marker, LV_ALIGN_CENTER, 0, -160);

  for (int i = 0; i < 8; i++) {
    bool major = (i % 2) == 0;
    compassCardLabels[i] = make_label(tile,
        major ? &lv_font_montserrat_28 : &lv_font_montserrat_16,
        i == 0 ? COL_RED : (major ? COL_TEXT : COL_FAINT),
        LV_ALIGN_CENTER, 0, 0, CARDINALS[i]);
  }

  compassValue = make_label(tile, &lv_font_montserrat_48, COL_TEXT, LV_ALIGN_CENTER, 0, -10, "---");
  make_huge(compassValue);
  compassCardinal = make_label(tile, &lv_font_montserrat_28, COL_MUTED, LV_ALIGN_CENTER, 0, 70, "-");
}

// ============================================================
//  Page dots
// ============================================================
static void update_dots(int active)
{
  for (int i = 0; i < SCREEN_COUNT; i++) {
    lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == active ? COL_TEXT : COL_FAINT), 0);
    lv_obj_set_size(dots[i], i == active ? 12 : 8, i == active ? 12 : 8);
  }
}

static void tile_changed_cb(lv_event_t *e)
{
  lv_obj_t *tile = lv_tileview_get_tile_act(tileview);
  update_dots(lv_obj_get_x(tile) / LV_HOR_RES);
}

static void build_dots(void)
{
  lv_obj_t *row = lv_obj_create(lv_layer_top());
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 4, 0);
  lv_obj_set_style_pad_column(row, 10, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -28);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

  for (int i = 0; i < SCREEN_COUNT; i++) {
    dots[i] = lv_obj_create(row);
    lv_obj_set_size(dots[i], 8, 8);
    lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dots[i], 0, 0);
    lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_CLICKABLE);
  }
  update_dots(0);
}

// ============================================================
//  Data binding
// ============================================================
static void refresh_cb(lv_timer_t *t)
{
  if (!dataProvider) return;
  GaugeData d;
  dataProvider(&d);

  // --- Fuel ---
  lv_arc_set_value(fuelArc, (int)(d.fuelPct + 0.5f));
  lv_obj_set_style_arc_color(fuelArc, lv_color_hex(fuel_color(d.fuelPct)), LV_PART_INDICATOR);
  lv_label_set_text_fmt(fuelValue, "%d", (int)(d.fuelPct + 0.5f));

  // --- Speed ---
  lv_label_set_text_fmt(speedValue, "%d", (int)(d.speedMph + 0.5f));
  lv_label_set_text_fmt(speedKmh, "%d km/h", (int)(d.speedMph * 1.609344f + 0.5f));
  lv_label_set_text_fmt(speedSats, "%d sats · %s", d.sats, d.fix ? "fix" : "no fix");

  // --- Volts ---
  int v10 = (int)(d.battV * 10.0f + 0.5f);
  lv_arc_set_value(voltArc, v10);
  uint32_t vc = volt_color(d.battV);
  lv_obj_set_style_arc_color(voltArc, lv_color_hex(vc), LV_PART_INDICATOR);
  lv_label_set_text_fmt(voltValue, "%d.%d", v10 / 10, v10 % 10);
  lv_label_set_text(voltStatus, d.battV < VOLT_LOW_BELOW ? "LOW" :
                                d.battV > VOLT_HIGH_ABOVE ? "HIGH" : "OK");
  lv_obj_set_style_text_color(voltStatus, lv_color_hex(vc), 0);

  // --- Clock ---
  lv_label_set_text(clockValue, d.clock);
  lv_label_set_text(clockSub, d.fix ? "GPS time" : "waiting for GPS time");

  // --- Compass ---
  int hdg = (int)(d.headingDeg + 0.5f) % 360;
  lv_label_set_text_fmt(compassValue, "%d°", hdg);
  int card = ((int)((d.headingDeg + 22.5f) / 45.0f)) % 8;
  lv_label_set_text(compassCardinal, CARDINALS[card]);
  // Rotate the cardinal ring so the current heading sits under the top marker.
  const float r = 190.0f;
  for (int i = 0; i < 8; i++) {
    float a = (i * 45.0f - d.headingDeg) * (float)M_PI / 180.0f;
    lv_obj_align(compassCardLabels[i], LV_ALIGN_CENTER,
                 (lv_coord_t)lroundf(r * sinf(a)), (lv_coord_t)lroundf(-r * cosf(a)));
  }
}

// ============================================================
void Gauge_UI_Init(gauge_data_provider_t provider)
{
  dataProvider = provider;

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(COL_BG), 0);

  tileview = lv_tileview_create(lv_scr_act());
  lv_obj_set_size(tileview, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_style_bg_color(tileview, lv_color_hex(COL_BG), 0);
  lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(tileview, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

  build_fuel(make_tile(0));
  build_speed(make_tile(1));
  build_volts(make_tile(2));
  build_clock(make_tile(3));
  build_compass(make_tile(4));
  build_dots();

  lv_timer_create(refresh_cb, 100, NULL);   // ~10 Hz UI refresh from the data provider
}
