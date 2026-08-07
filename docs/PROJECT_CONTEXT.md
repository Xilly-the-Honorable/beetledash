# BeetleDash — Engineering Spec & AI-Coding Context

> **For the AI coding assistant (Cursor):** This file is the ground truth for this project.
> Treat the hardware pin map and the calibration constants as FIXED — do not change them
> without an explicit instruction. When in doubt, follow this file over any assumption.
> You can rename/copy this to `.cursorrules` (repo root) so it's always in context.

---

## 1. What we're building
A drop-in **round digital gauge** for a **1965 VW Beetle (12 V, negative ground)**, replacing the
OEM gauges in a 52 mm dash hole. A Waveshare round touchscreen (ESP32-S3) shows the readings and
also serves a live dashboard to a phone/PC over WiFi.

**V1:** Fuel level · Battery voltage · GPS speed · Compass heading, served to the phone. (Working baseline.)
**V2 (current):** All of the above + Clock rendered on the round LCD via LVGL as swipeable screens
(`firmware/BeetleDash_V2_display`), phone dashboard still live.
Out of scope: oil pressure, oil temperature, external temperature.

---

## 2. Hardware inventory
| Part | Role | Interface |
|---|---|---|
| Waveshare **ESP32-S3-Touch-LCD-2.1** (480×480 round, ST7701, capacitive touch) | MCU + display + WiFi/BLE | — |
| **ADS1115** 16-bit ADC (NOYITO) | reads fuel + battery voltage | I²C @ 0x48 |
| **NEO-M8N** GPS + compass puck (Deegoo) | speed/time (UART) + heading (I²C mag) | UART + I²C |
| **VDO fuel sender** | fuel level (resistive) | analog → ADS A0 |
| DORHEA **12 V→5 V 3 A buck** (on order) | in-car power | — |
| Perfboard, resistor kit, 0.1 µF cap, inline fuse | front-end + protection | — |

Bench power today = **USB-C from the PC**. In-car = buck to the board's 5 V pin.

---

## 3. Board & build settings (Arduino IDE 2.x)
- Core: **esp32 by Espressif Systems v3.x**
- Board: **Waveshare ESP32-S3-Touch-LCD-2.1** (dedicated profile — presets flash/PSRAM correctly)
- **USB CDC On Boot: Enabled** (required for Serial Monitor over native USB)
- Upload Mode: UART0 / Hardware CDC · Upload Speed 921600 · PSRAM Enabled
- Serial Monitor baud: **115200**

---

## 4. Pin map — FIXED (from the board's broken-out header)
| Function | ESP32-S3 GPIO | Connects to |
|---|---|---|
| I²C SDA | **GPIO15** | ADS1115 SDA + compass SDA |
| I²C SCL | **GPIO7** | ADS1115 SCL + compass SCL |
| UART TX | **GPIO43** | GPS RX (optional) |
| UART RX | **GPIO44** | **GPS TX** (this is the essential one — speed) |
| Spare | GPIO0 | free (e.g. night-dim input) |
| Power | 3V3 / 5V / GND | ADS1115 VCC on **3V3**; divider excitation on **3V3** |

The RGB display + touch use the rest of the GPIOs internally — do not repurpose them.

---

## 5. Analog front-end & calibration — FIXED
ADS1115 gain = **GAIN_ONE (±4.096 V)**. Powered from 3V3.

**A0 — Fuel** (divider excited from 3V3):
```
3V3 —[ 100 Ω ]—•— A0
               |
          [ VDO sender ]
               |
              GND
```
- Sender MEASURED: **empty = 70.0 Ω, full = 10.9 Ω** (resistance DROPS as tank fills).
- `Rs = 100 * Vnode / (3.30 - Vnode)`  → then linearly map Rs from 70.0 (0 %) to 10.9 (100 %), clamp 0–100.
- Expected A0: ~1.36 V empty, ~0.32 V full. Disconnected sender → reads empty (fail-safe). Keep an EMA to smooth slosh.

**A1 — Battery voltage** (47k/10k divider, 0.1 µF to GND):
- `Vbatt = Vnode * (47000 + 10000) / 10000` = `Vnode * 5.7`. Healthy: ~13.8–14.4 V running, ~12.4–12.7 V rest.
- Voltage calibration = trim the 5.7 ratio to match a multimeter.

**A2 / A3** — unused (spare for future).

---

## 6. GPS & compass
- GPS: **TinyGPSPlus**, UART @ **9600 8N1** on RX=GPIO44 / TX=GPIO43. Speed = `gps.speed.mph()`.
  Send a **5–10 Hz update-rate command** at boot for a smooth speedo (UBX or PMTK depending on chip).
- Time: set the **Clock** screen from GPS UTC + a timezone offset constant (America/New_York, DST-aware or a simple offset).
- Compass: magnetometer on the shared I²C bus. **Auto-detect the chip:** QMC5883L @ **0x0D**, HMC5883L @ **0x1E**,
  and also handle **IST8310** (@0x0E/0x0C) since these pucks vary. `heading = atan2(y, x)` + **magnetic declination**
  (≈ **−13°** for the NY area). Needs figure-8 hard/soft-iron calibration in the car.
  **Smart heading:** use GPS course-over-ground while moving (accurate, no cal), fall back to the magnetometer at rest.

---

## 7. Connectivity — phone/PC dashboard
- ESP32 in **WiFi AP mode**: SSID **`BeetleDash`**, password **`beetle1234`**, page at **http://192.168.4.1**.
- Serve a self-contained (offline, no CDN) page + a `/data` JSON endpoint (or WebSocket) with:
  `fuel, volts, speed, heading, sats, fix, mag`. Keep this working in V2 alongside the LCD UI.

---

## 8. V2 — display firmware requirements
- Framework: **Arduino + LVGL**. Start from **Waveshare's ESP32-S3-Touch-LCD-2.1 LVGL demo** (correct ST7701 RGB
  timings + touch), or **Arduino_GFX** for the panel. Do NOT hand-roll the RGB timing.
- **As built:** panel/touch drivers come verbatim from the Waveshare demo — ST7701 init over 3-wire SPI
  (GPIO1/2), CS/reset via **TCA9554 expander @0x20**, touch is a **CST820 @0x15** (not GT911 — that's the
  2.8" boards), LVGL **8.3.10** with the demo's `lv_conf.h` + large Montserrat fonts enabled.
  The touch controller shares the GPIO15/7 I²C bus with the ADS1115 + compass, so **every I²C
  transaction goes through the mutex in `I2C_Driver`** — keep it that way.
- **Swipeable screens:** Fuel · Speed · Volts · Clock · Compass (touch/swipe to change).
- Round-gauge aesthetic; readable at a glance in daylight. Optional night-dim later (GPIO0).
- Run **UI on one core, sensors + WiFi on the other**; non-blocking loop; drain GPS every loop, sample ADC/mag ~5 Hz.
- Libraries: `LVGL`, `Arduino_GFX` (or Waveshare BSP), `Adafruit_ADS1X15`, `TinyGPSPlus`, magnetometer lib per detected chip.

---

## 9. Conventions & guardrails
- Keep all tunables in a **config block of `#define`s at the top** (pins, divider values, calibration, SSID).
- Non-blocking `loop()` — no `delay()` in the main path. Comment the *why*, not the obvious.
- **Do not change** the pin map (§4) or calibration constants (§5) without an explicit instruction — they're measured/verified.
- `BeetleDash_V1_bench.ino` in this repo is the **working V1 baseline** (fuel-calibrated). Build V2 from it; keep it compiling.
- If a compile error needs a pin/lib change that touches §4–§6, flag it rather than silently editing.

---

## 10. Definition of done (per feature)
1. Compiles for the Waveshare board profile with the settings in §3.
2. Serial @115200 prints sane values; `/data` JSON matches on the phone.
3. Fuel tracks the sender direction correctly (low Ω = full). Voltage matches a meter after trim.
4. GPS gets a fix outdoors; speed reads 0 at rest. Compass responds and is roughly correct after cal.
5. (V2) All screens render on the LCD and swipe; phone dashboard still live.
```
```
_Source of truth maintained by the project owner. Update this file when a spec changes;
don't let code and this file diverge._
