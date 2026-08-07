# 🚗 BeetleDash

A drop-in **round digital gauge** for a **1965 VW Beetle (12 V, negative ground)** — replacing the
OEM gauges in a 52 mm dash hole with a Waveshare round touchscreen (ESP32-S3). It shows fuel, speed,
voltage, a clock, and a compass, and serves the same data live to a phone/PC over WiFi.

> **Working with an AI coding assistant?** Read [`docs/PROJECT_CONTEXT.md`](docs/PROJECT_CONTEXT.md) first —
> it is the ground truth for pin map, calibration, and requirements. (Copy it to `.cursorrules` to keep it in context.)

---

## Status
- ✅ **V1 bench firmware** — reads fuel + battery (ADS1115), GPS speed, compass; serves a WiFi dashboard. No display driver yet.
- ✅ **Fuel calibrated** to the actual sender: **70.0 Ω empty / 10.9 Ω full**.
- ⏳ **V2** — LVGL round-display UI (Fuel · Speed · Volts · Clock · Compass), phone dashboard alongside.

## Hardware
| Part | Role | Bus |
|---|---|---|
| Waveshare ESP32-S3-Touch-LCD-2.1 (480×480 round) | MCU + display + WiFi | — |
| ADS1115 16-bit ADC | fuel + battery voltage | I²C 0x48 |
| NEO-M8N GPS + compass puck | speed/time + heading | UART + I²C |
| VDO fuel sender | fuel level | analog → A0 |
| DORHEA 12 V→5 V 3 A buck | in-car power | — |

## Wiring (quick reference — full detail in the docs)
| Function | GPIO | To |
|---|---|---|
| I²C SDA / SCL | 15 / 7 | ADS1115 + compass |
| UART RX / TX | 44 / 43 | GPS TX / RX |
| Power | 3V3 / 5V / GND | ADS on 3V3; board 5V from buck |

- Fuel divider: `3V3 –[100 Ω]– A0 –[VDO sender]– GND`
- Voltage divider: `12V –[47k]– A1 –[10k]– GND` → `V = Vnode × 5.7`, 0.1 µF cap A1→GND

## Repo layout
```
firmware/BeetleDash_V1_bench/   Arduino sketch (V1 baseline)
docs/PROJECT_CONTEXT.md         ground truth for AI coding + humans
docs/SETUP.md                   flash & bench-test guide
docs/Build-Plan.md              architecture & design notes
```

## Build & flash
Arduino IDE 2.x, core **esp32 by Espressif v3.x**, board **Waveshare ESP32-S3-Touch-LCD-2.1**,
**USB CDC On Boot: Enabled**, Serial @ 115200. Full steps in [`docs/SETUP.md`](docs/SETUP.md).

## Dashboard
Board runs a WiFi AP **`BeetleDash`** (pw `beetle1234`) → open **http://192.168.4.1**.
If your PC has wired internet, you can join BeetleDash on WiFi and keep internet on ethernet at once.

## License
MIT (see `LICENSE`) — do what you like, no warranty. It's a car gauge; fuse your 12 V tap.
