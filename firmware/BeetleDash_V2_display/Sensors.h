// Sensor + comms side of BeetleDash V2 (runs as a FreeRTOS task on core 0):
// ADS1115 fuel/battery, GPS speed/time, magnetometer heading, WiFi AP + /data.
// Sensor math is the V1 bench logic, unchanged. LVGL (core 1) reads a snapshot
// through Gauge_GetData().
#pragma once
#include <Arduino.h>

struct GaugeData {
  float fuelPct;
  float battV;
  float speedMph;
  float headingDeg;
  int   sats;
  bool  fix;
  char  clock[6];    // "HH:MM" local time from GPS, "--:--" until time is valid
  char  magName[10]; // detected magnetometer chip, for diagnostics
};

void Sensors_Start(void);              // init sensors + WiFi AP, spawn the core-0 task
void Gauge_GetData(GaugeData *out);    // thread-safe snapshot for the UI core
