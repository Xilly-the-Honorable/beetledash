#include "Gauge_UI.h"
#include "Config.h"
#include <lvgl.h>
#include <math.h>

// ---------- Palette: 1965 VW/VDO instrument family (docs/V3_vintage_ui_brief.md §0) ----------
#define COL_FACE      0x141414   // matte near-black gauge face
#define COL_BEZEL     0xC9CDD1   // chrome outer ring
#define COL_BEZEL_SHD 0x5A5E62   // thin inner shadow ring
#define COL_CREAM     0xEDE6D6   // aged-white markings/needles (never pure white)
#define COL_SAGE      0xC9CFC0   // classic VDO grey-green numerals
#define COL_FAINT     0x6B6B60   // dimmed cream for secondary text
#define COL_TRACK     0x2A2A26   // unlit scale track
#define COL_RED       0xB3352C   // vintage red (muted)
#define COL_AMBER     0xC98A2C   // muted amber
#define COL_GREEN     0x3E6B4F   // muted green band
#define COL_ORANGE    0xE8930C   // instrument orange (compass only)
#define COL_YELLOW    0xE8C43C   // amber/yellow cardinal letters
#define COL_HUB_SILVER 0xB9BDC1  // speedo-style silver hub cap
#define COL_HUB_DARK  0x1A1A1A   // volt-style dark hub cap
#define COL_DOT_OFF   0x3A3A3A   // inactive page dot

// Legacy aliases used by the not-yet-restyled screens (removed as milestones land)
#define COL_BG      COL_FACE
#define COL_CARD    COL_TRACK
#define COL_TEXT    COL_CREAM
#define COL_MUTED   COL_SAGE

#define BEZEL_W       8          // chrome ring width
#define BEZEL_SHD_W   6          // shadow ring width

#define SCREEN_COUNT 5

static gauge_data_provider_t dataProvider = NULL;

static lv_obj_t *tileview;
static lv_obj_t *dots[SCREEN_COUNT];

// Fuel screen (TANK)
static lv_obj_t *fuelMeter, *fuelDigital;
static lv_meter_indicator_t *fuelNeedle;
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

// Plain circle (used for bezel rings, hubs, markers)
static lv_obj_t *make_circle(lv_obj_t *parent, lv_coord_t d, uint32_t fill)
{
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_set_size(c, d, d);
  lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(c, lv_color_hex(fill), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(c);
  return c;
}

// Chrome bezel: outer chrome ring -> thin shadow ring -> matte face.
// Concentric filled circles, drawn once, everything else goes on top.
static void make_bezel(lv_obj_t *tile)
{
  make_circle(tile, LV_HOR_RES, COL_BEZEL);
  make_circle(tile, LV_HOR_RES - 2 * BEZEL_W, COL_BEZEL_SHD);
  make_circle(tile, LV_HOR_RES - 2 * (BEZEL_W + BEZEL_SHD_W), COL_FACE);
}

// Letter-spaced caption ("TANK", "VOLTS", "MPH", ...)
static lv_obj_t *make_caption(lv_obj_t *parent, const char *text, lv_align_t align,
                              lv_coord_t x, lv_coord_t y)
{
  lv_obj_t *l = make_label(parent, &lv_font_montserrat_20, COL_CREAM, align, x, y, text);
  lv_obj_set_style_text_letter_space(l, 6, 0);
  return l;
}

// Tiny period wordmark near the bottom of every face
static void make_wordmark(lv_obj_t *tile)
{
  lv_obj_t *l = make_label(tile, &lv_font_montserrat_12, COL_FAINT,
                           LV_ALIGN_BOTTOM_MID, 0, -52, "BEETLEDASH");
  lv_obj_set_style_text_letter_space(l, 4, 0);
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

// Bare meter: no background, no border — just scale, ticks, and needle.
static lv_obj_t *make_bare_meter(lv_obj_t *parent, lv_coord_t size)
{
  lv_obj_t *m = lv_meter_create(parent);
  lv_obj_set_size(m, size, size);
  lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_set_style_pad_all(m, 0, 0);
  lv_obj_clear_flag(m, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return m;
}

// Needle hub: volt-style dark cap with a thin chrome edge, or silver speedo cap.
static void make_hub(lv_obj_t *tile, lv_coord_t d, lv_coord_t x, lv_coord_t y, bool silver)
{
  lv_obj_t *hub = make_circle(tile, d, silver ? COL_HUB_SILVER : COL_HUB_DARK);
  if (!silver) {
    lv_obj_set_style_border_width(hub, 2, 0);
    lv_obj_set_style_border_color(hub, lv_color_hex(COL_BEZEL), 0);
  }
  lv_obj_align(hub, LV_ALIGN_CENTER, x, y);
}

// ============================================================
//  Screen builders
// ============================================================
// Original VW "TANK" gauge: needle pivots at bottom center, sweeping ~100°
// across the top of the face. R (reserve) left, 1/2 middle, 1/1 right.
static void fuel_tick_label_cb(lv_event_t *e)
{
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->type != LV_METER_DRAW_PART_TICK || dsc->text == NULL) return;
  if (dsc->value == 0)       dsc->text = (char *)"R";
  else if (dsc->value == 50) dsc->text = (char *)"1/2";
  else if (dsc->value == 100)dsc->text = (char *)"1/1";
}

static void build_fuel(lv_obj_t *tile)
{
  make_bezel(tile);

  // Pivot sits at (240, 340) — meter is oversized so its center lands there.
  const lv_coord_t msize = 540, pivotY = 340;
  fuelMeter = make_bare_meter(tile, msize);
  lv_obj_set_pos(fuelMeter, 240 - msize / 2, pivotY - msize / 2);

  lv_meter_scale_t *sc = lv_meter_add_scale(fuelMeter);
  lv_meter_set_scale_range(fuelMeter, sc, 0, 100, 100, 220);  // 100° sweep across the top
  lv_meter_set_scale_ticks(fuelMeter, sc, 11, 3, 16, lv_color_hex(COL_CREAM));       // every 10%
  lv_meter_set_scale_major_ticks(fuelMeter, sc, 5, 6, 30, lv_color_hex(COL_CREAM), 30); // R, 1/2, 1/1
  lv_obj_set_style_text_font(fuelMeter, &lv_font_montserrat_24, LV_PART_TICKS);
  lv_obj_set_style_text_color(fuelMeter, lv_color_hex(COL_CREAM), LV_PART_TICKS);
  lv_obj_add_event_cb(fuelMeter, fuel_tick_label_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

  // Red reserve wedge over the first ~12%
  lv_meter_indicator_t *reserve = lv_meter_add_arc(fuelMeter, sc, 10, lv_color_hex(COL_RED), 0);
  lv_meter_set_indicator_start_value(fuelMeter, reserve, 0);
  lv_meter_set_indicator_end_value(fuelMeter, reserve, 12);

  fuelNeedle = lv_meter_add_needle_line(fuelMeter, sc, 6, lv_color_hex(COL_CREAM), -36);
  lv_meter_set_indicator_value(fuelMeter, fuelNeedle, 0);

  make_hub(tile, 56, 0, pivotY - 240, false);                 // dark cap, chrome edge
  fuelDigital = make_label(tile, &lv_font_montserrat_20, COL_FAINT, LV_ALIGN_CENTER, 0, 20, "--%");
  make_caption(tile, "TANK", LV_ALIGN_CENTER, 0, 146);
  make_wordmark(tile);
}

static void build_speed(lv_obj_t *tile)
{
  make_bezel(tile);
  make_caption(tile, "SPEED", LV_ALIGN_TOP_MID, 0, 56);
  speedValue = make_label(tile, &lv_font_montserrat_48, COL_TEXT, LV_ALIGN_CENTER, 0, -40, "--");
  make_huge(speedValue);
  make_label(tile, &lv_font_montserrat_24, COL_MUTED, LV_ALIGN_CENTER, 0, 36, "mph");
  speedKmh = make_label(tile, &lv_font_montserrat_20, COL_FAINT, LV_ALIGN_CENTER, 0, 76, "-- km/h");
  speedSats = make_label(tile, &lv_font_montserrat_14, COL_FAINT, LV_ALIGN_BOTTOM_MID, 0, -70, "acquiring fix...");
  make_wordmark(tile);
}

static void build_volts(lv_obj_t *tile)
{
  make_bezel(tile);
  make_caption(tile, "VOLTS", LV_ALIGN_TOP_MID, 0, 56);

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
  make_wordmark(tile);
}

static void build_clock(lv_obj_t *tile)
{
  make_bezel(tile);
  make_caption(tile, "CLOCK", LV_ALIGN_TOP_MID, 0, 56);
  clockValue = make_label(tile, &lv_font_montserrat_48, COL_TEXT, LV_ALIGN_CENTER, 0, -10, "--:--");
  make_huge(clockValue);
  clockSub = make_label(tile, &lv_font_montserrat_14, COL_FAINT, LV_ALIGN_CENTER, 0, 75, "waiting for GPS time");
  make_wordmark(tile);
}

static void build_compass(lv_obj_t *tile)
{
  make_bezel(tile);
  make_caption(tile, "COMPASS", LV_ALIGN_TOP_MID, 0, 56);

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
  make_wordmark(tile);
}

// ============================================================
//  Page dots
// ============================================================
static void update_dots(int active)
{
  for (int i = 0; i < SCREEN_COUNT; i++) {
    lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == active ? COL_CREAM : COL_DOT_OFF), 0);
    lv_obj_set_size(dots[i], i == active ? 10 : 7, i == active ? 10 : 7);
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

  // --- Fuel (TANK) ---
  int fuelPct = (int)(d.fuelPct + 0.5f);
  lv_meter_set_indicator_value(fuelMeter, fuelNeedle, fuelPct);
  lv_label_set_text_fmt(fuelDigital, "%d%%", fuelPct);

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
