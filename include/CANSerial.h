#ifndef CAN_SERIAL_H
#define CAN_SERIAL_H

#include <Arduino.h>
#include <driver/twai.h>
#include <ErrorLog.h>
#include <CAN_SUBs.h>
#include "Can-Bus IDs.h"
#include "WebTerminal.h"
#include <cstring>
#include <cstdlib>

extern HardwareSerial *can_tx_ser;
extern HardwareSerial *can_rx_ser;
extern CanLiveMode canLiveMode;

static inline bool canLiveSerialEnabled()
{
  return canLiveMode == CAN_LIVE_SERIAL || canLiveMode == CAN_LIVE_ALL;
}

static inline bool canLiveTwaiEnabled()
{
  return canLiveMode == CAN_LIVE_TWAI || canLiveMode == CAN_LIVE_ALL;
}

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
  char lineBuf[48];
  size_t pos = snprintf(lineBuf, sizeof(lineBuf), "0x%lX;%u;", frame.identifier, frame.data_length_code);
  for (uint8_t i = 0; i < frame.data_length_code && pos + 2 < sizeof(lineBuf) - 1; ++i)
  {
    int written = snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "%02X", frame.data[i]);
    if (written < 0)
      break;
    pos += (size_t)written;
  }
  lineBuf[pos] = '\0';

  if (canLiveSerialEnabled())
  {
    Serial.print("[CAN SERIAL TX] ");
    Serial.println(lineBuf);
    webTerminalAppendFormat("[CAN SERIAL TX] %s", lineBuf);
  }

  can_tx_ser->print("0x");
  can_tx_ser->print((unsigned long)frame.identifier, HEX);
  can_tx_ser->print(';');
  can_tx_ser->print(frame.data_length_code);
  can_tx_ser->print(';');
  for (uint8_t i = 0; i < frame.data_length_code; ++i)
  {
    if (frame.data[i] < 0x10)
      can_tx_ser->print('0');
    can_tx_ser->print(frame.data[i], HEX);
  }
  can_tx_ser->print('\n');
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
  }

  // An den CAN-Bus weiterleiten, wenn es kein OTA-Kommando ist
  if (shouldForward)
  {
    CAN_SendEx(true, dlc, messageId,
               data[0], data[1], data[2], data[3],
               data[4], data[5], data[6], data[7]);
  }
  lineLen = 0;
}

#endif // CAN_SERIAL_H
