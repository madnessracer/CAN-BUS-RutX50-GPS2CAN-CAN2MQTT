#ifndef _433_EMPANG_H
#define _433_EMPANG_H

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <RCSwitch.h>

inline RCSwitch rcSwitchReceiver;
inline unsigned long rcSwitchLastValue = 0;
inline unsigned long rcSwitchLastReceivedAtMs = 0;
inline uint8_t rcSwitchLastDevice = 0;
inline bool rcSwitchLastValueValid = false;
inline portMUX_TYPE rcSwitchLock = portMUX_INITIALIZER_UNLOCKED;

inline constexpr unsigned long RCSWITCH_DUPLICATE_FILTER_MS = 500;
inline unsigned long rcSwitchLastDedupValue = 0;
inline unsigned long rcSwitchLastDedupAtMs = 0;

inline constexpr char RCSWITCH_NVS_NAMESPACE[] = "rcswitch";
inline constexpr char RCSWITCH_DEVICE_KEY_PREFIX[] = "dev";
inline constexpr uint8_t RCSWITCH_MAX_DEVICES = 255;
inline uint32_t rcSwitchDeviceCodes[RCSWITCH_MAX_DEVICES + 1] = {0};

inline uint8_t rcSwitchFindDeviceByCode(uint32_t code);

inline void rcswitchReceiveTask(void *pvParameters)
{
  for (;;)
  {
    if (rcSwitchReceiver.available())
    {
      unsigned long value = rcSwitchReceiver.getReceivedValue();
      unsigned long receivedAtMs = millis();

      bool accept = true;
      if (value != 0 && value == rcSwitchLastDedupValue && (receivedAtMs - rcSwitchLastDedupAtMs) < RCSWITCH_DUPLICATE_FILTER_MS)
      {
        accept = false;
      }

      rcSwitchLastDedupValue = value;
      rcSwitchLastDedupAtMs = receivedAtMs;

      if (accept)
      {
        uint8_t device = rcSwitchFindDeviceByCode(value);
        portENTER_CRITICAL(&rcSwitchLock);
        rcSwitchLastValue = value;
        rcSwitchLastDevice = device;
        rcSwitchLastReceivedAtMs = receivedAtMs;
        rcSwitchLastValueValid = true;
        portEXIT_CRITICAL(&rcSwitchLock);
      }

      rcSwitchReceiver.resetAvailable();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

inline void rcSwitchDeviceMapInit()
{
  Preferences prefs;
  if (!prefs.begin(RCSWITCH_NVS_NAMESPACE, true))
  {
    Serial.println("RCSWITCH: NVS init failed");
    return;
  }

  for (uint8_t device = 1; device <= RCSWITCH_MAX_DEVICES; ++device)
  {
    char key[16];
    snprintf(key, sizeof(key), "%s%u", RCSWITCH_DEVICE_KEY_PREFIX, (unsigned)device);
    rcSwitchDeviceCodes[device] = prefs.getUInt(key, 0);
  }

  prefs.end();
}

inline bool rcSwitchDeviceMapSet(uint8_t device, uint32_t code)
{
  if (device == 0 || device > RCSWITCH_MAX_DEVICES)
  {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(RCSWITCH_NVS_NAMESPACE, false))
  {
    Serial.println("RCSWITCH: NVS write init failed");
    return false;
  }

  char key[16];
  snprintf(key, sizeof(key), "%s%u", RCSWITCH_DEVICE_KEY_PREFIX, (unsigned)device);
  prefs.putUInt(key, code);
  prefs.end();

  rcSwitchDeviceCodes[device] = code;
  return true;
}

inline bool rcSwitchDeviceMapRemove(uint8_t device)
{
  if (device == 0 || device > RCSWITCH_MAX_DEVICES)
  {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(RCSWITCH_NVS_NAMESPACE, false))
  {
    Serial.println("RCSWITCH: NVS write init failed");
    return false;
  }

  char key[16];
  snprintf(key, sizeof(key), "%s%u", RCSWITCH_DEVICE_KEY_PREFIX, (unsigned)device);
  prefs.putUInt(key, 0);
  prefs.end();

  rcSwitchDeviceCodes[device] = 0;
  return true;
}

inline uint8_t rcSwitchFindDeviceByCode(uint32_t code)
{
  if (code == 0)
  {
    return 0;
  }

  for (uint8_t device = 1; device <= RCSWITCH_MAX_DEVICES; ++device)
  {
    if (rcSwitchDeviceCodes[device] == code)
    {
      return device;
    }
  }

  return 0;
}

inline void rcSwitchDeviceMapPrint()
{
  bool any = false;
  for (uint8_t device = 1; device <= RCSWITCH_MAX_DEVICES; ++device)
  {
    if (rcSwitchDeviceCodes[device] != 0)
    {
      if (!any)
      {
        Serial.println("RCSWITCH: device map:");
        any = true;
      }
      Serial.printf("  Device %u -> Code %lu\n", device, (unsigned long)rcSwitchDeviceCodes[device]);
    }
  }

  if (!any)
  {
    Serial.println("RCSWITCH: device map is empty.");
  }
}

#endif // _433_EMPANG_H
