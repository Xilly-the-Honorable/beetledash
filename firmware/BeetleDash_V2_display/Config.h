// BeetleDash V2 — all tunables in one place.
// Pin map and calibration are FIXED (docs/PROJECT_CONTEXT.md §4–§5) — measured on
// the actual car. Do not change without re-measuring.
#pragma once

// ---------- Pins (external header; display/touch pins live in Display_ST7701.h) ----------
#define PIN_SDA   15
#define PIN_SCL    7
#define PIN_GPS_TX 43   // ESP32 TX -> GPS RX
#define PIN_GPS_RX 44   // ESP32 RX <- GPS TX

// ---------- Fuel + voltage calibration (verbatim from V1 bench) ----------
#define VEXC        3.30f    // divider excitation = board 3V3 rail
#define FUEL_RTOP   100.0f   // top resistor of the fuel divider (ohms)
#define FUEL_R_EMPTY 70.0f   // MEASURED on the sender: 70.0 ohm = empty (stock VW VDO)
#define FUEL_R_FULL  10.9f   // MEASURED on the sender: 10.9 ohm = full
// This VDO sender drops resistance as the tank fills; the linear map handles it.
#define VOLT_R1     47000.0f // volt divider top (to +12V)
#define VOLT_R2     10000.0f // volt divider bottom (to GND)
#define VOLT_TRIM   1.0f     // multiply Vbatt to match a multimeter (calibration trim)

// ---------- Voltage status thresholds (UI + phone dashboard colors) ----------
#define VOLT_LOW_BELOW   11.8f  // red under this
#define VOLT_HIGH_ABOVE  14.6f  // amber over this (overcharge)

// ---------- Compass ----------
#define MAG_DECLINATION -13.0f  // magnetic declination, deg (NY area)
#define GPS_HEADING_MIN_MPH 3.0f // above this speed, trust GPS course over the magnetometer

// ---------- Clock ----------
// Minutes to add to GPS UTC. America/New_York: EDT = -240, EST = -300. (No DST logic in v1.)
#define TZ_OFFSET_MIN  -240

// ---------- GPS update rate ----------
#define GPS_RATE_MS 200          // 5 Hz — sent at boot as UBX (NEO-M8N) + PMTK fallback

// ---------- Dual-circuit brake monitor (V4) ----------
// A2/A3 tap the two brake-light switch outputs through 47k/10k dividers
// (docs/PROJECT_CONTEXT.md §5). ~2.2 V at the ADC = circuit pressurized.
// IMPORTANT: leave DISABLED until the dividers are physically wired — floating
// ADS inputs drift and could latch a phantom fault. UI_DEMO_MODE stages a fake
// fault for reviewing the alert flow without hardware.
#define BRAKE_MONITOR_ENABLED 0
#define BRAKE_V_SET  0.50f       // ADC volts, rising  -> circuit pressurized
#define BRAKE_V_CLR  0.35f       // ADC volts, falling -> circuit released (hysteresis)
#define BRAKE_XOR_MS 1000        // one-sided pressure sustained this long = latched fault
                                 // (1 s rejects transient XOR from unequal switch closing points)

// ---------- WiFi Access Point (phone dashboard) ----------
#define AP_SSID "BeetleDash"
#define AP_PASS "beetle1234"     // >= 8 chars
