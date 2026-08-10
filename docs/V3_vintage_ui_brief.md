# V3 — Vintage VW/VDO gauge redesign (Cursor build brief)

**Goal:** restyle the five V2 screens so each one looks like a period instrument from a 1965 Beetle /
classic VDO catalog — not a modern smartwatch face. Reference photos: original VW "TANK" fuel gauge,
1965 VDO 0–80 mph speedometer, VDO 8–16 V voltmeter (red/green bands), and an aviation
direction-indicator for the compass.

**Keep everything that works:** tileview + swipe + page dots, `GaugeData` snapshot, dual-core split,
I²C mutex, `UI_DEMO_MODE`. This is a *visual* pass — no sensor or architecture changes.

---

## 0. Shared "1965 VDO" design language (apply to ALL screens)

| Element | Spec |
|---|---|
| Bezel | Chrome ring at the screen edge: outer ring `#C9CDD1`, thin inner shadow ring `#5A5E62`, then face. ~14 px total. |
| Face | Matte near-black `#141414`, with a slightly darker edge vignette if cheap to do (optional). |
| Markings/ticks | Cream white `#EDE6D6` (aged white, NOT pure #FFF). |
| Speedo-style numerals | Pale sage `#C9CFC0` (the classic VDO grey-green). |
| Needles | Cream white `#EDE6D6`, tapered; center hub: dark cap `#1A1A1A` with thin chrome edge (volt style) or silver cap `#B9BDC1` (speedo style). |
| Accents | Vintage red `#B3352C` (muted, not neon), instrument orange `#E8930C` (compass only). |
| Fonts | Keep Montserrat (largest enabled sizes; 2× zoom where already used). Slight letter-spacing on labels ("TANK", "VOLTS", "MPH"). |
| Branding | Small "VDO"-style wordmark position = a tiny **"BEETLEDASH"** or **"VDO"**-look label near the bottom of each face (pick one, consistent). |
| Page dots | Keep, restyle: tiny cream dots, active dot cream, inactive `#3A3A3A`. |
| NO | Gradients-as-decoration, glow, neon colors, progress-bar look, drop shadows. |

---

## 1. Fuel — original VW "TANK" gauge
- Needle pivots at **bottom center**, sweeping an arc across the **top** of the face (~100° total), like the reference.
- Scale marks (cream, slightly slanted bars like the original): left end **"R"** (reserve), middle **"1/2"**, right end **"1/1"**.
- Small red arrow/wedge at the "R" end (reserve warning zone, ~0–12%).
- Label **TANK** centered below the pivot, cream, letter-spaced.
- Mapping: 0% → R, 50% → 1/2, 100% → 1/1 (fuel % logic unchanged; low Ω = full).
- Optional small cream digital % readout near the bottom, subtle.

## 2. Speed — 1965 VDO speedometer
- Full round dial, numerals **0–80** every 10, sage `#C9CFC0`, arranged like the reference (0 at ~7 o'clock sweeping clockwise to 80 at ~5 o'clock).
- Minor ticks every 5 mph (cream, short); major ticks at numerals (longer).
- Long tapered cream needle, **silver hub** `#B9BDC1`.
- Small **MPH** label under center; wordmark below.
- Keep a small digital mph readout (cream) under the hub — glanceable and it's what GPS actually gives us.

## 3. Volts — VDO voltmeter + digital readout (user-requested)
- Arc scale **8–16 V** like the reference: needle pivot lower-center, scale sweeping the top ~120°.
- Colored bands ON the scale arc: **red `#B3352C` 8→10.5**, **hatched/amber transition 10.5→11.5** (a plain amber `#C98A2C` band is fine), **muted green `#3E6B4F` 11.5→16**.
- Numerals 8 · 10 · 12 · 14 · 16 (cream), 1 V minor ticks.
- Label **VOLTS** right-of-center like the reference; wordmark at bottom.
- **NEW: digital voltage readout** — cream, e.g. **"12.6"** with a small "V", centered under the hub, updated live (1 decimal). This is the user's explicit request.
- Keep the existing color-state logic for the digital number if you like (red <11.8, cream normal, amber >14.6) but keep it muted.

## 4. Compass — aviation direction indicator with Beetle icon (user-requested)
- **Rotating card, fixed vehicle icon** (like the aircraft reference):
  - Fixed **orange lubber line** at 12 o'clock (small triangle or line, `#E8930C`).
  - The **card rotates** so the current heading sits under the lubber line (card angle = −heading).
  - Card markings: tick every 5°, longer every 10°, numerals every 30° as **3, 6, 12, 15, 21, 24, 30, 33** (tens of degrees, cream) with **N / E / S / W** in **amber/yellow `#E8C43C`** at 0/90/180/270 — exactly like the reference photo.
- **Center icon: the Beetle silhouette** — asset provided at `firmware_assets/beetle_icon.c` (`lv_img_dsc_t beetle_icon`, 120×200, `LV_IMG_CF_ALPHA_8BIT`). Recolor to instrument orange:
  ```c
  lv_obj_set_style_img_recolor(ic, lv_color_hex(0xE8930C), 0);
  lv_obj_set_style_img_recolor_opa(ic, LV_OPA_COVER, 0);
  ```
  The icon stays **fixed pointing up** (the card turns around it). Scale to taste (~40% of face diameter tall); `lv_img_set_zoom` works on A8 images.
- Small cream digital readout at the bottom: `247° SW`.
- **Implementation note:** build the card ONCE (canvas or an `lv_img` snapshot), then rotate with `lv_img_set_angle()` (0.1° units, pivot = center). Do NOT redraw ticks every frame — RGB panel + full-screen redraws will hurt. If canvas memory is tight, allocate it in PSRAM.
- Keep smart-heading logic unchanged (GPS course >3 mph, mag at rest, declination −13°).

## 5. Clock — match the family
- Style it like a vintage VDO dash clock: black face, cream **analog** hands + 12/3/6/9 numerals if cheap to build with lv_meter/lines; small cream digital HH:MM under the hub.
- If analog hands are more than ~an hour of work, a large cream digital HH:MM in the vintage type treatment is acceptable — but analog is preferred for the family look.
- `TZ_OFFSET_MIN` logic unchanged.

---

## Asset placement
- Copy `firmware_assets/beetle_icon.c` into `firmware/BeetleDash_V2_display/` (Arduino builds every .c in the sketch folder). Declare `extern const lv_img_dsc_t beetle_icon;` where used.
- Keep `firmware_assets/beetle_icon.png` in the repo as the source-of-truth preview.

## Milestones (commit each)
1. **Shared style pass** — bezel/face/palette helpers + restyled page dots applied to all screens.
2. **Fuel (TANK)** — the signature screen; get this one photo-faithful first.
3. **Volts** — bands + digital readout.
4. **Speed** — dial + numerals + needle.
5. **Compass** — rotating card + beetle icon + lubber line.
6. **Clock** — family styling.

## Definition of done
- Side-by-side with the reference photos, each screen reads as "same family" at arm's length.
- Compiles under LVGL **8.3.10** (pinned — do NOT accept a Library Manager update).
- `UI_DEMO_MODE` still sweeps everything; phone dashboard untouched and live.
- No changes to pin map, calibration, I²C mutex, or sensor logic.

## Ask-first
- Any change touching the fixed pin map / calibration / mutex.
- If card rotation performance is poor (<15 fps feel), flag it — we'll discuss pre-rendered card sectors instead.
