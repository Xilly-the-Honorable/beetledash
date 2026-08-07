// BeetleDash gauge UI — five swipeable LVGL screens on the round 480x480 LCD:
// Fuel · Speed · Volts · Clock · Compass, with page dots. The UI pulls data
// from a provider callback ~10x per second (dummy generator in M3, live
// GaugeData snapshot from the sensor core afterwards).
#pragma once
#include "Sensors.h"

typedef void (*gauge_data_provider_t)(GaugeData *out);

void Gauge_UI_Init(gauge_data_provider_t provider);
