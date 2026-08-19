/*
 * BeetleDash — V2.2 display firmware - No demo Mode
 * ------------------------------------------------------------
 * Target : Waveshare ESP32-S3-Touch-LCD-2.1 (round 480x480, ST7701 RGB, CST820 touch)
 *
 * Renders Fuel / Speed / Volts / Clock / Compass as swipeable LVGL screens
 * on the round LCD, while keeping the V1 WiFi phone dashboard alive.
 *
 * Panel + touch bring-up (Display_ST7701, Touch_CST820, TCA9554PWR, I2C_Driver,
 * LVGL_Driver) is based on Waveshare's official LVGL_Arduino demo for this exact
 * board — ST7701 init sequence and RGB timings are used verbatim, do not tweak.
 *
 * Board settings (Arduino IDE / arduino-cli):
 *   Board: Waveshare ESP32-S3-Touch-LCD-2.1  ·  USB CDC On Boot: Enabled
 *   PSRAM: Enabled (OPI)  ·  Serial monitor @ 115200
 *
 * Libraries: lvgl 8.3.x (from the Waveshare demo package, see docs/SETUP.md),
 *            Adafruit ADS1X15, TinyGPSPlus
 * ------------------------------------------------------------
 */

#include "Config.h"       // all tunables (pins, calibration, AP, timezone)
#include "LVGL_Driver.h"  // pulls in Display_ST7701 / Touch_CST820 / TCA9554PWR / I2C_Driver
#include "Sensors.h"      // core-0 sensor/WiFi task + GaugeData snapshot
#include "Gauge_UI.h"     // five swipeable gauge screens + page dots

// Set to 1 to drive the UI with sweeping demo values instead of real sensors
// (handy on the bench with nothing wired up).
#define UI_DEMO_MODE 0

#if UI_DEMO_MODE
static void dummy_provider(GaugeData *d) {
  float t = millis() / 1000.0f;
  d->fuelPct = 50.0f + 50.0f * sinf(t * 0.30f);
  d->battV = 12.8f + 3.4f * sinf(t * 0.20f);   // dips below 10 V to demo the VOLTS LOW alert
  d->speedMph = 30.0f + 30.0f * sinf(t * 0.15f);
  d->headingDeg = fmodf(t * 20.0f, 360.0f);
  d->sats = 7;
  d->fix = true;
  strcpy(d->clock, "12:34");
  strcpy(d->magName, "demo");
  // Brake demo: lamps pulse alternately (2 s each), and a fake C1 fault is
  // staged during seconds 15-45 of every minute — raises the overlay, lets you
  // acknowledge to the banner, then drops (as if cleared) and re-stages next
  // minute to exercise the re-raise path.
  int phase = ((int)t) % 4;
  d->brake1 = (phase < 2);
  d->brake2 = (phase >= 2);
  int cycle = ((int)t) % 60;
  d->brakeFault = (cycle >= 15 && cycle < 45) ? BRAKE_FAULT_C1 : BRAKE_FAULT_NONE;
}
#endif

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nBeetleDash V2 display starting...");

  I2C_Init();
  TCA9554PWR_Init(0x00);  // all EXIO pins as outputs (LCD reset/CS, touch reset)
  Set_EXIO(EXIO_PIN8, Low);
  LCD_Init();  // ST7701 reset + init + touch + backlight
  Lvgl_Init();

  Sensors_Start();  // sensors + WiFi AP on core 0; LVGL owns core 1

#if UI_DEMO_MODE
  Gauge_UI_Init(dummy_provider);
#else
  Gauge_UI_Init(Gauge_GetData);  // live sensor snapshot from core 0
#endif
  Serial.println("Panel up.");
}

void loop() {
  Lvgl_Loop();
  vTaskDelay(pdMS_TO_TICKS(5));
}
