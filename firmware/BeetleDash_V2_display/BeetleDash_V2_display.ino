/*
 * BeetleDash — V2 display firmware
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

#include "LVGL_Driver.h"     // pulls in Display_ST7701 / Touch_CST820 / TCA9554PWR / I2C_Driver

// ================= Milestone 1: panel bring-up =================
// Simple test screen: title + live touch readout. Sensors come in M2.

static lv_obj_t *touchLabel = NULL;

static void touch_probe_cb(lv_event_t *e)
{
  lv_indev_t *indev = lv_indev_get_act();
  if (!indev || !touchLabel) return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);
  lv_label_set_text_fmt(touchLabel, "touch  x=%d  y=%d", p.x, p.y);
}

static void make_test_screen(void)
{
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b0f14), LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "BeetleDash V2");
  lv_obj_set_style_text_color(title, lv_color_hex(0xe8eef5), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

  touchLabel = lv_label_create(scr);
  lv_label_set_text(touchLabel, "touch the screen...");
  lv_obj_set_style_text_color(touchLabel, lv_color_hex(0x7d8ea0), LV_PART_MAIN);
  lv_obj_align(touchLabel, LV_ALIGN_CENTER, 0, 20);

  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, touch_probe_cb, LV_EVENT_PRESSING, NULL);
}

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println("\nBeetleDash V2 display starting...");

  I2C_Init();
  TCA9554PWR_Init(0x00);       // all EXIO pins as outputs (LCD reset/CS, touch reset)
  Set_EXIO(EXIO_PIN8, Low);
  LCD_Init();                  // ST7701 reset + init + touch + backlight
  Lvgl_Init();

  make_test_screen();
  Serial.println("Panel up.");
}

void loop()
{
  Lvgl_Loop();
  vTaskDelay(pdMS_TO_TICKS(5));
}
