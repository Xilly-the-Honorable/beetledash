#include "I2C_Driver.h"

static SemaphoreHandle_t i2cMutex = NULL;

void I2C_Init(void) {
  if (i2cMutex == NULL) i2cMutex = xSemaphoreCreateMutex();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
}

void I2C_Lock(void) {
  if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY);
}

void I2C_Unlock(void) {
  if (i2cMutex) xSemaphoreGive(i2cMutex);
}

bool I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length)
{
  I2C_Lock();
  Wire.beginTransmission(Driver_addr);
  Wire.write(Reg_addr);
  if ( Wire.endTransmission(true)){
    I2C_Unlock();
    printf("The I2C transmission fails. - I2C Read\r\n");
    return -1;
  }
  Wire.requestFrom(Driver_addr, Length);
  for (int i = 0; i < Length; i++) {
    *Reg_data++ = Wire.read();
  }
  I2C_Unlock();
  return 0;
}

bool I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length)
{
  I2C_Lock();
  Wire.beginTransmission(Driver_addr);
  Wire.write(Reg_addr);
  for (int i = 0; i < Length; i++) {
    Wire.write(*Reg_data++);
  }
  if ( Wire.endTransmission(true))
  {
    I2C_Unlock();
    printf("The I2C transmission fails. - I2C Write\r\n");
    return -1;
  }
  I2C_Unlock();
  return 0;
}
