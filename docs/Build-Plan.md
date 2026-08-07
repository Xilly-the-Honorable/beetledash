# 1965 Beetle — Round LCD Digital Dash: Build Plan

**Goal:** A round LCD gauge in the 80 mm dash hole showing fuel level, oil temperature, oil pressure, and outside-air temperature (plus GPS speed/heading as a bonus), with a live view mirrored to an iPhone over WiFi.

**Decisions locked in:** 12V (converted) electrical system · LCD *replaces* the original gauges · iPhone via WiFi (recommendation below) · fuel sender in hand, remaining senders to be sourced.

---

## 1. Parts — what you have vs. what's still needed

### In hand ✅
| Part | Role |
|---|---|
| Waveshare ESP32-S3 2.1" round touch (480×480, ST7701 RGB) | Brain + gauge face + WiFi/BLE |
| NOYITO ADS1115 4-ch 16-bit ADC (I²C) | Reads the 4 analog sensor voltages |
| Deegoo NEO-M8N GPS + compass | Speed, heading, position (UART + I²C) |
| VDO electric fuel sender (10Ω empty / 180Ω full) | Fuel level |
| Double-sided perf board | Mount the divider network + connectors |
| 25-value resistor kit | Divider resistors, pull-ups |

### Still to buy 🛒
| Part | Why | Suggested spec |
|---|---|---|
| **12V→5V buck converter** | Board runs on 5V; car is 12–14.4V and spikes | Pololu **D24V22F5** (5V, 2.5A, 36V-tolerant input) — automotive-robust. Budget option: LM2596 module + protection below |
| **Inline fuse + holder** | Protect the tap from the ignition/accessory feed | 1A blade fuse, inline holder |
| **Reverse-polarity + spike protection** | Automotive 12V is dirty (load dump) | Schottky diode (e.g. SS34) in series **or** P-FET; TVS diode P6KE18A across input |
| **VDO oil PRESSURE sender (electric)** | Oil pressure gauge | 10–180Ω resistive, range 0–80 psi / 0–5 bar. Confirm thread to match the case port (Beetle oil-pressure port ≈ M10×1) |
| **VDO oil TEMPERATURE sender (electric)** | Oil temp gauge | Thermistor type, ~700Ω cold → ~40Ω hot. Needs a mounting location (drain-plug adapter, sandwich plate, or shared oil-pressure port with a T) |
| **External temp probe** | Outside-air temp | **DS18B20 waterproof** (digital, ±0.5°C, one wire). Simpler and more accurate than an analog thermistor |
| Wiring / connectors | Sender runs + power | JST/Dupont, 18–20 AWG for power, shielded pair nice-to-have for sender runs |

> ⚠️ **Confirm before buying the oil senders:** the thread size on your engine's ports. Send me a photo of the oil-pressure port (and where you want oil temp to read from) and I'll pin down exact VDO part numbers so the threads and gauge ranges match.

---

## 2. System architecture

```
        12V ign/acc ─[1A fuse]─[reverse-poly + TVS]─► 12V→5V buck ─► 5V ─┐
                                                                          │
                                                        ┌─────────────────▼──────────────────┐
                                                        │   Waveshare ESP32-S3 round display  │
                                                        │   (RGB LCD + touch + WiFi/BLE)      │
                                                        │                                     │
        VDO fuel  ─┐                                    │  I²C  (GPIO15 SDA / GPIO7 SCL) ◄────┼── ADS1115 (0x48)
        oil press ─┤   divider network on              │  UART (GPIO43 TX / GPIO44 RX)  ◄────┼── NEO-M8N GPS
        oil temp  ─┤   perf board, excited by 3V3 ─────┼─► ADS1115 A0/A1/A2                   │
        12V batt  ─┘                                    │  I²C ◄──────────────────────────────┼── compass (QMC5883L 0x0D)
                                                        │  1-Wire (GPIO0) ◄───────────────────┼── DS18B20 outside-air temp
                                                        │  WiFi AP "BeetleDash" ──────────────┼──► iPhone (Safari dashboard)
                                                        └─────────────────────────────────────┘
```

The round display eats ~20 GPIOs for the RGB panel, but Waveshare breaks out a 12-pin header with a **dedicated I²C bus, a UART pair, GPIO0, and 3V3/5V/GND** — exactly the pins we need. Nothing on the header collides with the display.

---

## 3. Pin map / wiring

| Signal | ESP32-S3 pin | Notes |
|---|---|---|
| ADS1115 SDA | GPIO15 (I²C SDA) | Shared bus; ADS1115 addr **0x48** (ADDR→GND) |
| ADS1115 SCL | GPIO7 (I²C SCL) | 4.7k pull-ups (board likely already has them) |
| ADS1115 VDD | 3V3 | Same rail used to excite the dividers |
| Compass (QMC5883L) | same I²C bus | addr 0x0D — no conflict with ADC or touch |
| GPS TX → | GPIO44 (UART RX) | NEO-M8N default **9600 baud** |
| GPS RX ← | GPIO43 (UART TX) | |
| GPS VCC/GND | 3V3 / GND | |
| DS18B20 data | GPIO0 | 4.7k pull-up to 3V3 (idles high = normal boot) |
| 5V in | 5V header pin | From buck output |
| GND | GND | **Star-ground** the buck, board, ADS1115, and sender returns together |

### ADS1115 channel assignment
| Ch | Sensor | Divider top resistor (to 3V3) | Voltage span (approx) |
|---|---|---|---|
| A0 | Fuel (10–180Ω) | **220Ω** | 0.14 V (empty) → 1.49 V (full) |
| A1 | Oil pressure (10–180Ω) | **220Ω** | 0.14 V → 1.49 V |
| A2 | Oil temp (~40–700Ω) | **330Ω** | 0.36 V (hot) → 2.24 V (cold) |
| A3 | Battery voltage (bonus) | 100k / 27k divider | 2.55 V @12V, 3.06 V @14.4V |

**Set the ADS1115 PGA to ±4.096V (gain 1).** All signals stay well under 3.3V, and 16-bit resolution gives ~125 µV/count — far finer than any gauge needs.

### How each resistive sender is read
Each VDO sender is grounded at one end to the chassis. We build a two-resistor divider on the perf board:

```
3V3 ──[ R_top ]──●── ADS1115 input
                 │
             [ sender ]  (variable resistance)
                 │
                chassis GND
```
`V_node = 3.3 × R_sender / (R_top + R_sender)`. Because we drive it from the board's own regulated 3V3 — **not** car voltage — the reading is stable and electrically isolated from the noisy 12V bus. Since the OEM gauges are being removed, each sender feeds only this divider (no fighting over the signal).

The **DS18B20** reports °C directly over 1-Wire, so outside-air temp needs no divider or calibration. That frees ADS1115 **A3** to double as a **battery voltmeter** — a gauge the original car never had.

---

## 4. Power supply (12V)

Tap a **switched/accessory** 12V feed (dies with the key so it can't flatten the battery). Chain: `12V → 1A fuse → reverse-polarity protection → TVS across input → buck → 5V → board 5V pin`. Size the buck for ≥1A (display backlight + WiFi bursts pull 300–500 mA; headroom keeps it cool). The Pololu D24V22F5 tolerates input spikes to 36V, which matters on an older charging system; a bare LM2596 module works too but *only* with the fuse + TVS + reverse-polarity parts in front of it.

---

## 5. iPhone display — recommendation

Go with a **WiFi web dashboard, ESP32 in Access-Point mode.** The ESP32 hosts its own network ("BeetleDash"); you connect the phone once and open a page in Safari that shows live gauges, updated over a WebSocket. No app to install, no cell signal needed, and it looks better on the phone than a literal screen mirror would.

I'd skip *mirroring the exact round screen* — that means continuously streaming the framebuffer (MJPEG), which pins the CPU and adds lag for no real benefit, since a purpose-built web gauge page is cleaner and lighter. (If you later want the phone to be the *primary* display in a phone mount, we can revisit.)

---

## 6. Firmware plan (Arduino / ESP-IDF)

Framework: **Arduino-ESP32** with **LVGL** for the round gauge UI (Waveshare ships an ST7701 + LVGL demo we start from). Libraries: `Adafruit_ADS1X15` (ADC), `TinyGPSPlus` (GPS), a QMC5883 lib (compass), `OneWire` + `DallasTemperature` (DS18B20), `ESPAsyncWebServer` + WebSockets (phone dashboard).

Main loop: read the 4 ADC channels → convert to engineering units via per-sender calibration tables → read GPS + compass + DS18B20 → update the LVGL gauges on the round screen → push a small JSON packet over WebSocket to any connected phone. All sensor reads are cheap; the display refresh dominates, so we run the UI on one core and sensors/WiFi on the other.

---

## 7. Calibration

Each VDO sender is characterized as **resistance → engineering unit**, so the firmware converts measured voltage → resistance → value via a small lookup table:

- **Fuel:** 10Ω = empty, 180Ω = full; interpolate linearly (refine later with real tank levels).
- **Oil pressure:** use the sender's published resistance-vs-pressure curve (I'll enter the table once we pick the exact part).
- **Oil temp:** use the thermistor's resistance-vs-temp curve from the datasheet.
- **Outside temp:** none — DS18B20 reads °C directly.
- **Battery:** `V = V_adc × 4.70` (from the 100k/27k divider).

A one-time bench check per channel (known resistor or known input) sets the offset; I'll build a small on-screen calibration mode so you can trim each gauge in the car.

---

## 8. Next steps
1. **Photos of the engine ports** (oil-pressure port; intended oil-temp location) → I lock exact VDO oil sender part numbers + threads.
2. Order the shopping-list parts (buck + protection + two oil senders + DS18B20).
3. I generate the firmware skeleton (display + ADC + WiFi dashboard) so you can flash it and see live fuel/battery readings on the bench before the oil senders arrive.
4. Build the divider network on the perf board; bench-test with fixed resistors standing in for the senders.
5. Install, calibrate in the car.
