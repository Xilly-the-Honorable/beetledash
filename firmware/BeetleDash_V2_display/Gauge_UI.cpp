#include "Gauge_UI.h"
#include "Config.h"
#include <lvgl.h>
#include <math.h>
#include <esp_heap_caps.h>

extern const lv_img_dsc_t beetle_icon;   // beetle_icon.c (A8 silhouette, recolored at runtime)

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
// Speed screen (VDO speedometer)
static lv_obj_t *speedMeter, *speedDigital, *speedSats;
static lv_meter_indicator_t *speedNeedle;
// Volts screen (VDO voltmeter)
static lv_obj_t *voltMeter, *voltDigital;
static lv_meter_indicator_t *voltNeedle;
// Clock screen (vintage VDO dash clock)
static lv_obj_t *clockMeter, *clockValue, *clockSub;
static lv_meter_indicator_t *clockHourHand, *clockMinHand;
// Compass screen (aviation direction indicator)
static lv_obj_t *compassCard, *compassReadout;

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

// Digital readout color state: muted red low, cream normal, muted amber high.
static uint32_t volt_text_color(float v)
{
  if (v < VOLT_LOW_BELOW)  return COL_RED;
  if (v > VOLT_HIGH_ABOVE) return COL_AMBER;
  return COL_CREAM;
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

// 1965 VDO speedometer: 0–80 sage numerals, 0 at ~7 o'clock sweeping clockwise
// to 80 at ~5 o'clock, long cream needle on a silver hub.
static void build_speed(lv_obj_t *tile)
{
  make_bezel(tile);

  speedMeter = make_bare_meter(tile, 430);
  lv_obj_center(speedMeter);

  lv_meter_scale_t *sc = lv_meter_add_scale(speedMeter);
  lv_meter_set_scale_range(speedMeter, sc, 0, 80, 270, 135);    // 0 @ 7 o'clock → 80 @ 5 o'clock
  lv_meter_set_scale_ticks(speedMeter, sc, 17, 3, 14, lv_color_hex(COL_CREAM));      // every 5 mph
  lv_meter_set_scale_major_ticks(speedMeter, sc, 2, 6, 26, lv_color_hex(COL_CREAM), 24); // every 10
  lv_obj_set_style_text_font(speedMeter, &lv_font_montserrat_28, LV_PART_TICKS);
  lv_obj_set_style_text_color(speedMeter, lv_color_hex(COL_SAGE), LV_PART_TICKS);    // sage numerals

  speedNeedle = lv_meter_add_needle_line(speedMeter, sc, 6, lv_color_hex(COL_CREAM), -14);
  lv_meter_set_indicator_value(speedMeter, speedNeedle, 0);

  make_hub(tile, 64, 0, 0, true);                               // silver speedo hub
  make_caption(tile, "MPH", LV_ALIGN_CENTER, 0, 72);
  speedDigital = make_label(tile, &lv_font_montserrat_28, COL_CREAM, LV_ALIGN_CENTER, 0, 118, "--");
  speedSats = make_label(tile, &lv_font_montserrat_14, COL_FAINT, LV_ALIGN_CENTER, 0, 152, "acquiring fix...");
  make_wordmark(tile);
}

// VDO voltmeter: 8–16 V scale (decivolt units internally), colored bands on the
// arc, needle from a lower-center pivot, live digital readout under the hub.
static void volt_tick_label_cb(lv_event_t *e)
{
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->type != LV_METER_DRAW_PART_TICK || dsc->text == NULL) return;
  static char buf[8];                       // LVGL draws each label before the next event
  lv_snprintf(buf, sizeof(buf), "%d", (int)(dsc->value / 10));
  dsc->text = buf;
}

static void build_volts(lv_obj_t *tile)
{
  make_bezel(tile);

  const lv_coord_t msize = 540, pivotY = 340;
  voltMeter = make_bare_meter(tile, msize);
  lv_obj_set_pos(voltMeter, 240 - msize / 2, pivotY - msize / 2);

  lv_meter_scale_t *sc = lv_meter_add_scale(voltMeter);
  lv_meter_set_scale_range(voltMeter, sc, 80, 160, 120, 210);   // 8–16 V over 120° up top
  lv_meter_set_scale_ticks(voltMeter, sc, 9, 3, 16, lv_color_hex(COL_CREAM));        // every 1 V
  lv_meter_set_scale_major_ticks(voltMeter, sc, 2, 6, 28, lv_color_hex(COL_CREAM), 28); // 8·10·12·14·16
  lv_obj_set_style_text_font(voltMeter, &lv_font_montserrat_24, LV_PART_TICKS);
  lv_obj_set_style_text_color(voltMeter, lv_color_hex(COL_CREAM), LV_PART_TICKS);
  lv_obj_add_event_cb(voltMeter, volt_tick_label_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

  // Colored bands on the scale arc (like the reference): red 8–10.5,
  // amber transition 10.5–11.5, muted green 11.5–16.
  struct { int from, to; uint32_t col; } bands[] = {
    {80, 105, COL_RED}, {105, 115, COL_AMBER}, {115, 160, COL_GREEN},
  };
  for (auto &b : bands) {
    lv_meter_indicator_t *arc = lv_meter_add_arc(voltMeter, sc, 10, lv_color_hex(b.col), 0);
    lv_meter_set_indicator_start_value(voltMeter, arc, b.from);
    lv_meter_set_indicator_end_value(voltMeter, arc, b.to);
  }

  voltNeedle = lv_meter_add_needle_line(voltMeter, sc, 6, lv_color_hex(COL_CREAM), -36);
  lv_meter_set_indicator_value(voltMeter, voltNeedle, 120);

  make_hub(tile, 56, 0, pivotY - 240, false);
  make_caption(tile, "VOLTS", LV_ALIGN_CENTER, 78, -24);        // right-of-center, like the reference

  // Live digital readout under the hub — hard requirement.
  voltDigital = make_label(tile, &lv_font_montserrat_28, COL_CREAM, LV_ALIGN_CENTER, 0, 150, "--.-");
  make_label(tile, &lv_font_montserrat_14, COL_FAINT, LV_ALIGN_CENTER, 52, 154, "V");
  make_wordmark(tile);
}

// Vintage VDO dash clock: cream analog hands, 12/3/6/9 numerals, small digital
// HH:MM under the hub. Minute scale 0–60; hour hand rides a hidden 0–720 scale.
static void clock_tick_label_cb(lv_event_t *e)
{
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->type != LV_METER_DRAW_PART_TICK || dsc->text == NULL) return;
  switch (dsc->value) {
    case 0:  dsc->text = (char *)"12"; break;
    case 15: dsc->text = (char *)"3";  break;
    case 30: dsc->text = (char *)"6";  break;
    case 45: dsc->text = (char *)"9";  break;
    default: dsc->text = (char *)"";   break;   // 5-minute marks: tick only
  }
}

static void build_clock(lv_obj_t *tile)
{
  make_bezel(tile);

  clockMeter = make_bare_meter(tile, 430);
  lv_obj_center(clockMeter);

  lv_meter_scale_t *scMin = lv_meter_add_scale(clockMeter);
  lv_meter_set_scale_range(clockMeter, scMin, 0, 60, 360, 270);   // 0 (=12) at the top
  lv_meter_set_scale_ticks(clockMeter, scMin, 61, 2, 10, lv_color_hex(COL_FAINT));      // minutes
  lv_meter_set_scale_major_ticks(clockMeter, scMin, 5, 5, 22, lv_color_hex(COL_CREAM), 26); // 5-min
  lv_obj_set_style_text_font(clockMeter, &lv_font_montserrat_28, LV_PART_TICKS);
  lv_obj_set_style_text_color(clockMeter, lv_color_hex(COL_CREAM), LV_PART_TICKS);
  lv_obj_add_event_cb(clockMeter, clock_tick_label_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

  // Tickless half-day scale so the hour hand moves smoothly between numerals
  lv_meter_scale_t *scHour = lv_meter_add_scale(clockMeter);
  lv_meter_set_scale_range(clockMeter, scHour, 0, 720, 360, 270);
  lv_meter_set_scale_ticks(clockMeter, scHour, 0, 0, 0, lv_color_black());

  clockMinHand  = lv_meter_add_needle_line(clockMeter, scMin,  5, lv_color_hex(COL_CREAM), -26);
  clockHourHand = lv_meter_add_needle_line(clockMeter, scHour, 8, lv_color_hex(COL_CREAM), -82);
  lv_meter_set_indicator_value(clockMeter, clockMinHand, 0);
  lv_meter_set_indicator_value(clockMeter, clockHourHand, 0);

  make_hub(tile, 44, 0, 0, false);
  clockValue = make_label(tile, &lv_font_montserrat_28, COL_CREAM, LV_ALIGN_CENTER, 0, 118, "--:--");
  clockSub = make_label(tile, &lv_font_montserrat_14, COL_FAINT, LV_ALIGN_CENTER, 0, 152, "waiting for GPS time");
  make_wordmark(tile);
}

// Aviation direction indicator: the card (ticks + numerals + cardinals) is drawn
// ONCE onto a canvas in PSRAM, then rotated as a whole with lv_img_set_angle()
// (card angle = -heading). The beetle icon and lubber line stay fixed on top.
#define CARD_SIZE 420
#define CARD_C    (CARD_SIZE / 2)

static void draw_compass_card(lv_obj_t *canvas)
{
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);

  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_hex(COL_CREAM);

  for (int deg = 0; deg < 360; deg += 5) {
    bool major = (deg % 10) == 0;
    ld.width = major ? 3 : 2;
    float a = deg * (float)M_PI / 180.0f;
    float sx = sinf(a), cy = -cosf(a);
    lv_coord_t r1 = CARD_C - 2, r2 = CARD_C - (major ? 22 : 13);
    lv_point_t pts[2] = {
      { (lv_coord_t)(CARD_C + r1 * sx), (lv_coord_t)(CARD_C + r1 * cy) },
      { (lv_coord_t)(CARD_C + r2 * sx), (lv_coord_t)(CARD_C + r2 * cy) },
    };
    lv_canvas_draw_line(canvas, pts, 2, &ld);
  }

  // Numerals every 30°: N/E/S/W in amber at the cardinals, tens-of-degrees
  // (3, 6, 12, 15, 21, 24, 30, 33) in cream between them.
  lv_draw_label_dsc_t td;
  lv_draw_label_dsc_init(&td);
  td.align = LV_TEXT_ALIGN_CENTER;
  const lv_coord_t rText = CARD_C - 52;
  for (int i = 0; i < 12; i++) {
    int deg = i * 30;
    bool cardinal = (deg % 90) == 0;
    td.font  = cardinal ? &lv_font_montserrat_28 : &lv_font_montserrat_24;
    td.color = lv_color_hex(cardinal ? COL_YELLOW : COL_CREAM);
    char buf[4];
    if (cardinal) buf[0] = "NESW"[deg / 90], buf[1] = '\0';
    else          lv_snprintf(buf, sizeof(buf), "%d", deg / 10);
    float a = deg * (float)M_PI / 180.0f;
    lv_coord_t x = (lv_coord_t)(CARD_C + rText * sinf(a));
    lv_coord_t y = (lv_coord_t)(CARD_C - rText * cosf(a));
    lv_canvas_draw_text(canvas, x - 40, y - 16, 80, &td, buf);
  }
}

static void build_compass(lv_obj_t *tile)
{
  make_bezel(tile);

  // Card canvas lives in PSRAM (ARGB, ~530 KB) — drawn once, rotated per update.
  static lv_color_t *cardBuf = NULL;
  if (cardBuf == NULL)
    cardBuf = (lv_color_t *)heap_caps_malloc(
        LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(CARD_SIZE, CARD_SIZE), MALLOC_CAP_SPIRAM);

  compassCard = lv_canvas_create(tile);
  lv_canvas_set_buffer(compassCard, cardBuf, CARD_SIZE, CARD_SIZE, LV_IMG_CF_TRUE_COLOR_ALPHA);
  lv_obj_center(compassCard);
  draw_compass_card(compassCard);
  lv_img_set_pivot(compassCard, CARD_C, CARD_C);

  // Fixed beetle silhouette, instrument orange, nose up — the card turns around it.
  lv_obj_t *ic = lv_img_create(tile);
  lv_img_set_src(ic, &beetle_icon);
  lv_obj_set_style_img_recolor(ic, lv_color_hex(COL_ORANGE), 0);
  lv_obj_set_style_img_recolor_opa(ic, LV_OPA_COVER, 0);
  lv_img_set_zoom(ic, 230);                     // 200 px tall art → ~180 px (~40% of face)
  lv_obj_center(ic);

  // Fixed orange lubber line at 12 o'clock, over the card edge.
  lv_obj_t *lubber = lv_obj_create(tile);
  lv_obj_set_size(lubber, 6, 30);
  lv_obj_set_style_bg_color(lubber, lv_color_hex(COL_ORANGE), 0);
  lv_obj_set_style_bg_opa(lubber, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(lubber, 0, 0);
  lv_obj_set_style_radius(lubber, 2, 0);
  lv_obj_clear_flag(lubber, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(lubber, LV_ALIGN_CENTER, 0, -(CARD_C - 12));

  compassReadout = make_label(tile, &lv_font_montserrat_24, COL_CREAM, LV_ALIGN_CENTER, 0, 146, "---");
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

  // --- Speed (VDO speedometer) ---
  int mph = (int)(d.speedMph + 0.5f);
  lv_meter_set_indicator_value(speedMeter, speedNeedle, mph > 80 ? 80 : mph);
  lv_label_set_text_fmt(speedDigital, "%d", mph);
  lv_label_set_text_fmt(speedSats, "%d sats · %s", d.sats, d.fix ? "fix" : "no fix");

  // --- Volts (VDO voltmeter) ---
  int v10 = (int)(d.battV * 10.0f + 0.5f);
  int vNeedle = v10 < 80 ? 80 : (v10 > 160 ? 160 : v10);   // needle stays on the 8–16 scale
  lv_meter_set_indicator_value(voltMeter, voltNeedle, vNeedle);
  lv_label_set_text_fmt(voltDigital, "%d.%d", v10 / 10, v10 % 10);
  lv_obj_set_style_text_color(voltDigital, lv_color_hex(volt_text_color(d.battV)), 0);

  // --- Clock (VDO dash clock) ---
  lv_label_set_text(clockValue, d.clock);
  lv_label_set_text(clockSub, d.fix ? "GPS time" : "waiting for GPS time");
  if (d.clock[0] >= '0' && d.clock[0] <= '9') {
    int hh = (d.clock[0] - '0') * 10 + (d.clock[1] - '0');
    int mm = (d.clock[3] - '0') * 10 + (d.clock[4] - '0');
    lv_meter_set_indicator_value(clockMeter, clockMinHand, mm);
    lv_meter_set_indicator_value(clockMeter, clockHourHand, (hh % 12) * 60 + mm);
  }

  // --- Compass (direction indicator) ---
  // Card angle = -heading, in LVGL 0.1° units. Skip the (expensive) rotation
  // redraw entirely when the heading hasn't moved.
  int hdg = (int)(d.headingDeg + 0.5f) % 360;
  static int lastAngle = -1;
  int angle = (3600 - (int)(d.headingDeg * 10.0f + 0.5f) % 3600) % 3600;
  if (angle != lastAngle) {
    lastAngle = angle;
    lv_img_set_angle(compassCard, angle);
  }
  int card = ((int)((d.headingDeg + 22.5f) / 45.0f)) % 8;
  lv_label_set_text_fmt(compassReadout, "%d° %s", hdg, CARDINALS[card]);
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
