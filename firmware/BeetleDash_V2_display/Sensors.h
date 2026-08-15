// Sensor + comms side of BeetleDash V2 (runs as a FreeRTOS task on core 0):
// ADS1115 fuel/battery, GPS speed/time, magnetometer heading, WiFi AP + /data.
// Sensor math is the V1 bench logic, unchanged. LVGL (core 1) reads a snapshot
// through Gauge_GetData().
#pragma once
#include <Arduino.h>

// brakeFault codes
#define BRAKE_FAULT_NONE 0
#define BRAKE_FAULT_C1   1   // circuit 1 failed to build pressure
#define BRAKE_FAULT_C2   2   // circuit 2 failed to build pressure

struct GaugeData {
  float fuelPct;
  float battV;
  float speedMph;
  float headingDeg;
  int   sats;
  bool  fix;
  char  clock[6];    // "HH:MM" local time from GPS, "--:--" until time is valid
  char  magName[10]; // detected magnetometer chip, for diagnostics
  bool  brake1;      // brake circuit 1 pressurized (switch closed)
  bool  brake2;      // brake circuit 2 pressurized
  uint8_t brakeFault; // BRAKE_FAULT_* — latched by the sensor task
};

void Sensors_Start(void);              // init sensors + WiFi AP, spawn the core-0 task
void Gauge_GetData(GaugeData *out);    // thread-safe snapshot for the UI core
void Sensors_ClearBrakeFault(void);    // long-press escape hatch (false-latch recovery)
