#ifndef CAN_PING_H
#define CAN_PING_H

#include <Arduino.h>
#include "driver/twai.h"
#include "CAN_SUBs.h"

/*
  CANPing – CAN-Bus Ping/Pong zum Testen der Sende-/Empfangsgeschwindigkeit.

  Request-ID  = UPDATE_CMD_BASE - 10 + node_id = 0x1FFFFEF6 + node_id
  Response-ID = UPDATE_CMD_BASE -  9 + node_id = 0x1FFFFEF7 + node_id

  Protokoll:
    Request:  data[0] = Sequenznummer  (vom Sender beliebig gewählt)
              data[1..7] = beliebige Nutzlast (wird gespiegelt)
    Response: data[0] = Sequenznummer  (gespiegelt)
              data[1..7] = gespiegelte Nutzlast

  Der Sender misst die Zeit zwischen Request und Response → Roundtrip.
*/

namespace CANPing {

static constexpr uint32_t CANPING_CMD_BASE     = 0x1FFFFEF6UL; // OTA_BASE - 10
static constexpr uint32_t CANPING_RESP_OFFSET  = 1;             // Response = CMD_BASE + 1 + node_id

static bool enabled = false;
static bool fast_mode = false;
static uint8_t node_id = 0;
static uint32_t ping_count = 0;

static inline void init(uint8_t nodeId)
{
  node_id = nodeId;
}

static inline void setEnabled(bool en)
{
  enabled = en;
  if (!en)
  {
    fast_mode = false;
  }
  if (en)
  {
    Serial.printf("CANPing aktiviert. Request-ID:  0x%08X\n", (unsigned)(CANPING_CMD_BASE + node_id));
    Serial.printf("                  Response-ID: 0x%08X\n", (unsigned)(CANPING_CMD_BASE + CANPING_RESP_OFFSET + node_id));
  }
  else
  {
    Serial.println("CANPing deaktiviert.");
  }
}

static inline bool isEnabled()
{
  return enabled;
}

static inline void setFastMode(bool en)
{
  if (en)
  {
    enabled = true;
    fast_mode = true;
    Serial.println("CANPing Fast-Mode aktiviert: nur schnellste Ping-Antworten.");
    Serial.println("Beliebige Taste auf USB-Serial beendet Fast-Mode.");
  }
  else
  {
    fast_mode = false;
  }
}

static inline bool isFastMode()
{
  return fast_mode;
}

// In der Loop aufrufen nachdem ein Frame empfangen wurde.
static inline bool processMessage(uint32_t id, const uint8_t* data, uint8_t dlc)
{
  if (!enabled) return false;
  if (id != CANPING_CMD_BASE + node_id) return false;

  // Pong senden: Response-ID, gleiche Nutzlast gespiegelt
  uint32_t respId = CANPING_CMD_BASE + CANPING_RESP_OFFSET + node_id;
  uint8_t respData[8] = {0};
  uint8_t respDlc = dlc > 0 ? dlc : 1;
  for (uint8_t i = 0; i < respDlc; ++i)
  {
    respData[i] = data[i];
  }

  CAN_SendEx(true, respDlc, respId,
             respData[0], respData[1], respData[2], respData[3],
             respData[4], respData[5], respData[6], respData[7]);

  ping_count++;
  return true;
}

static inline uint32_t getPingCount()
{
  return ping_count;
}

static inline void resetPingCount()
{
  ping_count = 0;
}

static inline void stopFastModeAndDisable()
{
  fast_mode = false;
  enabled = false;
  Serial.printf("CANPing Fast-Mode beendet. Beantwortete Pings: %lu\n", (unsigned long)ping_count);
}

} // namespace CANPing

#endif // CAN_PING_H
