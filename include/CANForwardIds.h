#ifndef CAN_FORWARD_IDS_H
#define CAN_FORWARD_IDS_H

#include <Arduino.h>
#include <Preferences.h>
#include <stdint.h>

static constexpr char CANFW_NVS_NAMESPACE[] = "canfw";
static constexpr char CANFW_COUNT_KEY[] = "count";
static constexpr char CANFW_ID_KEY_PREFIX[] = "id";
static constexpr uint8_t CANFW_MAX_IDS = 32;

inline uint32_t *canForwardIdsStorage()
{
  static uint32_t storage[CANFW_MAX_IDS] = {0};
  return storage;
}

inline uint8_t &canForwardIdsCountRef()
{
  static uint8_t count = 0;
  return count;
}

inline void canForwardIdsInit()
{
  Preferences prefs;
  if (!prefs.begin(CANFW_NVS_NAMESPACE, true))
  {
    Serial.println("CANFW: NVS init failed");
    return;
  }

  uint32_t count = prefs.getUInt(CANFW_COUNT_KEY, 0);
  if (count > CANFW_MAX_IDS)
  {
    count = CANFW_MAX_IDS;
  }

  uint32_t *canForwardIds = canForwardIdsStorage();
  uint8_t &canForwardIdsCount = canForwardIdsCountRef();
  canForwardIdsCount = 0;
  for (uint32_t i = 0; i < count; ++i)
  {
    char key[16];
    snprintf(key, sizeof(key), "%s%u", CANFW_ID_KEY_PREFIX, (unsigned)i);
    canForwardIds[i] = prefs.getUInt(key, 0);
    if (canForwardIds[i] != 0)
    {
      canForwardIdsCount++;
    }
  }

  prefs.end();
}

inline bool canForwardIdsAdd(uint32_t messageId)
{
  uint32_t *canForwardIds = canForwardIdsStorage();
  uint8_t &canForwardIdsCount = canForwardIdsCountRef();

  for (uint8_t i = 0; i < canForwardIdsCount; ++i)
  {
    if (canForwardIds[i] == messageId)
    {
      Serial.printf("CANFW: ID 0x%lX already stored\n", (unsigned long)messageId);
      return false;
    }
  }

  if (canForwardIdsCount >= CANFW_MAX_IDS)
  {
    Serial.println("CANFW: maximum ID count reached");
    return false;
  }

  canForwardIds[canForwardIdsCount] = messageId;
  canForwardIdsCount++;

  Preferences prefs;
  if (!prefs.begin(CANFW_NVS_NAMESPACE, false))
  {
    Serial.println("CANFW: NVS write init failed");
    return false;
  }

  prefs.putUInt(CANFW_COUNT_KEY, canForwardIdsCount);
  char key[16];
  snprintf(key, sizeof(key), "%s%u", CANFW_ID_KEY_PREFIX, (unsigned)(canForwardIdsCount - 1));
  prefs.putUInt(key, messageId);
  prefs.end();

  Serial.printf("CANFW: added ID 0x%lX\n", (unsigned long)messageId);
  return true;
}

inline bool canForwardIdsRemove(uint32_t messageId)
{
  uint32_t *canForwardIds = canForwardIdsStorage();
  uint8_t &canForwardIdsCount = canForwardIdsCountRef();

  int index = -1;
  for (uint8_t i = 0; i < canForwardIdsCount; ++i)
  {
    if (canForwardIds[i] == messageId)
    {
      index = i;
      break;
    }
  }
  if (index < 0)
  {
    Serial.printf("CANFW: ID 0x%lX not found\n", (unsigned long)messageId);
    return false;
  }

  for (uint8_t i = index; i + 1 < canForwardIdsCount; ++i)
  {
    canForwardIds[i] = canForwardIds[i + 1];
  }
  canForwardIdsCount--;

  Preferences prefs;
  if (!prefs.begin(CANFW_NVS_NAMESPACE, false))
  {
    Serial.println("CANFW: NVS write init failed");
    return false;
  }

  prefs.putUInt(CANFW_COUNT_KEY, canForwardIdsCount);
  char key[16];
  for (uint8_t i = 0; i < canForwardIdsCount; ++i)
  {
    snprintf(key, sizeof(key), "%s%u", CANFW_ID_KEY_PREFIX, (unsigned)i);
    prefs.putUInt(key, canForwardIds[i]);
  }
  prefs.end();

  Serial.printf("CANFW: removed ID 0x%lX\n", (unsigned long)messageId);
  return true;
}

inline void canForwardIdsPrint()
{
  uint32_t *canForwardIds = canForwardIdsStorage();
  uint8_t &canForwardIdsCount = canForwardIdsCountRef();

  if (canForwardIdsCount == 0)
  {
    Serial.println("CANFW: no forward IDs configured");
    return;
  }

  Serial.printf("CANFW: configured serial forward IDs (%u):\n", (unsigned)canForwardIdsCount);
  for (uint8_t i = 0; i < canForwardIdsCount; ++i)
  {
    Serial.printf("  [%u] 0x%lX\n", (unsigned)i, (unsigned long)canForwardIds[i]);
  }
}

inline bool canForwardIdAllowed(uint32_t messageId)
{
  uint32_t *canForwardIds = canForwardIdsStorage();
  uint8_t &canForwardIdsCount = canForwardIdsCountRef();

  for (uint8_t i = 0; i < canForwardIdsCount; ++i)
  {
    if (canForwardIds[i] == messageId)
    {
      return true;
    }
  }
  return false;
}

#endif // CAN_FORWARD_IDS_H
