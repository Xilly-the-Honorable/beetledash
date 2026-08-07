# BeetleDash V1 — Flash & Bench-Test Guide

V1 shows **fuel level, battery voltage, GPS speed, and compass heading** on your phone.
No display driver yet — this proves every wire is good before we tackle the round screen.

Power the board from your **computer's USB-C** for all of this. (The DORHEA 12V→5V buck is only needed in the car; it's not in the loop yet.)

---

## 1. Arduino IDE setup (one time)

1. Install the **ESP32 board package**: *File → Preferences → Additional Boards URLs* →
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   then *Tools → Board → Boards Manager* → install **esp32 by Espressif** (v3.x).
2. Install two libraries (*Tools → Manage Libraries*): **Adafruit ADS1X15** and **TinyGPSPlus** (by Mikal Hart).
3. Select the board and these settings under **Tools**:
   - Board: **ESP32S3 Dev Module**
   - USB CDC On Boot: **Enabled**
   - Flash Size: **16MB**
   - PSRAM: **OPI PSRAM**
4. Plug the board in via USB-C, pick the new COM/tty port, click **Upload**.
5. Open **Serial Monitor @ 115200** — you should see the ADS1115, magnetometer, and WiFi AP lines.

> If upload fails: hold **BOOT**, tap **RESET**, release **BOOT**, then upload again.

---

## 2. Bench wiring (all on the 12-pin header)

| From | To board pin |
|---|---|
| ADS1115 VDD / GND | 3V3 / GND |
| ADS1115 SDA | GPIO15 |
| ADS1115 SCL | GPIO7 |
| GPS puck VCC / GND | 3V3 / GND |
| GPS **TX** → | GPIO44 (board RX) |
| GPS **RX** ← | GPIO43 (board TX) |

The magnetometer on the GPS puck shares SDA/SCL — nothing extra to wire.

**Fuel divider** (on the perf board):
```
3V3 ──[ 100Ω ]──┬── ADS1115 A0
                │
           [ fuel sender ]
                │
               GND        + optional 0.1µF from A0 to GND
```

**Voltage divider** (bench: feed the "12V" node from a bench supply or the 5V pin to sanity-check):
```
+12V ──[ 47k ]──┬── ADS1115 A1
                │
             [ 10k ]
                │
               GND        + optional 0.1µF from A1 to GND
```

---

## 3. Test fuel without the tank

Drop a single resistor from the kit in place of the sender and watch the reading:

| Resistor | Expected fuel % |
|---|---|
| 10 Ω | ~0 % (empty) |
| 47 Ω | ~22 % |
| 100 Ω | ~53 % |
| 150 Ω | ~82 % |
| 180 Ω | ~100 % (full) |

A 200Ω trim pot is even nicer — sweep it and watch the bar move.

---

## 4. See it on your phone

1. On the phone WiFi list, join **BeetleDash** (password **beetle1234**).
2. Open a browser to **http://192.168.4.1**.
3. Live fuel bar, battery, speed, and a compass needle update ~2–3×/sec.

The `mag` field in the JSON (and Serial) tells you which compass chip was detected — **QMC5883L** or **HMC5883L**. Good to know for later.

---

## 5. Notes before it means anything real

- **GPS speed** needs a clear view of the sky and ~30–60 s for first fix. Indoors it stays at `no fix`.
- **Compass** needs calibration (spin the puck through all orientations) and a **declination** value for your area — around **−13°** for the NY area. Set `MAG_DECLINATION` at the top of the sketch. Mounted in a steel car it will need re-calibration in place; GPS course-over-ground is the reliable heading while moving.
- **Battery reading** is calibrated for the 47k/10k divider (`V = Vnode × 5.7`). Trim the `VOLT_R1/R2` constants to your actual resistor values if a meter disagrees.
- All fuel/voltage tuning lives in the `#define` block at the top — no need to hunt through the code.

---

## Next: V2 = the round display
Once you confirm these four values are live, I'll add the LVGL round-gauge UI (Arduino_GFX + ST7701) so the same data renders on the 2.1" screen — the phone dashboard keeps working alongside it.
