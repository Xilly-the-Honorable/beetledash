// LVGL glue for the Waveshare ESP32-S3-Touch-LCD-2.1 — based on Waveshare's
// LVGL_Arduino demo (LVGL_Driver). Flush writes straight into the RGB panel's
// double framebuffer in PSRAM; touch is the CST820 pointer device.
#pragma once

#include <lvgl.h>
#include <esp_heap_caps.h>
#include "Display_ST7701.h"
#include "Touch_CST820.h"

#define LCD_WIDTH     ESP_PANEL_LCD_WIDTH
#define LCD_HEIGHT    ESP_PANEL_LCD_HEIGHT

#define EXAMPLE_LVGL_TICK_PERIOD_MS  2

extern lv_disp_drv_t disp_drv;

void Lvgl_Display_LCD( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p );
void Lvgl_Touchpad_Read( lv_indev_drv_t * indev_drv, lv_indev_data_t * data );
void example_increase_lvgl_tick(void *arg);

void Lvgl_Init(void);
void Lvgl_Loop(void);
