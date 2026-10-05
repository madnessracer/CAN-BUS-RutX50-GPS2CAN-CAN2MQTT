#ifndef CAN_SERIAL_H
#define CAN_SERIAL_H

#include <Arduino.h>
#include <driver/twai.h>
#include <ErrorLog.h>
#include <CAN_SUBs.h>
#include "Can-Bus IDs.h"
#include "WebTerminal.h"
#include "PCA9555.h"
#include "SerialBridge.h"
#include <cstring>
#include <cstdlib>

extern HardwareSerial *can_tx_ser;
extern HardwareSerial *can_rx_ser;
extern CanLiveMode canLiveMode;

class SerialBridgeClass;
extern SerialBridgeClass SerialBridge;

static inline bool canLiveSerialEnabled()
{
  return canLiveMode == CAN_LIVE_SERIAL || canLiveMode == CAN_LIVE_ALL;
}

static inline bool canLiveTwaiEnabled()
{
  return canLiveMode == CAN_LIVE_TWAI || canLiveMode == CAN_LIVE_ALL;
}

static constexpr size_t CAN_SERIAL_TX_QUEUE_SIZE = 32;
static constexpr size_t CAN_SERIAL_TX_LINE_SIZE = 64;
static char canSerialTxQueue[CAN_SERIAL_TX_QUEUE_SIZE][CAN_SERIAL_TX_LINE_SIZE];
static uint8_t canSerialTxQueueHead = 0;
static uint8_t canSerialTxQueueTail = 0;
static uint8_t canSerialTxQueueCount = 0;
static unsigned long canSerialTxLastMillis = 0;

inline bool canSerialTxQueueIsFull()
{
  return canSerialTxQueueCount >= CAN_SERIAL_TX_QUEUE_SIZE;
}

inline bool canSerialTxQueueIsEmpty()
{
  return canSerialTxQueueCount == 0;
}

inline bool canSerialTxEnqueue(const twai_message_t &frame)
{
  if (canSerialTxQueueIsFull())
  {
    return false;
  }

  char lineBuf[CAN_SERIAL_TX_LINE_SIZE];
  size_t pos = snprintf(lineBuf, sizeof(lineBuf), "0x%lX;%u;", frame.identifier, frame.data_length_code);
  for (uint8_t i = 0; i < frame.data_length_code && pos + 2 < sizeof(lineBuf) - 1; ++i)
  {
    int written = snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "%02X", frame.data[i]);
    if (written < 0)
      break;
    pos += (size_t)written;
  }
  if (pos + 1 < sizeof(lineBuf))
  {
    lineBuf[pos++] = '\n';
    lineBuf[pos] = '\0';
  }

  memcpy(canSerialTxQueue[canSerialTxQueueTail], lineBuf, pos + 1);
  canSerialTxQueueTail = (canSerialTxQueueTail + 1) % CAN_SERIAL_TX_QUEUE_SIZE;
  canSerialTxQueueCount++;
  return true;
}

inline void processCanSerialTxQueue()
{
  if (canSerialTxQueueIsEmpty())
  {
    return;
  }

  unsigned long now = millis();
  if (now - canSerialTxLastMillis < 5)
  {
    return;
  }

  char *line = canSerialTxQueue[canSerialTxQueueHead];
  size_t len = strlen(line);
  if (len == 0)
  {
    canSerialTxQueueHead = (canSerialTxQueueHead + 1) % CAN_SERIAL_TX_QUEUE_SIZE;
    canSerialTxQueueCount--;
    canSerialTxLastMillis = now;
    return;
  }

  if (canLiveSerialEnabled())
  {
    char lastChar = line[len - 1];
    if (lastChar == '\n')
    {
      line[len - 1] = '\0';
    }
    Serial.print("[CAN SERIAL TX] ");
    Serial.println(line);
    webTerminalAppendFormat("[CAN SERIAL TX] %s", line);
    if (lastChar == '\n')
    {
      line[len - 1] = lastChar;
    }
  }

  can_tx_ser->write((const uint8_t *)line, len);

  canSerialTxQueueHead = (canSerialTxQueueHead + 1) % CAN_SERIAL_TX_QUEUE_SIZE;
  canSerialTxQueueCount--;
  canSerialTxLastMillis = now;
}

inline bool parseHexId(const char *text, uint32_t &messageId)
{
  if (text == nullptr || text[0] == '\0')
  {
    return false;
  }

  char *endptr = nullptr;
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
  {
    messageId = (uint32_t)strtoul(text + 2, &endptr, 16);
  }
  else
  {
    messageId = (uint32_t)strtoul(text, &endptr, 10);
  }

  if (endptr == nullptr || *endptr != '\0')
  {
    return false;
  }

  if (messageId > 0x1FFFFFFF)
  {
    return false;
  }

  return true;
}

void canForwardIdsInit();
bool canForwardIdsAdd(uint32_t messageId);
bool canForwardIdsRemove(uint32_t messageId);
void canForwardIdsPrint();
bool canForwardIdAllowed(uint32_t messageId);

void OTA_Start();
void OTA_Stop();

static inline bool parseHexByte(const char *hex, uint8_t &value)
{
  if (!isxdigit((unsigned char)hex[0]) || !isxdigit((unsigned char)hex[1]))
  {
    return false;
  }

  char tmp[3] = {hex[0], hex[1], '\0'};
  char *endptr = nullptr;
  unsigned long v = strtoul(tmp, &endptr, 16);
  if (endptr == nullptr || *endptr != '\0' || v > 0xFF)
  {
    return false;
  }
  value = (uint8_t)v;
  return true;
}

inline bool parseCanMessageLine(const char *line, uint32_t &messageId, uint8_t &dlc, uint8_t data[8])
{
  if (line == nullptr)
  {
    return false;
  }

  if (!(line[0] == '0' && (line[1] == 'x' || line[1] == 'X')))
  {
    return false;
  }

  const char *sep1 = strchr(line, ';');
  if (sep1 == nullptr || sep1 - line < 3)
  {
    return false;
  }

  const char *sep2 = strchr(sep1 + 1, ';');
  if (sep2 == nullptr || sep2 - sep1 < 2)
  {
    return false;
  }

  if (sep1 - line - 2 > 8)
  {
    return false;
  }

  char idStr[11] = {0};
  size_t idLen = sep1 - line - 2;
  if (idLen >= sizeof(idStr))
  {
    return false;
  }
  memcpy(idStr, line + 2, idLen);

  messageId = (uint32_t)strtoul(idStr, NULL, 16);
  if (messageId > 0x1FFFFFFF)
  {
    return false;
  }

  char dlcStr[4] = {0};
  size_t dlcLen = sep2 - sep1 - 1;
  if (dlcLen == 0 || dlcLen >= sizeof(dlcStr))
  {
    return false;
  }
  memcpy(dlcStr, sep1 + 1, dlcLen);

  long dlcValue = strtol(dlcStr, NULL, 10);
  if (dlcValue < 1 || dlcValue > 8)
  {
    return false;
  }
  dlc = (uint8_t)dlcValue;

  const char *dataStr = sep2 + 1;
  size_t dataLen = strlen(dataStr);
  if (dataLen != dlc * 2)
  {
    return false;
  }

  for (uint8_t i = 0; i < dlc; ++i)
  {
    if (!parseHexByte(dataStr + i * 2, data[i]))
    {
      return false;
    }
  }

  return true;
}

inline bool parseCanMessageLine(const String &line, uint32_t &messageId, uint8_t &dlc, uint8_t data[8])
{
  return parseCanMessageLine(line.c_str(), messageId, dlc, data);
}

void startDebugMode();

inline void logCanTwaiFrame(const twai_message_t &frame)
{
  char lineBuf[64];
  size_t pos = snprintf(lineBuf, sizeof(lineBuf), "0x%lX;%u;", frame.identifier, frame.data_length_code);
  for (uint8_t i = 0; i < frame.data_length_code && pos + 2 < sizeof(lineBuf) - 1; ++i)
  {
    int written = snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "%02X", frame.data[i]);
    if (written < 0)
      break;
    pos += (size_t)written;
  }
  lineBuf[pos] = '\0';

  if (!canLiveModeWeb)
  {
    Serial.print("[CAN TWAI RX] ");
    Serial.println(lineBuf);
  }
  webTerminalAppendFormat("[CAN TWAI RX] %s", lineBuf);
}

inline void sendCanMessageToCanTxSer(const twai_message_t &frame)
{
  if (!canSerialTxEnqueue(frame))
  {
    return;
  }
}

inline void canSerialForwardFrame(const twai_message_t &frame)
{
  sendCanMessageToCanTxSer(frame);
}

inline void handleSerialCanInput()
{
  static char lineBuf[128];
  static size_t lineLen = 0;

  while (can_rx_ser->available())
  {
    char c = (char)can_rx_ser->read();
    if (c == '\r')
    {
      continue;
    }
    if (c == '\n')
    {
      if (lineLen == 0)
      {
        continue;
      }
      lineBuf[lineLen] = '\0';
      break;
    }
    if (lineLen < sizeof(lineBuf) - 1)
    {
      lineBuf[lineLen++] = c;
    }
    else
    {
      lineLen = 0;
    }
  }

  if (lineLen == 0 || lineBuf[lineLen] != '\0')
  {
    return;
  }

  const char *line = lineBuf;

  if (canLiveSerialEnabled())
  {
    if (!canLiveModeWeb)
    {
      Serial.printf("[CAN SERIAL RX] %s\n", line);
    }
    webTerminalAppendFormat("[CAN SERIAL RX] %s", line);
  }

  if (strcasecmp(line, "debug") == 0)
  {
    lineLen = 0;
    startDebugMode();
    return;
  }

  uint32_t messageId;
  uint8_t dlc;
  uint8_t data[8] = {0};
  if (!parseCanMessageLine(line, messageId, dlc, data))
  {
    errorLogAddOnce("CAN_PARSE_FAIL", "Ungueltiges CAN-Serial-Format");
    lineLen = 0;
    return;
  }

  if (errorLogIsActive("CAN_PARSE_FAIL"))
  {
    errorLogResolve("CAN_PARSE_FAIL");
  }

  bool shouldForward = true; // Standardmäßig weiterleiten, außer es handelt sich um ein OTA-Kommando
  
  // Spezieller Check für SteuerID-Kommandos, um OTA und WebTerminal lokal zu steuern
  if (messageId == SteuerID)
  {
    if (dlc >= 2 && data[0] == 0x01 && data[1] == 0x00)
    {
      OTA_Stop();
      shouldForward = false;
    }
    else if (dlc >= 2 && data[0] == 0x01 && data[1] == 0x01)
    {
      OTA_Start();
      shouldForward = false;
    }
    else if (dlc >= 2 && data[0] == 0x01 && data[1] == 0x02)
    {
      webTerminalSetEnabled(false);
      shouldForward = false;
    }
    else if (dlc >= 2 && data[0] == 0x01 && data[1] == 0x03)
    {
      webTerminalSetEnabled(true);
      shouldForward = false;
    }
    else if (dlc >= 2 && data[0] == 0x01 && data[1] == 0x05)
    {
      SerialBridge.stopBridge();
      shouldForward = false;
    }
    else if (dlc >= 2 && data[0] == 0x01 && data[1] == 0x06)
    {
      if (!SerialBridge.beginBridge())
      {
        errorLogAddOnce("CAN_BRIDGE_FAIL", "SerialBridge start failed");
      }
      shouldForward = false;
    }
  }
  else if (messageId == FetSteuerID)
  {
    if (dlc >= 2)
    {
      uint8_t fetNum = data[0];
      uint8_t fetState = data[1];
      if (fetNum >= 1 && fetNum <= 8 && (fetState == 0x00 || fetState == 0x01))
      {
        if (!PCA9555_SetOutput(fetNum - 1, fetState == 0x01))
        {
          errorLogAddOnce("CAN_FET_FAIL", "FET CAN-Steuerung fehlgeschlagen");
        }
        shouldForward = false;
      }
      else
      {
        errorLogAddOnce("CAN_FET_FAIL", "Ungueltige FET CAN-Steuerungsdaten");
      }
    }
  }

  // An den CAN-Bus weiterleiten, wenn es kein OTA- oder FET-Kommando ist
  if (shouldForward)
  {
    CAN_SendEx(true, dlc, messageId,
               data[0], data[1], data[2], data[3],
               data[4], data[5], data[6], data[7]);
  }
  lineLen = 0;
}

#endif // CAN_SERIAL_H
