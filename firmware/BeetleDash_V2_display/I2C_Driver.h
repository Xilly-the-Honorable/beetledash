// I2C bus driver — based on Waveshare ESP32-S3-Touch-LCD-2.1 demo (I2C_Driver).
// BeetleDash addition: a FreeRTOS mutex serializing the shared bus, because the
// CST820 touch (LVGL, core 1) and the ADS1115/magnetometer (sensor task, core 0)
// all live on the same SDA=15/SCL=7 bus.
#pragma once
#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define I2C_SCL_PIN       7
#define I2C_SDA_PIN       15

void I2C_Init(void);

// Take/release the bus around every I2C transaction (leaf level only — do not nest).
void I2C_Lock(void);
void I2C_Unlock(void);

bool I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length);
bool I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length);
