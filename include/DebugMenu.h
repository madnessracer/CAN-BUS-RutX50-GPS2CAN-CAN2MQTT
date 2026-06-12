#ifndef DEBUG_MENU_H
#define DEBUG_MENU_H

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <time.h>
#if defined(__has_include)
#  if __has_include(<timezonedb_lookup.h>)
#    include <timezonedb_lookup.h>
#    define DEBUG_MENU_HAS_TIMEZONE_LOOKUP 1
#  else
#    define DEBUG_MENU_HAS_TIMEZONE_LOOKUP 0
#  endif
#else
#  define DEBUG_MENU_HAS_TIMEZONE_LOOKUP 0
#endif
#include "Wlan_Config.h"
#include "Ws2812.h"
#include "ErrorLog.h"
#include "CAN_SUBs.h"
#include "PCA9555.h"
#include "GpsState.h"
#include "WebTerminal.h"
#include "GPSSerial.h"
#include "ZoneDetectTask.h"
#include "UnixTimeClock.h"
#include "CANPing.h"

static inline bool debugMenuParseGpsUtcSeconds(uint32_t &outUtcSeconds)
{
  return unixTimeClockParseGpsUtcDateTime(outUtcSeconds);
}

static inline void debugMenuFormatUtcOffset(long seconds, char *out, size_t outSize)
{
  if (outSize == 0)
    return;

  int hours = (int)(seconds / 3600);
  int minutes = (int)(abs(seconds % 3600) / 60);
  snprintf(out, outSize, "UTC%+03d:%02d", hours, minutes);
}

static inline void debugMenuFormatLocalTime(const struct tm &tmInfo, char *out, size_t outSize)
{
  if (outSize == 0)
    return;
  snprintf(out, outSize, "%04d-%02d-%02d %02d:%02d:%02d",
           tmInfo.tm_year + 1900,
           tmInfo.tm_mon + 1,
           tmInfo.tm_mday,
           tmInfo.tm_hour,
           tmInfo.tm_min,
           tmInfo.tm_sec);
}

static inline bool debugMenuComputeTimezoneOffset(const char *zoneName, time_t utcTime, long &outOffsetSeconds, bool &outDstActive, char *outPosixRule, size_t outPosixRuleSize, struct tm *outLocalTm)
{
#if DEBUG_MENU_HAS_TIMEZONE_LOOKUP
  if (zoneName == nullptr || zoneName[0] == '\0')
  {
    return false;
  }

  const char *posixTz = lookup_posix_timezone_tz(zoneName);
  if (posixTz == nullptr)
  {
    return false;
  }

  if (outPosixRule && outPosixRuleSize > 0)
  {
    strncpy(outPosixRule, posixTz, outPosixRuleSize - 1);
    outPosixRule[outPosixRuleSize - 1] = '\0';
  }

  if (!unixTimeClockComputeOffsetFromZone(zoneName, utcTime, outOffsetSeconds, outDstActive))
  {
    return false;
  }

  if (outLocalTm != nullptr)
  {
    time_t localEpoch = utcTime;
    if (outOffsetSeconds >= 0)
    {
      localEpoch = (time_t)((uint32_t)utcTime + (uint32_t)outOffsetSeconds);
    }
    else
    {
      localEpoch = (time_t)((uint32_t)utcTime - (uint32_t)(-outOffsetSeconds));
    }
    unixTimeClockUtcBreakdown(localEpoch, *outLocalTm);
  }
  return true;
#else
  if (outPosixRule && outPosixRuleSize > 0)
  {
    outPosixRule[0] = '\0';
  }
  (void)zoneName;
  (void)utcTime;
  (void)outOffsetSeconds;
  (void)outDstActive;
  (void)outLocalTm;
  return false;
#endif
}

static inline uint32_t debugMenuGetEffectiveLocalUnix(uint32_t utcSeconds, long &outOffsetSeconds, bool &outDstActive, char *outTimezone, size_t outTimezoneSize)
{
  uint32_t localSeconds = unixTimeClockGetLocal();
  outOffsetSeconds = unixTimeClockOffsetSeconds();
  outDstActive = false;
  if (outTimezone && outTimezoneSize > 0)
  {
    char country[64] = "";
    bool valid = false;
    bool hasGpsPosition = false;
    unsigned long lastUpdateMs = 0;
    float latitude = 0.0f;
    float longitude = 0.0f;
    if (zoneDetectGetStatus(outTimezone, outTimezoneSize, country, sizeof(country), valid, hasGpsPosition, lastUpdateMs, latitude, longitude) && valid && outTimezone[0] != '\0')
    {
      long computedOffset = 0;
      bool computedDstActive = false;
      if (debugMenuComputeTimezoneOffset(outTimezone, (time_t)utcSeconds, computedOffset, computedDstActive, nullptr, 0, nullptr))
      {
        outOffsetSeconds = computedOffset;
        outDstActive = computedDstActive;
        if (computedOffset >= 0)
        {
          localSeconds = utcSeconds + (uint32_t)computedOffset;
        }
        else
        {
          localSeconds = utcSeconds - (uint32_t)(-computedOffset);
        }
      }
    }
    else
    {
      outTimezone[0] = '\0';
    }
  }
  return localSeconds;
}

enum DebugMenuState
{
  DEBUG_OFF,
  DEBUG_MAIN,
  DEBUG_WIFI,
  DEBUG_CAN_LIVE,
  DEBUG_GPS_LIVE,
  DEBUG_TIME,
  DEBUG_PCA_LIVE
};

extern bool gpsLiveRawModeEnabled;

enum WifiSubState
{
  WIFI_IDLE,
  WIFI_WAIT_SELECTION,
  WIFI_WAIT_PASSWORD
};

static DebugMenuState debugMenuState = DEBUG_OFF;
static WifiSubState wifiSubState = WIFI_IDLE;
static bool debugPromptShown = false;
static bool wifiPromptShown = false;
static int wifiChoice = 0;
static String wifiPasswordBuffer;
static String wifiPendingSSID;
static constexpr unsigned long DEFAULT_DEBUG_AUTOEXIT_MINUTES = 5;
static unsigned long debugAutoExitInterval = 0;
static unsigned long debugAutoExitStart = 0;

void clearTerminalScreen()
{
  uint8_t clearByte = 0x00;
  Serial.write(&clearByte, 1);
  Serial.flush();
}

static void debugMenuTrimWhitespace(char *text)
{
  if (text == NULL)
    return;

  size_t start = 0;
  while (text[start] != '\0' && isspace((unsigned char)text[start]))
  {
    start++;
  }

  size_t end = strlen(text);
  while (end > start && isspace((unsigned char)text[end - 1]))
  {
    end--;
  }

  if (start > 0)
  {
    memmove(text, text + start, end - start);
  }
  text[end - start] = '\0';
}

static bool readSerialLine(Stream &stream, char *buffer, size_t bufferSize, size_t &pos)
{
  while (stream.available())
  {
    int c = stream.read();
    if (c < 0)
      break;
    if (c == '\r')
      continue;
    if (c == '\n')
    {
      if (pos == 0)
        continue;
      buffer[pos] = '\0';
      pos = 0;
      return true;
    }
    if (pos < bufferSize - 1)
    {
      buffer[pos++] = (char)c;
    }
    else
    {
      pos = 0;
    }
  }
  return false;
}

static int splitTokens(char *text, char *tokens[], int maxTokens)
{
  int count = 0;
  char *token = strtok(text, " ");
  while (token != nullptr && count < maxTokens)
  {
    tokens[count++] = token;
    token = strtok(nullptr, " ");
  }
  return count;
}

static bool readSerialCommand(char *cmd, size_t maxLen)
{
  static char lineBuf[128];
  static size_t linePos = 0;
  if (!readSerialLine(Serial, lineBuf, sizeof(lineBuf), linePos))
  {
    return false;
  }
  debugMenuTrimWhitespace(lineBuf);
  if (lineBuf[0] == '\0')
  {
    return false;
  }
  size_t i = 0;
  for (; lineBuf[i] != '\0' && i < maxLen - 1; ++i)
  {
    cmd[i] = tolower((unsigned char)lineBuf[i]);
  }
  cmd[i] = '\0';
  return true;
}

static bool readSerialText(char *text, size_t maxLen)
{
  static char lineBuf[128];
  static size_t linePos = 0;
  if (!readSerialLine(Serial, lineBuf, sizeof(lineBuf), linePos))
  {
    return false;
  }
  debugMenuTrimWhitespace(lineBuf);
  if (lineBuf[0] == '\0')
  {
    return false;
  }
  strncpy(text, lineBuf, maxLen - 1);
  text[maxLen - 1] = '\0';
  return true;
}

void saveDebugAutoExitSetting(unsigned long minutes);
void loadDebugAutoExitSetting();
extern HardwareSerial *can_tx_ser;
extern HardwareSerial *can_rx_ser;
bool parseCanMessageLine(const String &line, uint32_t &messageId, uint8_t &dlc, uint8_t data[8]);
void processCanLiveMode();
void processGpsLiveMode();
inline void processTimeLiveMode();
extern CanLiveMode canLiveMode;
extern bool gpsLiveMode;
void printDebugHelp();
inline void processTimeLiveMode()
{
  if (!debugPromptShown)
  {
    clearTerminalScreen();
    Serial.print("\n=== Zeit Live Mode ===\n");
    Serial.print("Zeige aktuelle Zeit. Beliebige Taste zum Beenden.\n");
    Serial.print("========================\n");
    debugPromptShown = true;
  }

  if (Serial.available() > 0)
  {
    while (Serial.available() > 0)
    {
      Serial.read();
    }
    debugMenuState = DEBUG_MAIN;
    debugPromptShown = false;
    Serial.print("Zeit Live Mode beendet.\n");
    printDebugHelp();
    return;
  }

  unsigned long now = millis();
  static unsigned long lastUpdateMillis = 0;
  if (lastUpdateMillis != 0 && now - lastUpdateMillis < 1000)
  {
    return;
  }
  lastUpdateMillis = now;

  clearTerminalScreen();
  Serial.print("\n=== Zeit Live Mode ===\n");

  unsigned long uptimeMillis = now;
  unsigned long uptimeSeconds = uptimeMillis / 1000UL;
  unsigned long uptimeDays = uptimeSeconds / 86400UL;
  unsigned long uptimeHours = (uptimeSeconds % 86400UL) / 3600UL;
  unsigned long uptimeMinutes = (uptimeSeconds % 3600UL) / 60UL;
  unsigned long uptimeSecondsOnly = uptimeSeconds % 60UL;
  Serial.printf("System Uptime: %lu Tage %02lu:%02lu:%02lu\n", uptimeDays, uptimeHours, uptimeMinutes, uptimeSecondsOnly);

  Serial.print("Zeige aktuelle Zeit. Beliebige Taste zum Beenden.\n");
  Serial.print("========================\n");
  if (unixTimeClockIsInitialized())
  {
    uint32_t unixSeconds = unixTimeClockGet();
    long offsetSeconds = 0;
    bool dstActive = false;
    char timezone[64] = "";
    uint32_t localUnixSeconds = debugMenuGetEffectiveLocalUnix(unixSeconds, offsetSeconds, dstActive, timezone, sizeof(timezone));
    uint32_t seconds = unixSeconds % 60;
    uint32_t minutes = (unixSeconds / 60) % 60;
    uint32_t hours = (unixSeconds / 3600) % 24;
    uint32_t localSeconds = localUnixSeconds % 60;
    uint32_t localMinutes = (localUnixSeconds / 60) % 60;
    uint32_t localHours = (localUnixSeconds / 3600) % 24;
    char offsetStr[16] = "";
    debugMenuFormatUtcOffset(offsetSeconds, offsetStr, sizeof(offsetStr));
    Serial.printf("UTC Zeit: %02lu:%02lu:%02lu\n", hours, minutes, seconds);
    if (timezone[0] != '\0')
    {
      Serial.printf("Timezone : %s\n", timezone);
    }
    else
    {
      Serial.print("Timezone : nicht verfuegbar\n");
    }
    Serial.printf("Sommerzeit: %s\n", dstActive ? "ja" : "nein");
    Serial.printf("UTC Offset: %s\n", offsetStr);
    Serial.printf("Local Zeit: %02lu:%02lu:%02lu\n", localHours, localMinutes, localSeconds);  
    Serial.printf("Unix-Sekunden: %lu\n", unixSeconds);
    Serial.printf("Local Unix : %lu\n", localUnixSeconds);
    Serial.printf("GPS-Sync: %s\n", unixTimeClockHasGpsSynced() ? "ja" : "nein");
  }
  else
  {
    Serial.print("Keine Uhrzeitinitialisierung verfuegbar.\n");
  }

  if (gpsSerialData.utcTime[0] != '\0' && gpsSerialData.date[0] != '\0')
  {
    char utcFormatted[16] = "";
    gpsSerialFormatUtcTime(gpsSerialData.utcTime, utcFormatted, sizeof(utcFormatted));
    char dateFormatted[16] = "";
    gpsSerialFormatDate(gpsSerialData.date, dateFormatted, sizeof(dateFormatted));
    Serial.printf("GPS UTC Time: %s\n", utcFormatted);
    Serial.printf("GPS Date    : %s\n", dateFormatted);
  }
}

void printWifiHelp()
{
  Serial.print("\n=== WLAN Debug-Untermenue ===\n");
  Serial.printf("%-28s - %s\n", "help", "diese Hilfe anzeigen");
  Serial.printf("%-28s - %s\n", "ssid", "gespeicherte SSID anzeigen");
  Serial.printf("%-28s - %s\n", "pwd", "gespeichertes Passwort anzeigen");
  Serial.printf("%-28s - %s\n", "scan", "verfuegbare WLAN-Netze scannen und speichern");
  Serial.printf("%-28s - %s\n", "back", "zurueck zum Hauptmenue");
  Serial.print("============================\n");
}

static const char *getTWAiStateName(twai_state_t state)
{
  switch (state)
  {
    case TWAI_STATE_STOPPED:
      return "STOPPED";
    case TWAI_STATE_RUNNING:
      return "RUNNING";
    case TWAI_STATE_RECOVERING:
      return "RECOVERING";
    case TWAI_STATE_BUS_OFF:
      return "BUS_OFF";
    default:
      return "UNKNOWN";
  }
}

static void printPcaStatus()
{
  if (!PCA9555_IsInitialized())
  {
    Serial.println("PCA9555 nicht initialisiert.");
    return;
  }

  uint8_t outputPort0 = 0;
  uint8_t outputPort1 = 0;
  if (!PCA9555_ReadOutputs(outputPort0, outputPort1))
  {
    Serial.println("PCA9555 nicht erreichbar.");
    return;
  }

  uint8_t inputPort0 = 0;
  uint8_t inputPort1 = 0;
  if (!PCA9555_ReadInputs(inputPort0, inputPort1))
  {
    Serial.println("PCA9555 nicht erreichbar.");
    return;
  }

  Serial.print("\n=== PCA9555 Status ===\n");
  Serial.printf("FET-Ausgaenge (0.0-0.7): 0x%02X\n", outputPort0);
  for (uint8_t i = 0; i < 8; ++i)
  {
    Serial.printf("  FET%u=%s\n", i + 1, (outputPort0 & (1 << i)) ? "ON" : "OFF");
  }
  Serial.printf("OPTO-Eingaenge (1.0-1.7): 0x%02X\n", inputPort1);
  for (uint8_t i = 0; i < 8; ++i)
  {
    Serial.printf("  OPTO%u=%s\n", i + 1, (inputPort1 & (1 << i)) ? "HIGH" : "LOW");
  }
  Serial.print("========================\n");
}

static void printPcaInputStatus()
{
  if (!PCA9555_IsInitialized())
  {
    Serial.println("PCA9555 nicht initialisiert.");
    return;
  }

  uint8_t inputPort0 = 0;
  uint8_t inputPort1 = 0;
  if (!PCA9555_ReadInputs(inputPort0, inputPort1))
  {
    Serial.println("PCA9555 nicht erreichbar.");
    return;
  }

  Serial.print("\n=== PCA9555 Eingänge ===\n");
  Serial.printf("OPTO-Eingaenge (1.0-1.7): 0x%02X\n", inputPort1);
  for (uint8_t i = 0; i < 8; ++i)
  {
    Serial.printf("  OPTO%u=%s\n", i + 1, (inputPort1 & (1 << i)) ? "HIGH" : "LOW");
  }
  Serial.print("========================\n");
}

static void printPcaOutputStatus()
{
  if (!PCA9555_IsInitialized())
  {
    Serial.println("PCA9555 nicht initialisiert.");
    return;
  }

  uint8_t outputPort0 = 0;
  uint8_t outputPort1 = 0;
  if (!PCA9555_ReadOutputs(outputPort0, outputPort1))
  {
    Serial.println("PCA9555 nicht erreichbar.");
    return;
  }

  Serial.print("\n=== PCA9555 Ausgänge ===\n");
  Serial.printf("FET-Ausgaenge (0.0-0.7): 0x%02X\n", outputPort0);
  for (uint8_t i = 0; i < 8; ++i)
  {
    Serial.printf("  FET%u=%s\n", i + 1, (outputPort0 & (1 << i)) ? "ON" : "OFF");
  }
  Serial.print("========================\n");
}

static void processPcaLiveMode()
{
  static bool firstRun = true;
  if (!debugPromptShown)
  {
    while (Serial.available())
    {
      Serial.read();
    }
    debugPromptShown = true;
    firstRun = true;
  }

  bool changed = false;
  if (!PCA9555_CheckInterrupt(changed))
  {
    Serial.println("PCA9555 nicht erreichbar.");
    return;
  }

  if (firstRun || changed)
  {
    uint8_t inputPort0 = 0;
    uint8_t inputPort1 = 0;
    if (!PCA9555_ReadCachedInputs(inputPort0, inputPort1))
    {
      Serial.println("PCA9555 nicht erreichbar.");
      return;
    }

    clearTerminalScreen();
    Serial.print("OPTO:");
    for (uint8_t i = 0; i < 8; ++i)
    {
      Serial.printf(" %u:%s", i + 1, (inputPort1 & (1 << i)) ? "H" : "L");
    }
    Serial.print("\n");
    firstRun = false;
  }

  if (Serial.available())
  {
    Serial.read();
    startDebugMode();
  }
}

static void printCanTestStatistics(unsigned long count, unsigned long elapsedMs, const twai_status_info_t &before, const twai_status_info_t &after)
{
  Serial.print("\n=== CAN Test Statistik ===\n");
  Serial.printf("Anzahl gesendeter Pakete: %lu\n", count);
  Serial.printf("Dauer: %lums\n", elapsedMs);
  Serial.printf("Status vorher: %s, TX-Fehler: %u, RX-Fehler: %u\n",
                getTWAiStateName(before.state), before.tx_error_counter, before.rx_error_counter);
  Serial.printf("Status nachher: %s, TX-Fehler: %u, RX-Fehler: %u\n",
                getTWAiStateName(after.state), after.tx_error_counter, after.rx_error_counter);
  Serial.print("===========================\n");
}

static void printCanStatus()
{
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK)
  {
    Serial.print("Fehler: TWAI Status konnte nicht gelesen werden.\n");
    return;
  }

  Serial.print("\n=== CAN Status ===\n");
  Serial.printf("Zustand: %s\n", getTWAiStateName(status.state));
  Serial.printf("TX-Fehler: %u\n", status.tx_error_counter);
  Serial.printf("RX-Fehler: %u\n", status.rx_error_counter);
  Serial.printf("Bus-Fehler: %u\n", status.bus_error_count);
  Serial.printf("Arbitrationsfehler: %u\n", status.arb_lost_count);
  Serial.print("==================\n");
}

void printDebugHelp()
{
  clearTerminalScreen();
  Serial.print("\n=== Debug-Menue Hilfe ===\n");
  Serial.printf("%-34s - %s\n", "help", "diese Hilfe anzeigen");
  Serial.printf("%-34s - %s\n", "exit", "Debug-Menue verlassen");
  Serial.printf("%-34s - %s\n", "reboot", "ESP neu starten");
  Serial.printf("%-34s - %s\n", "status", "Zeige Systemstatus");
  Serial.printf("%-34s - %s\n", "temp", "Zeige ESP-Temperatur");
  Serial.printf("%-34s - %s\n", "time", "Live- und System-Uptime anzeigen, beliebige Taste zum Beenden");
  Serial.printf("%-34s - %s\n", "aht10 status", "AHT10-Status anzeigen");
  Serial.printf("%-34s - %s\n", "aht10 on | aht10 off", "AHT10 periodisch ein-/ausschalten");
  Serial.printf("%-34s - %s\n", "aht10 now", "AHT10 sofort messen und senden");
  Serial.printf("%-34s - %s\n", "wifi", "WLAN-Untermenue oeffnen");
  Serial.print("\n");

  Serial.printf("%-34s - %s\n", "ota | ota on | ota off", "OTA-Status / starten / beenden");
  Serial.printf("%-34s - %s\n", "ota autooff X", "OTA nach X Minuten auto ausschalten");
  Serial.printf("%-34s - %s\n", "ota autooff off", "OTA Auto-Off deaktivieren");
  Serial.print("\n");

  Serial.printf("%-34s - %s\n", "pca status | pca in | pca out", "PCA9555 I/O / Ein- und Ausgaenge");
  Serial.printf("%-34s - %s\n", "pca out N on/off", "FET1-8 schalten");
  Serial.printf("%-34s - %s\n", "pca live | pca init", "Live / neu initialisieren");
  Serial.print("\n");

  Serial.printf("%-34s - %s\n", "ws off", "WS2812 ausschalten");
  Serial.printf("%-34s - %s\n", "ws blink R G B I D", "Blinkfarbe R,G,B, Intervall I ms, Dauer D ms");
  Serial.printf("%-34s - %s\n", "ws on R G B", "Farbe dauerhaft einschalten");
  Serial.print("\n");

  Serial.printf("%-34s - %s\n", "can live <serial|twai|all>", "CAN Live Modus mit Parameter auswählen");
  Serial.printf("%-34s - %s\n", "gps live [roh]", "GPS-Serial live verfolgen, rohdaten optional anzeigen");
  Serial.printf("%-34s - %s\n", "gps stop", "GPS Live Mode beenden");
  Serial.printf("%-34s - %s\n", "can status", "Aktuellen CAN/TWAI-Status anzeigen");
  Serial.printf("%-34s - %s\n", "can test [count]", "Sende CAN-Testpakete und pruefe empfangene Pakete");
  Serial.printf("%-34s - %s\n", "sendcan 0xID;DLC;DATA", "CAN-Nachricht auf den CAN-Bus senden");
  Serial.printf("%-34s - %s\n", "webterm on|off|status", "Web Terminal Konsole im Browser ein-/ausschalten");
  Serial.printf("%-34s - %s\n", "canping on|off|status|fast", "CAN-Ping Responder steuern (fast = priorisierter Modus)");
  Serial.print("\n");

  Serial.printf("%-34s - %s\n", "zone", "ZoneDetect Status anzeigen");
  Serial.printf("%-34s - %s\n", "error l | error i | error c", "Fehler-Log anzeigen/info/loeschen");
  Serial.print("\n");
  Serial.printf("%-34s - %s\n", "autoexit X", "Debug nach X Minuten automatisch verlassen");
  Serial.printf("%-34s - %s\n", "autoexit off", "Auto-Exit deaktivieren");
  Serial.print("========================\n");
}

static void printZoneDetectStatus()
{
  char timezone[64] = {0};
  char country[64] = {0};
  bool valid = false;
  bool hasGpsPosition = false;
  unsigned long lastUpdateMs = 0;
  float lat = 0.0f;
  float lon = 0.0f;

  if (zoneDetectGetStatus(timezone, sizeof(timezone), country, sizeof(country), valid, hasGpsPosition, lastUpdateMs, lat, lon))
  {
    bool dbLoaded = zoneDetectIsDatabaseLoaded();
    if (!valid && hasGpsPosition && dbLoaded)
    {
      zoneDetectRequestImmediateLookup();
    }
    Serial.print("\n=== ZoneDetect Status ===\n");
    if (valid)
    {
      Serial.printf("Timezone: %s\n", timezone[0] != '\0' ? timezone : "-");
      Serial.printf("Country : %s\n", country[0] != '\0' ? country : "-");

      if (gpsSerialData.utcTime[0] != '\0' && gpsSerialData.date[0] != '\0')
      {
        char utcFormatted[16] = "";
        gpsSerialFormatUtcTime(gpsSerialData.utcTime, utcFormatted, sizeof(utcFormatted));
        char dateFormatted[16] = "";
        gpsSerialFormatDate(gpsSerialData.date, dateFormatted, sizeof(dateFormatted));
        Serial.printf("GPS UTC Date : %s\n", dateFormatted);
        Serial.printf("GPS UTC Time : %s\n", utcFormatted);

        uint32_t gpsUtcSeconds = 0;
        if (debugMenuParseGpsUtcSeconds(gpsUtcSeconds))
        {
          Serial.printf("UTC Timestamp: %lu\n", (unsigned long)gpsUtcSeconds);
#if DEBUG_MENU_HAS_TIMEZONE_LOOKUP
          long offsetSeconds = 0;
          bool dstActive = false;
          char posixRule[128] = "";
          struct tm localTm = {0};
          if (debugMenuComputeTimezoneOffset(timezone, (time_t)gpsUtcSeconds, offsetSeconds, dstActive, posixRule, sizeof(posixRule), &localTm))
          {
            char offsetStr[16] = "";
            debugMenuFormatUtcOffset(offsetSeconds, offsetStr, sizeof(offsetStr));
            char localTimeStr[32] = "";
            debugMenuFormatLocalTime(localTm, localTimeStr, sizeof(localTimeStr));
            Serial.printf("UTC Offset  : %s\n", offsetStr);
            Serial.printf("TZ Rule     : %s\n", posixRule[0] != '\0' ? posixRule : "-");
            Serial.printf("DST active  : %s\n", dstActive ? "ja" : "nein");
            Serial.printf("Local Time  : %s\n", localTimeStr);
            unixTimeClockSetWithOffset(gpsUtcSeconds, offsetSeconds);
            Serial.printf("Local Unix  : %lu\n", (unsigned long)unixTimeClockGetLocal());
          }
          else
          {
            Serial.print("UTC Offset  : konnte nicht berechnet werden. micro-timezonedb fehlt oder Zone nicht gefunden.\n");
          }
#else
          Serial.print("UTC Offset  : micro-timezonedb nicht installiert.\n");
#endif
        }
        else
        {
          Serial.print("UTC Timestamp: konnte nicht aus GPS-Daten ermittelt werden.\n");
        }
      }
      else
      {
        Serial.print("GPS UTC Date/Time: nicht verfuegbar\n");
      }

      Serial.printf("Position: %.6f, %.6f\n", lat, lon);
      Serial.printf("Letzte Aktualisierung: %lu ms\n", lastUpdateMs);
    }
    else if (hasGpsPosition)
    {
      if (dbLoaded)
      {
        Serial.print("ZoneDetect DB geladen, GPS-Daten vorhanden, warte auf ersten Lookup.\n");
      }
      else
      {
        Serial.print("ZoneDetect DB nicht geladen oder nicht gefunden. Bitte prüfe /timezone16.bin in LittleFS.\n");
      }
      Serial.printf("Position: %.6f, %.6f\n", lat, lon);
    }
    else
    {
      Serial.print("ZoneDetect ist noch nicht verfügbar oder es wurden keine GPS-Daten gefunden.\n");
    }
    Serial.print("===========================\n");
  }
  else
  {
    Serial.print("ZoneDetect Status konnte nicht abgerufen werden.\n");
  }
}

void printOtaStatus()
{
  Serial.print("\n=== OTA Status ===\n");
  Serial.printf("OTA aktiviert: %u\n", OTA_On);
  if (OTA_On && WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("WiFi-IP: %s\n", WiFi.localIP().toString().c_str());
  }
  Serial.print(OTA_On ? "OTA ist aktiv." : "OTA ist inaktiv.");
  Serial.print('\n');
  if (OTA_AutoOffIntervalMs > 0)
  {
    Serial.printf("OTA Auto-Off: %lu Minuten\n", OTA_AutoOffIntervalMs / 60000UL);
  }
  else
  {
    Serial.print("OTA Auto-Off: deaktiviert\n");
  }
  Serial.print("==================\n");
}

void printTemperature()
{
  float temp = temperatureRead();
  Serial.print("\n=== ESP Temperatur ===\n");
  Serial.printf("Temperatur: %.1f Â°C\n", temp);
  Serial.print("=======================\n");
}

void printCurrentTime()
{
  Serial.print("\n=== Aktuelle Zeit ===\n");
  if (unixTimeClockIsInitialized())
  {
    uint32_t unixSeconds = unixTimeClockGet();
    long offsetSeconds = 0;
    bool dstActive = false;
    char timezone[64] = "";
    uint32_t localUnixSeconds = debugMenuGetEffectiveLocalUnix(unixSeconds, offsetSeconds, dstActive, timezone, sizeof(timezone));
    uint32_t seconds = unixSeconds % 60;
    uint32_t minutes = (unixSeconds / 60) % 60;
    uint32_t hours = (unixSeconds / 3600) % 24;
    uint32_t days = unixSeconds / 86400;
    uint32_t localSeconds = localUnixSeconds % 60;
    uint32_t localMinutes = (localUnixSeconds / 60) % 60;
    uint32_t localHours = (localUnixSeconds / 3600) % 24;
    char offsetStr[16] = "";
    debugMenuFormatUtcOffset(offsetSeconds, offsetStr, sizeof(offsetStr));
    Serial.printf("Unix-Sekunden: %lu\n", unixSeconds);
    Serial.printf("UTC Zeit    : %02lu:%02lu:%02lu\n", hours, minutes, seconds);
    Serial.printf("Local Zeit  : %02lu:%02lu:%02lu\n", localHours, localMinutes, localSeconds);
    Serial.printf("UTC Offset  : %s\n", offsetStr);
    Serial.printf("Sommerzeit  : %s\n", dstActive ? "ja" : "nein");
    if (timezone[0] != '\0')
    {
      Serial.printf("Timezone    : %s\n", timezone);
    }
    else
    {
      Serial.print("Timezone    : nicht verfuegbar\n");
    }
    Serial.printf("Local Unix  : %lu\n", localUnixSeconds);
    Serial.printf("Tage seit Epoch: %lu\n", days);
    Serial.printf("GPS-Sync   : %s\n", unixTimeClockHasGpsSynced() ? "ja" : "nein");
  }
  else
  {
    Serial.print("Keine Uhrzeitinitialisierung verfuegbar.\n");
  }
  if (gpsSerialData.utcTime[0] != '\0' && gpsSerialData.date[0] != '\0')
  {
    char utcFormatted[16] = "";
    gpsSerialFormatUtcTime(gpsSerialData.utcTime, utcFormatted, sizeof(utcFormatted));
    char dateFormatted[16] = "";
    gpsSerialFormatDate(gpsSerialData.date, dateFormatted, sizeof(dateFormatted));
    Serial.printf("GPS UTC Time : %s\n", utcFormatted);
    Serial.printf("GPS Date     : %s\n", dateFormatted);
  }
  Serial.print("====================\n");
}

void printDebugStatus()
{
  Serial.print("\n=== Debug Status ===\n");
  Serial.printf("OTA aktiviert: %u\n", OTA_On);
  if (OTA_AutoOffIntervalMs > 0)
  {
    Serial.printf("OTA Auto-Off: %lu Minuten\n", OTA_AutoOffIntervalMs / 60000UL);
  }
  else
  {
    Serial.print("OTA Auto-Off: deaktiviert\n");
  }
  Serial.printf("WiFi-Status: %d\n", WiFi.status());
  Serial.printf("Boot-Zaehler: %lu\n", getBootCount());
#ifdef FIRMWARE_VERSION
  Serial.printf("Firmware-Version: %s\n", FIRMWARE_VERSION);
#else
  Serial.print("Firmware-Version: unbekannt\n");
#endif
  Serial.printf("OTA-Firmware-Zähler: %lu\n", getFirmwareVersion());
  Serial.printf("AHT10 aktiv: %s\n", aht10Enabled ? "ja" : "nein");
  if (!isnan(aht10LastTemperature))
  {
    Serial.printf("AHT10 Temperatur: %.1f °C\n", aht10LastTemperature);
    Serial.printf("AHT10 Luftfeuchte: %.1f %%\n", aht10LastHumidity);
  }
  else
  {
    Serial.print("AHT10: Keine letzte Messung vorhanden\n");
  }
  if (debugAutoExitInterval > 0)
  {
    Serial.printf("Auto-Exit in: %lu min\n", (debugAutoExitInterval - (millis() - debugAutoExitStart)) / 60000);
  }
  else
  {
    Serial.print("Auto-Exit: deaktiviert\n");
  }

  twai_status_info_t canStatus;
  if (twai_get_status_info(&canStatus) == ESP_OK)
  {
    Serial.print("=== CAN/TWAI Status ===\n");
    Serial.printf("Zustand: %s\n", getTWAiStateName(canStatus.state));
    Serial.printf("TX-Fehler: %u\n", canStatus.tx_error_counter);
    Serial.printf("RX-Fehler: %u\n", canStatus.rx_error_counter);
    Serial.printf("Bus-Fehler: %u\n", canStatus.bus_error_count);
    Serial.printf("Arbitrationsfehler: %u\n", canStatus.arb_lost_count);
  }
  else
  {
    Serial.print("=== CAN/TWAI Status: Fehler beim Auslesen ===\n");
  }

  Serial.print("=== WS2812 Status ===\n");
  Serial.printf("Mode: %s\n", ws2812GetModeName());
  if (ws2812GetMode() == WS2812_BLINK)
  {
    uint32_t color = ws2812GetColor();
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    Serial.printf("Farbe: %u,%u,%u\n", r, g, b);
    Serial.printf("Pause: %lums\n", ws2812GetInterval());
    Serial.printf("Dauer: %lums\n", ws2812GetDuration());
  }
  else if (ws2812GetMode() == WS2812_ON)
  {
    uint32_t color = ws2812GetColor();
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    Serial.printf("Farbe: %u,%u,%u\n", r, g, b);
  }
  Serial.print("====================\n");
}

void printStoredWifiCredentials()
{
  String ssid = SSID_Lesen();
  String password = PASSWORD_Lesen();

  Serial.print("\n=== Gespeicherte WLAN-Daten ===\n");
  Serial.print("SSID: ");
  Serial.print(ssid.length() ? ssid : "<leer>");
  Serial.print('\n');
  Serial.print("Passwort: ");
  Serial.print(password.length() ? password : "<leer>");
  Serial.print('\n');
  Serial.print("===============================\n");
}

void scanWifiNetworks()
{
  Serial.print("\nStarte WLAN-Scan...\n");

  if (WiFi.getMode() != WIFI_STA)
  {
    Serial.print("WiFi-Modus: STA\n");
    WiFi.mode(WIFI_STA);
    delay(300);
  }
  WiFi.disconnect(false);
  delay(200);

  Serial.print("Scanne... (bitte warten)\n");
  esp_task_wdt_reset();
  int scanResult = WiFi.scanNetworks(false, false);
  Serial.printf("Scan-Ergebnis: %d\n", scanResult);

  if (scanResult == WIFI_SCAN_FAILED)
  {
    Serial.print("FEHLER: Scan fehlgeschlagen.\n");
    return;
  }

  if (scanResult == 0)
  {
    Serial.print("Keine Netzwerke gefunden.\n");
    return;
  }

  Serial.printf("\n%d Netzwerke gefunden:\n", scanResult);
  Serial.print("--------------------------------------------\n");
  Serial.print(" Nr  SSID                      dBm   Kanal\n");
  Serial.print("--------------------------------------------\n");
  for (int i = 0; i < scanResult; ++i)
  {
    Serial.printf(" %2d  %-24s  %4d    %2d\n",
                  i + 1,
                  WiFi.SSID(i).c_str(),
                  WiFi.RSSI(i),
                  WiFi.channel(i));
  }
  Serial.print("--------------------------------------------\n");

  Serial.print("\nWae hle die Nummer der SSID zum Speichern, oder 0 zum Abbrechen:\n");
  wifiSubState = WIFI_WAIT_SELECTION;
}

void showWifiPrompt()
{
  clearTerminalScreen();
  Serial.print("\n>> WLAN-Untermenue gestartet. Tippe 'help' fuer Befehle.\n");
  printWifiHelp();
  wifiPromptShown = true;
  wifiSubState = WIFI_IDLE;
}

void processWifiMenu()
{
  if (!wifiPromptShown)
  {
    showWifiPrompt();
  }

  if (wifiSubState == WIFI_WAIT_SELECTION)
  {
    static size_t wifiLinePos = 0;
    char input[64] = {0};
    if (readSerialLine(Serial, input, sizeof(input), wifiLinePos))
    {
      debugMenuTrimWhitespace(input);
      if (input[0] != '\0')
      {
        wifiChoice = atoi(input);
        if (wifiChoice <= 0)
        {
          Serial.print("Scan-Auswahl abgebrochen.\n");
          WiFi.scanDelete();
          wifiSubState = WIFI_IDLE;
          return;
        }
        if (wifiChoice > WiFi.scanComplete())
        {
          Serial.print("Ungueltige Auswahl. Versuche erneut.\n");
          wifiSubState = WIFI_IDLE;
          return;
        }
        wifiPendingSSID = WiFi.SSID(wifiChoice - 1);
        Serial.printf("Ausgewaehlte SSID: %s\n", wifiPendingSSID.c_str());
        Serial.print("Passwort eingeben:\n");
        wifiSubState = WIFI_WAIT_PASSWORD;
      }
    }
    return;
  }

  if (wifiSubState == WIFI_WAIT_PASSWORD)
  {
    if (Serial.available())
    {
      char password[64] = {0};
      if (readSerialText(password, sizeof(password)))
      {
        SSID_Schreiben(wifiPendingSSID);
        PASSWORD_Schreiben(String(password));
        Serial.print("WLAN-Daten gespeichert.\n");
        wifiSubState = WIFI_IDLE;
      }
    }
    return;
  }

  char cmd[64] = {0};
  if (!readSerialCommand(cmd, sizeof(cmd)))
  {
    return;
  }

  if (strcmp(cmd, "help") == 0)
  {
    printWifiHelp();
  }
  else if (strcmp(cmd, "ssid") == 0)
  {
    Serial.print('\n');
    Serial.print("Gespeicherte SSID: ");
    Serial.print(SSID_Lesen());
    Serial.print('\n');
  }
  else if (strcmp(cmd, "pwd") == 0)
  {
    Serial.print('\n');
    Serial.print("Gespeichertes Passwort: ");
    Serial.print(PASSWORD_Lesen());
    Serial.print('\n');
  }
  else if (strcmp(cmd, "scan") == 0)
  {
    scanWifiNetworks();
  }
  else if (strcmp(cmd, "back") == 0)
  {
    Serial.print("Zurueck zum Hauptmenue.\n");
    debugMenuState = DEBUG_MAIN;
    wifiPromptShown = false;
    debugPromptShown = false;
    debugAutoExitStart = millis();
  }
  else
  {
    Serial.printf("Unbekannter WLAN-Befehl: '%s'. Tippe 'help'.\n", cmd);
    debugAutoExitStart = millis();
  }
}

void saveDebugAutoExitSetting(unsigned long minutes)
{
  char buf[16];
  sprintf(buf, "%lu", minutes);
  FS_Open();
  writeFile(LittleFS, "/autoexit.txt", buf);
  FS_Close();
}

void loadDebugAutoExitSetting()
{
  FS_Open();
  if (!LittleFS.exists("/autoexit.txt"))
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", DEFAULT_DEBUG_AUTOEXIT_MINUTES);
    writeFile(LittleFS, "/autoexit.txt", buf);
    debugAutoExitInterval = DEFAULT_DEBUG_AUTOEXIT_MINUTES * 60000UL;
    FS_Close();
    return;
  }

  readFile(LittleFS, "/autoexit.txt");
  FS_Close();

  char *endptr = nullptr;
  unsigned long minutes = strtoul(FileBuffer, &endptr, 10);
  if (endptr == FileBuffer || (*endptr != '\0' && !isspace((unsigned char)*endptr)))
  {
    debugAutoExitInterval = DEFAULT_DEBUG_AUTOEXIT_MINUTES * 60000UL;
  }
  else if (minutes > 0)
  {
    debugAutoExitInterval = minutes * 60000UL;
  }
  else
  {
    debugAutoExitInterval = 0;
  }
}

void processDebugMenu()
{
  if (debugMenuState == DEBUG_OFF)
  {
    return;
  }

  if (debugAutoExitInterval > 0 && millis() - debugAutoExitStart >= debugAutoExitInterval)
  {
    Serial.print("Auto-Exit: Debug-Modus wurde verlassen.\n");
    debugMenuState = DEBUG_OFF;
    debugPromptShown = false;
    wifiPromptShown = false;
    return;
  }

  if (OTA_On == 1)
  {
    ArduinoOTA.handle();
  }

  if (debugMenuState == DEBUG_MAIN)
  {
    if (!debugPromptShown)
    {
      clearTerminalScreen();
      Serial.print("\n>> Debug-Modus gestartet. Tippe 'help' fuer Befehle.\n");
      printDebugHelp();
      debugPromptShown = true;
    }

    char cmd[64] = {0};
    if (!readSerialCommand(cmd, sizeof(cmd)))
    {
      return;
    }

    if (strcmp(cmd, "help") == 0)
    {
      printDebugHelp();
    }
    else if (strcmp(cmd, "exit") == 0)
    {
      Serial.print("Beende Debug-Modus.\n");
      debugMenuState = DEBUG_OFF;
      debugPromptShown = false;
    }
    else if (strcmp(cmd, "reboot") == 0)
    {
      Serial.print("Starte reboot...\n");
      delay(100);
      ESP.restart();
    }
    else if (strcmp(cmd, "status") == 0)
    {
      printDebugStatus();
      debugAutoExitStart = millis();
    }
    else if (strcmp(cmd, "time") == 0)
    {
      debugMenuState = DEBUG_TIME;
      debugPromptShown = false;
      debugAutoExitStart = millis();
    }
    else if (strcmp(cmd, "temp") == 0)
    {
      printTemperature();
      debugAutoExitStart = millis();
    }
    else if (strncmp(cmd, "aht10", 5) == 0)
    {
      char params[32] = {0};
      strncpy(params, cmd + 5, sizeof(params) - 1);
      debugMenuTrimWhitespace(params);
      if (params[0] == '\0' || strcmp(params, "status") == 0)
      {
        printAHT10Status();
      }
      else if (strcmp(params, "on") == 0)
      {
        aht10Enabled = true;
        Serial.print("AHT10 periodische Messung eingeschaltet.\n");
      }
      else if (strcmp(params, "off") == 0)
      {
        aht10Enabled = false;
        Serial.print("AHT10 periodische Messung ausgeschaltet.\n");
      }
      else if (strcmp(params, "now") == 0)
      {
        if (aht10State == AHT10_IDLE)
        {
          if (startAHT10Measurement())
          {
            Serial.print("AHT10 Messung gestartet, Ergebnis in ca. 80 ms.\n");
          }
          else
          {
            Serial.print("AHT10 Messung konnte nicht gestartet werden.\n");
          }
        }
        else
        {
          Serial.print("AHT10 Messung bereits aktiv, bitte warten.\n");
        }
      }
      else
      {
        Serial.print("Unbekannter AHT10-Befehl. Verwende aht10 status/on/off/now.\n");
      }
      debugAutoExitStart = millis();
    }
    else if (strcmp(cmd, "wifi") == 0)
    {
      debugMenuState = DEBUG_WIFI;
      wifiPromptShown = false;
      debugAutoExitStart = millis();
    }
    else if (strcmp(cmd, "ws off") == 0)
    {
      ws2812Off();
      Serial.print("WS2812 ausgeschaltet.\n");
    }
    else if (strncmp(cmd, "ws blink", 8) == 0)
    {
      char params[64] = {0};
      strncpy(params, cmd + 8, sizeof(params) - 1);
      debugMenuTrimWhitespace(params);
      char *parts[5] = {0};
      int idx = splitTokens(params, parts, 5);
      if (idx != 5)
      {
        Serial.print("Verwendung: ws blink R G B Intervall_ms Dauer_ms\n");
      }
      else
      {
        int r = atoi(parts[0]);
        int g = atoi(parts[1]);
        int b = atoi(parts[2]);
        unsigned long interval = strtoul(parts[3], NULL, 10);
        unsigned long duration = strtoul(parts[4], NULL, 10);
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 || interval == 0 || duration == 0)
        {
          Serial.print("Ungueltige Werte. RGB 0-255, Intervall/Dauer > 0.\n");
        }
        else
        {
          ws2812SetBlinkRGB(r, g, b, interval, duration);
          Serial.printf("WS2812 blink: %d,%d,%d, %lums Pause, %lums Dauer\n", r, g, b, interval, duration);
        }
      }
    }
    else if (strncmp(cmd, "ws on", 5) == 0)
    {
      char params[64] = {0};
      strncpy(params, cmd + 5, sizeof(params) - 1);
      debugMenuTrimWhitespace(params);
      char *parts[3] = {0};
      int idx = splitTokens(params, parts, 3);
      if (idx != 3)
      {
        Serial.print("Verwendung: ws on R G B\n");
      }
      else
      {
        int r = atoi(parts[0]);
        int g = atoi(parts[1]);
        int b = atoi(parts[2]);
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
        {
          Serial.print("Ungueltige Werte. RGB 0-255.\n");
        }
        else
        {
          ws2812SetOnRGB(r, g, b);
          Serial.printf("WS2812 on: %d,%d,%d\n", r, g, b);
        }
      }
    }
    else if (strcmp(cmd, "can status") == 0)
    {
      printCanStatus();
    }
    else if (strcmp(cmd, "can live serial") == 0)
    {
      debugMenuState = DEBUG_CAN_LIVE;
      debugPromptShown = false;
      canLiveModeWeb = false;
      canLiveMode = CAN_LIVE_SERIAL;
      Serial.print("Starte CAN Live Mode (seriell).\n");
    }
    else if (strcmp(cmd, "can live all") == 0 || strcmp(cmd, "can all") == 0)
    {
      debugMenuState = DEBUG_CAN_LIVE;
      debugPromptShown = false;
      canLiveModeWeb = false;
      canLiveMode = CAN_LIVE_ALL;
      Serial.print("Starte CAN All Mode (TWAI + seriell). Beliebige Taste zum Beenden.\n");
    }
    else if (strcmp(cmd, "can live twai") == 0)
    {
      debugMenuState = DEBUG_CAN_LIVE;
      debugPromptShown = false;
      canLiveModeWeb = false;
      canLiveMode = CAN_LIVE_TWAI;
      Serial.print("Starte CAN Live Mode (TWAI).\n");
    }
    else if (strcmp(cmd, "gps live roh") == 0)
    {
      debugMenuState = DEBUG_GPS_LIVE;
      debugPromptShown = false;
      gpsLiveMode = true;
      gpsLiveRawModeEnabled = true;
      Serial.print("Starte GPS Live Mode (Rohdaten). Beliebige Taste zum Beenden.\n");
    }
    else if (strcmp(cmd, "gps live") == 0)
    {
      debugMenuState = DEBUG_GPS_LIVE;
      debugPromptShown = false;
      gpsLiveMode = true;
      gpsLiveRawModeEnabled = false;
      Serial.print("Starte GPS Live Mode. Beliebige Taste zum Beenden.\n");
    }
    else if (strcmp(cmd, "gps stop") == 0)
    {
      debugMenuState = DEBUG_MAIN;
      debugPromptShown = false;
      gpsLiveMode = false;
      gpsLiveRawModeEnabled = false;
      Serial.print("GPS Live Mode beendet.\n");
      printDebugHelp();
    }
    else if (strcmp(cmd, "can stop") == 0)
    {
      canLiveMode = CAN_LIVE_OFF;
      Serial.print("CAN Live Mode beendet.\n");
      printDebugHelp();
    }
    else if (strcmp(cmd, "pca status") == 0)
    {
      printPcaStatus();
    }
    else if (strcmp(cmd, "pca in") == 0)
    {
      printPcaInputStatus();
    }
    else if (strcmp(cmd, "pca out") == 0)
    {
      printPcaOutputStatus();
    }
    else if (strncmp(cmd, "pca out ", 8) == 0)
    {
      char params[32] = {0};
      strncpy(params, cmd + 8, sizeof(params) - 1);
      debugMenuTrimWhitespace(params);
      char *parts[3] = {0};
      int count = splitTokens(params, parts, 3);
      if (count != 2)
      {
        Serial.print("Verwendung: pca out <1-8> <on|off>\n");
      }
      else
      {
        int outputIndex = atoi(parts[0]);
        bool onState = (strcmp(parts[1], "on") == 0);
        bool offState = (strcmp(parts[1], "off") == 0);
        if (outputIndex < 1 || outputIndex > 8 || (!onState && !offState))
        {
          Serial.print("Verwendung: pca out <1-8> <on|off>\n");
        }
        else if (!PCA9555_SetOutput(outputIndex - 1, onState))
        {
          Serial.println("PCA9555 nicht erreichbar oder nicht initialisiert.");
        }
        else
        {
          Serial.printf("FET%u %s\n", outputIndex, onState ? "eingeschaltet" : "ausgeschaltet");
        }
      }
    }
    else if (strcmp(cmd, "pca live") == 0)
    {
      debugMenuState = DEBUG_PCA_LIVE;
      debugPromptShown = false;
      Serial.print("Starte PCA9555 Live Mode. Beliebige Taste zum Beenden.\n");
    }
    else if (strcmp(cmd, "pca init") == 0)
    {
      if (PCA9555_Init())
      {
        Serial.println("PCA9555 initialisiert.");
      }
      else
      {
        Serial.println("PCA9555 nicht erreichbar.");
      }
    }
    else if (strncmp(cmd, "can test", 8) == 0)
    {
      char params[32] = {0};
      strncpy(params, cmd + 8, sizeof(params) - 1);
      debugMenuTrimWhitespace(params);
      unsigned long count = 100;
      if (params[0] != '\0')
      {
        count = strtoul(params, NULL, 10);
      }
      if (count == 0)
      {
        Serial.print("Ungueltige Anzahl. Verwende: can test oder can test <Anzahl>\n");
      }
      else
      {
        twai_status_info_t beforeStatus;
        twai_status_info_t afterStatus;
        twai_get_status_info(&beforeStatus);

        unsigned long receivedCount = 0;
        unsigned long matchedCount = 0;
        const uint32_t testId = 0x1FFFFFFF; // höchste Extended-CAN-ID
        const uint8_t testData[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

        Serial.printf("Sende %lu CAN-Testpakete...\n", count);
        unsigned long startTime = millis();
        for (unsigned long i = 0; i < count; ++i)
        {
          CAN_SendEx(true, 8, testId,
                     testData[0], testData[1], testData[2], testData[3],
                     testData[4], testData[5], testData[6], testData[7]);
          if ((i & 0x1F) == 0x1F)
          {
            yield();
          }

          twai_message_t incoming;
          while (twai_receive(&incoming, pdMS_TO_TICKS(5)) == ESP_OK)
          {
            receivedCount++;
            if (incoming.identifier == testId && incoming.data_length_code == 8)
            {
              bool same = true;
              for (uint8_t k = 0; k < 8; ++k)
              {
                if (incoming.data[k] != testData[k])
                {
                  same = false;
                  break;
                }
              }
              if (same)
              {
                matchedCount++;
              }
            }
          }
        }

        unsigned long extraPollStart = millis();
        twai_message_t incoming;
        while (millis() - extraPollStart < 100 && twai_receive(&incoming, pdMS_TO_TICKS(5)) == ESP_OK)
        {
          receivedCount++;
          if (incoming.identifier == testId && incoming.data_length_code == 8)
          {
            bool same = true;
            for (uint8_t k = 0; k < 8; ++k)
            {
              if (incoming.data[k] != testData[k])
              {
                same = false;
                break;
              }
            }
            if (same)
            {
              matchedCount++;
            }
          }
        }

        unsigned long elapsed = millis() - startTime;
        twai_get_status_info(&afterStatus);
        printCanTestStatistics(count, elapsed, beforeStatus, afterStatus);
        Serial.printf("Empfangene Pakete waehrend Test: %lu\n", receivedCount);
        Serial.printf("Identische Testpakete empfangen: %lu\n", matchedCount);
      }
    }
    else if (strcmp(cmd, "error log") == 0 || strcmp(cmd, "error l") == 0)
    {
      errorLogPrint();
    }
    else if (strcmp(cmd, "error info") == 0 || strcmp(cmd, "error i") == 0)
    {
      errorLogPrintInfo();
    }
    else if (strcmp(cmd, "error clear") == 0 || strcmp(cmd, "error c") == 0)
    {
      errorLogClear();
      Serial.print("Fehler-Log geloescht.\n");
    }
    else if (strncmp(cmd, "sendcan ", 8) == 0)
    {
      char payload[64] = {0};
      strncpy(payload, cmd + 8, sizeof(payload) - 1);
      debugMenuTrimWhitespace(payload);
      uint32_t messageId;
      uint8_t dlc;
      uint8_t data[8] = {0};
      if (!parseCanMessageLine(payload, messageId, dlc, data))
      {
        Serial.print("Ungueltiges Format. Verwende: sendcan 0xID;DLC;DATA\n");
      }
      else
      {
        CAN_SendEx(true, dlc, messageId,
                   data[0], data[1], data[2], data[3],
                   data[4], data[5], data[6], data[7]);
        Serial.printf("CAN gesendet: %s\n", payload);
      }
    }
    else if (strncmp(cmd, "webterm", 7) == 0)
    {
      char params[32] = {0};
      strncpy(params, cmd + 7, sizeof(params) - 1);
      debugMenuTrimWhitespace(params);
      if (strcmp(params, "on") == 0)
      {
        webTerminalSetEnabled(true);
      }
      else if (strcmp(params, "off") == 0)
      {
        webTerminalSetEnabled(false);
        Serial.print("Web-Terminal deaktiviert.\n");
      }
      else if (strcmp(params, "status") == 0)
      {
        Serial.printf("Web-Terminal: %s\n", webTerminalIsEnabled() ? "aktiv" : "deaktiviert");
      }
      else
      {
        Serial.print("Verwendung: webterm on|off|status\n");
      }
    }

    else if (strncmp(cmd, "canping", 7) == 0)
    {
      const char* params = cmd + 7;
      while (*params == ' ') params++;
      if (strcmp(params, "on") == 0)
      {
        CANPing::setEnabled(true);
      }
      else if (strcmp(params, "off") == 0)
      {
        CANPing::setEnabled(false);
      }
      else if (strcmp(params, "status") == 0)
      {
        Serial.printf("CANPing: %s, Modus: %s, Pings beantwortet: %lu\n",
          CANPing::isEnabled() ? "aktiv" : "deaktiviert",
          CANPing::isFastMode() ? "FAST" : "normal",
          (unsigned long)CANPing::getPingCount());
        Serial.printf("  Request-ID:  0x%08X\n", (unsigned)(CANPing::CANPING_CMD_BASE + CANPing::node_id));
        Serial.printf("  Response-ID: 0x%08X\n", (unsigned)(CANPing::CANPING_CMD_BASE + CANPing::CANPING_RESP_OFFSET + CANPing::node_id));
      }
      else if (strcmp(params, "fast") == 0)
      {
        CANPing::setFastMode(true);
        Serial.println("CANPing FAST aktiv: beliebige Taste auf USB-Serial beendet den Modus.");
      }
      else
      {
        Serial.print("Verwendung: canping on|off|status|fast\n");
      }
    }

    else if (strncmp(cmd, "autoexit", 8) == 0)
    {
      if (strcmp(cmd, "autoexit off") == 0)
      {
        debugAutoExitInterval = 0;
        saveDebugAutoExitSetting(0);
        Serial.print("Auto-Exit deaktiviert.\n");
      }
      else
      {
        char value[16] = {0};
        strncpy(value, cmd + 9, sizeof(value) - 1);
        debugMenuTrimWhitespace(value);
        if (value[0] != '\0')
        {
          unsigned long minutes = strtoul(value, NULL, 10);
          if (minutes > 0)
          {
            debugAutoExitInterval = minutes * 60000UL;
            debugAutoExitStart = millis();
            saveDebugAutoExitSetting(minutes);
            Serial.printf("Auto-Exit in %lu Minuten aktiviert.\n", minutes);
          }
          else
          {
            Serial.print("Ungueltige Zeit. Gib eine Zahl in Minuten ein.\n");
          }
        }
        else
        {
          Serial.print("Verwendung: autoexit <Minuten> oder autoexit off\n");
        }
      }
    }
    else if (strncmp(cmd, "ota", 3) == 0)
    {
      if (strcmp(cmd, "ota") == 0)
      {
        printOtaStatus();
      }
      else if (strcmp(cmd, "ota on") == 0)
      {
        Serial.print("OTA einschalten...\n");
        OTA_Start();
      }
      else if (strcmp(cmd, "ota off") == 0)
      {
        Serial.print("OTA ausschalten...\n");
        OTA_Stop();
      }
      else if (strncmp(cmd, "ota autooff", 11) == 0)
      {
        if (strcmp(cmd, "ota autooff off") == 0)
        {
          OTA_AutoOffIntervalMs = 0;
          Serial.print("OTA Auto-Off deaktiviert.\n");
        }
        else
        {
          char value[16] = {0};
          strncpy(value, cmd + 12, sizeof(value) - 1);
          debugMenuTrimWhitespace(value);
          if (value[0] != '\0')
          {
            unsigned long minutes = strtoul(value, NULL, 10);
            if (minutes > 0)
            {
              OTA_AutoOffIntervalMs = minutes * 60000UL;
              Serial.printf("OTA Auto-Off auf %lu Minuten gesetzt.\n", minutes);
            }
            else
            {
              Serial.print("Ungueltige Zeit. Gib eine Zahl in Minuten ein.\n");
            }
          }
          else
          {
            Serial.print("Verwendung: ota autooff <Minuten> oder ota autooff off\n");
          }
        }
      }
      else
      {
        Serial.printf("Unbekannter OTA-Befehl: '%s'. Verwende ota, ota on oder ota off.\n", cmd);
      }
    }
    else if (strcmp(cmd, "zone") == 0)
    {
      printZoneDetectStatus();
    }
    else
    {
      Serial.printf("Unbekannter Befehl: '%s'. Tippe 'help'.\n", cmd);
    }
    if (debugMenuState != DEBUG_OFF)
    {
      debugAutoExitStart = millis();
    }
  }
  else if (debugMenuState == DEBUG_CAN_LIVE)
  {
    processCanLiveMode();
  }
  else if (debugMenuState == DEBUG_GPS_LIVE)
  {
    processGpsLiveMode();
  }
  else if (debugMenuState == DEBUG_TIME)
  {
    processTimeLiveMode();
  }
  else if (debugMenuState == DEBUG_PCA_LIVE)
  {
    processPcaLiveMode();
  }
  else if (debugMenuState == DEBUG_WIFI)
  {
    processWifiMenu();
  }
}

void startDebugMode()
{
  debugMenuState = DEBUG_MAIN;
  debugPromptShown = false;
  wifiPromptShown = false;
  wifiSubState = WIFI_IDLE;
  wifiChoice = 0;
  wifiPendingSSID = String();
  wifiPasswordBuffer = String();
  canLiveMode = CAN_LIVE_OFF;
  gpsLiveMode = false;
  debugAutoExitStart = millis();
}

void processCanLiveMode()
{
  if (!debugPromptShown)
  {
    clearTerminalScreen();
    Serial.print("\n=== CAN Live Mode ===\n");
    Serial.print("Zeige CAN Live Traffic.\n");
    Serial.print("Beliebige Taste auf USB-Serial zum Beenden.\n");
    Serial.print("=======================\n");
    debugPromptShown = true;
  }

  static size_t canLiveLinePos = 0;
  char line[128] = {0};
  if (readSerialLine(*can_rx_ser, line, sizeof(line), canLiveLinePos))
  {
    debugMenuTrimWhitespace(line);
    if (line[0] != '\0')
    {
      Serial.printf("[CAN SERIAL RX] %s\n", line);
    }
  }

  static size_t canLiveExitPos = 0;
  char key[32] = {0};
  if (readSerialLine(Serial, key, sizeof(key), canLiveExitPos))
  {
    debugMenuTrimWhitespace(key);
    debugMenuState = DEBUG_MAIN;
    debugPromptShown = false;
    canLiveMode = CAN_LIVE_OFF;
    Serial.print("CAN Live Mode beendet.\n");
    printDebugHelp();
    return;
  }
}

void processGpsLiveMode()
{
  if (!debugPromptShown)
  {
    clearTerminalScreen();
    Serial.print("\n=== GPS Live Mode ===\n");
    Serial.print("Zeige GPS-Serial Traffic.\n");
    Serial.print("Beliebige Taste auf USB-Serial zum Beenden.\n");
    Serial.print("=======================\n");
    debugPromptShown = true;
  }

  static size_t gpsLiveLinePos = 0;
  static size_t gpsLiveExitPos = 0;
  static unsigned long lastZonePrint = 0;
  char line[128] = {0};
  char key[32] = {0};
  if (readSerialLine(Serial, key, sizeof(key), gpsLiveExitPos))
  {
    debugMenuTrimWhitespace(key);
    debugMenuState = DEBUG_MAIN;
    debugPromptShown = false;
    gpsLiveMode = false;
    gpsLiveRawModeEnabled = false;
    Serial.print("GPS Live Mode beendet.\n");
    printDebugHelp();
    return;
  }

  if (millis() - lastZonePrint >= 2000)
  {
    lastZonePrint = millis();
    char timezone[64] = {0};
    char country[64] = {0};
    bool valid = false;
    bool hasGpsPosition = false;
    unsigned long lastUpdateMs = 0;
    float lat = 0.0f;
    float lon = 0.0f;

    if (zoneDetectGetStatus(timezone, sizeof(timezone), country, sizeof(country), valid, hasGpsPosition, lastUpdateMs, lat, lon))
    {
      if (valid)
      {
        Serial.printf("ZoneDetect: %s / %s (%.6f, %.6f)\n", timezone[0] != '\0' ? timezone : "-", country[0] != '\0' ? country : "-", lat, lon);
      }
      else if (hasGpsPosition)
      {
        Serial.printf("ZoneDetect: GPS-Fix vorhanden, warte auf ersten Lookup (%.6f, %.6f).\n", lat, lon);
      }
      else
      {
        Serial.print("ZoneDetect: noch keine Daten oder kein GPS-Fix.\n");
      }
    }
    else
    {
      Serial.print("ZoneDetect: Status nicht verfuegbar.\n");
    }
  }
}

void handleDebugActivation()
{
  if (debugMenuState == DEBUG_OFF)
  {
    static size_t dbgLinePos = 0;
    char input[64] = {0};
    if (readSerialLine(Serial, input, sizeof(input), dbgLinePos))
    {
      debugMenuTrimWhitespace(input);
      for (size_t i = 0; input[i] != '\0'; ++i)
      {
        input[i] = tolower((unsigned char)input[i]);
      }
      if (strcmp(input, "debug") == 0)
      {
        startDebugMode();
      }
    }
  }
}

#endif // DEBUG_MENU_H
