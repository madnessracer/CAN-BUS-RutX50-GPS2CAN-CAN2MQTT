#ifndef PCA9555_H
#define PCA9555_H

#include <Wire.h>
#include <Arduino.h>
#include "FileSystem.h"

static const uint8_t PCA9555_I2C_ADDRESS = 0x20;
static const uint8_t PCA9555_INT_PIN = 21;
static const char *PCA9555_STATE_PATH = "/pca_outputs.txt";

static uint8_t pca9555Address = PCA9555_I2C_ADDRESS;
static uint8_t pca9555OutputState = 0;
static uint8_t pca9555CachedInputPort0 = 0;
static uint8_t pca9555CachedInputPort1 = 0;
static bool pca9555CachedInputsValid = false;
static bool pca9555Initialized = false;

static inline bool PCA9555_LoadOutputState()
{
  if (!FS_Open())
    return false;

  readFile(LittleFS, PCA9555_STATE_PATH);
  if (FileBuffer[0] == '\0')
    return true;

  char *endptr = nullptr;
  unsigned long state = strtoul(FileBuffer, &endptr, 16);
  if (endptr == FileBuffer)
    return false;

  pca9555OutputState = (uint8_t)(state & 0xFF);
  return true;
}

static inline bool PCA9555_SaveOutputState()
{
  if (!FS_Open())
    return false;

  char buf[4];
  sprintf(buf, "%02X", pca9555OutputState);
  writeFile(LittleFS, PCA9555_STATE_PATH, buf);
  return true;
}

static inline bool PCA9555_WriteRegister(uint8_t reg, uint8_t value)
{
  Wire.beginTransmission(pca9555Address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static inline bool PCA9555_ReadRegister(uint8_t reg, uint8_t &value)
{
  Wire.beginTransmission(pca9555Address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0)
  {
    return false;
  }
  if (Wire.requestFrom(pca9555Address, (uint8_t)1) != 1)
  {
    return false;
  }
  value = Wire.read();
  return true;
}

static inline bool PCA9555_UpdateCachedInputs(bool &changed);

static inline bool PCA9555_Init(uint8_t address = PCA9555_I2C_ADDRESS)
{
  pca9555Address = address;
  pca9555Initialized = false;
  pca9555OutputState = 0;

  PCA9555_LoadOutputState();

  if (!PCA9555_WriteRegister(0x06, 0x00))
    return false;
  if (!PCA9555_WriteRegister(0x07, 0xFF))
    return false;
  if (!PCA9555_WriteRegister(0x04, 0x00))
    return false;
  if (!PCA9555_WriteRegister(0x05, 0x00))
    return false;
  if (!PCA9555_WriteRegister(0x02, pca9555OutputState))
    return false;

  pca9555Initialized = true;
  pca9555CachedInputsValid = false;
  bool dummy = false;
  PCA9555_UpdateCachedInputs(dummy);
  return true;
}

static inline bool PCA9555_IsInitialized()
{
  return pca9555Initialized;
}

static inline bool PCA9555_ReadInputs(uint8_t &port0, uint8_t &port1)
{
  if (!pca9555Initialized)
    return false;
  if (!PCA9555_ReadRegister(0x00, port0))
    return false;
  if (!PCA9555_ReadRegister(0x01, port1))
    return false;
  return true;
}

static inline bool PCA9555_ReadOutputs(uint8_t &port0, uint8_t &port1)
{
  if (!pca9555Initialized)
    return false;
  if (!PCA9555_ReadRegister(0x02, port0))
    return false;
  if (!PCA9555_ReadRegister(0x03, port1))
    return false;
  return true;
}

static inline bool PCA9555_IsInterruptActive()
{
  return digitalRead(PCA9555_INT_PIN) == LOW;
}

static inline bool PCA9555_UpdateCachedInputs(bool &changed)
{
  uint8_t port0 = 0;
  uint8_t port1 = 0;
  if (!PCA9555_ReadInputs(port0, port1))
    return false;

  changed = !pca9555CachedInputsValid || port0 != pca9555CachedInputPort0 || port1 != pca9555CachedInputPort1;
  pca9555CachedInputsValid = true;
  pca9555CachedInputPort0 = port0;
  pca9555CachedInputPort1 = port1;
  return true;
}

static inline bool PCA9555_ReadCachedInputs(uint8_t &port0, uint8_t &port1)
{
  if (!pca9555Initialized || !pca9555CachedInputsValid)
    return false;
  port0 = pca9555CachedInputPort0;
  port1 = pca9555CachedInputPort1;
  return true;
}

static inline bool PCA9555_CheckInterrupt(bool &changed)
{
  if (!pca9555Initialized)
    return false;

  if (!PCA9555_IsInterruptActive())
  {
    changed = false;
    return true;
  }

  return PCA9555_UpdateCachedInputs(changed);
}

static inline bool PCA9555_SetOutput(uint8_t outputPin, bool active)
{
  if (!pca9555Initialized || outputPin >= 8)
    return false;
  if (active)
  {
    pca9555OutputState |= (1 << outputPin);
  }
  else
  {
    pca9555OutputState &= ~(1 << outputPin);
  }
  if (!PCA9555_WriteRegister(0x02, pca9555OutputState))
    return false;

  PCA9555_SaveOutputState();
  return true;
}

static inline bool PCA9555_GetCachedOutputs(uint8_t &outputState)
{
  if (!pca9555Initialized)
    return false;
  outputState = pca9555OutputState;
  return true;
}

#endif // PCA9555_H
