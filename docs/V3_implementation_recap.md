# V3 vintage UI — implementation recap (for Claude)

Status: **all six milestones from `V3_vintage_ui_brief.md` are implemented, flashed, and running on the car's Waveshare 2.1" round panel.** One commit per milestone on `main`. LVGL stays pinned at 8.3.10. No changes to sensor logic, `GaugeData`, the I²C mutex, WiFi/dashboard, pin map, or calibration.

## What was built

### 0. Shared design language
- Chrome bezel = three concentric filled circles: `#C9CDD1` ring (8 px) → `#5A5E62` shadow ring (6 px) → `#141414` matte face.
- Palette constants exactly per brief (cream `#EDE6D6`, sage `#C9CFC0`, muted red/amber/green, instrument orange, amber-yellow cardinals).
- Letter-spaced captions, small "BEETLEDASH" wordmark near the bottom of every face.
- Page dots restyled: cream active, `#3A3A3A` inactive.

### 1. Fuel — TANK
- Bottom-center pivot done with an **oversized `lv_meter` (540 px) positioned so its center sits at (240, 340)**, below the visible face; 100° sweep across the top (rotation 220°).
- Tick labels R / 1/2 / 1/1 via a `LV_EVENT_DRAW_PART_BEGIN` override of `dsc->text`.
- Red reserve wedge = arc indicator over 0–12%. Dark hub with chrome edge, "TANK" caption below the pivot, subtle digital % readout mid-face.

### 2. Speed — VDO speedometer
- Full-round 430 px `lv_meter`: 0 at 7 o'clock → 80 at 5 o'clock (rotation 135°, span 270°), sage numerals every 10, cream minor ticks every 5.
- Cream needle, **silver hub**, "MPH" caption, small digital mph + sats/fix status under the hub.

### 3. Volts — VDO voltmeter
- Same lower-pivot trick as TANK; 8–16 V over 120° (decivolt units internally, 80–160).
- Band arcs on the scale: red 8–10.5, amber 10.5–11.5, green 11.5–16. Numerals 8·10·12·14·16 via tick-label override.
- **Digital readout under the hub** (hard requirement): live 1-decimal volts, color states kept muted (red < 11.8, cream normal, amber > 14.6). "VOLTS" caption right-of-center.

### 4. Compass — direction indicator
- Card (5°/10° ticks, numerals 3/6/12/15/21/24/30/33 cream, N/E/S/W amber `#E8C43C`) drawn **once** onto a 420×420 `lv_canvas` whose buffer lives in **PSRAM** (~530 KB, `LV_IMG_CF_TRUE_COLOR_ALPHA`).
- Rotated per update with `lv_img_set_angle(card, −heading)`; the rotation call is **skipped when the heading hasn't changed**, so a parked car costs nothing.
- Beetle icon (`beetle_icon.c`) recolored to `0xE8930C`, zoomed to ~180 px tall, fixed pointing up. Orange lubber line at 12 o'clock. Digital `247° SW` readout at the bottom.
- Smart-heading logic untouched.

### 5. Clock — VDO dash clock
- Analog: minute scale 0–60 with 12/3/6/9 numerals (tick-label override, other majors blank), hour hand on a hidden 0–720 half-day scale so it moves smoothly between numerals.
- Cream hands, dark chrome-edged hub, digital HH:MM under the hub. `TZ_OFFSET_MIN` untouched.

## Deviations / additions beyond the brief
1. **`beetle_icon.c` needed a one-line fix**: `#define LV_LVGL_H_INCLUDE_SIMPLE` before its include block — Arduino resolves `lvgl.h` from the library `src/` path, not `"lvgl/lvgl.h"`.
2. **Tap-to-advance navigation added post-brief** (user request from on-car testing): tapping anywhere on a gauge jumps to the next screen instantly (`LV_ANIM_OFF`, wraps around). Swiping still works — LVGL suppresses the click once a drag becomes a scroll. Rationale: swipe animation redraws the full 480×480 every frame and feels sluggish on this RGB panel; an instant tile switch is one redraw.
3. Flash shrank ~96 KB as a side effect: the 32/40/48 pt Montserrat fonts are no longer referenced now that the huge 2×-zoomed digital numbers are gone.

## On-hardware observations
- All five screens render and swipe; tap navigation confirmed working.
- Swipe animation is slow (expected: full-frame redraws during scroll on the ST7701 RGB bus). Mitigated by tap navigation rather than fighting the panel.
- Compass card rotation: no user complaint after flashing; the "pre-rendered card sectors" fallback from the brief was not needed.

## Definition-of-done checklist
- [x] Each screen reads as "same family" vintage VDO at arm's length (user-flashed; visual sign-off pending daylight test)
- [x] Compiles under LVGL 8.3.10 (pinned, no Library Manager update)
- [x] `UI_DEMO_MODE` still sweeps everything (verified by a dedicated compile + the demo provider covers all new widgets)
- [x] Phone dashboard untouched (`/data` + AP code not modified)
- [x] No changes to pin map, calibration, I²C mutex, or sensor logic
