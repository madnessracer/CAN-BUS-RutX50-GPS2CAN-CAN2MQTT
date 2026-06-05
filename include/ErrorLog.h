#ifndef __ERROR_LOG_H__
#define __ERROR_LOG_H__

#include <Arduino.h>
#include <LittleFS.h>
#include "FileSystem.h"
#include "UnixTimeClock.h"

#define ERROR_LOG_PATH "/error_log.txt"
#define MAX_ERROR_LOG_ENTRIES 20
#define ERROR_LOG_LINE_SIZE 128

static const char *ERROR_LOG_KEYS[] = {
  "CAN_BUS_OFF",
  "CAN_STATUS_FAIL",
  "CAN_ERR_WARN",
  "CAN_TX_FAIL",
  "CAN_PARSE_FAIL",
  "AHT10_FAIL",
};
static const size_t ERROR_LOG_KEY_COUNT = sizeof(ERROR_LOG_KEYS) / sizeof(ERROR_LOG_KEYS[0]);
static bool errorLogActiveStates[ERROR_LOG_KEY_COUNT] = {false};

static int errorLogGetIndex(const char *key)
{
  for (size_t i = 0; i < ERROR_LOG_KEY_COUNT; ++i)
  {
    if (strcmp(ERROR_LOG_KEYS[i], key) == 0)
    {
      return (int)i;
    }
  }
  return -1;
}

static void errorLogWriteLines(char lines[][ERROR_LOG_LINE_SIZE], size_t count)
{
  if (!FS_Open())
  {
    return;
  }
  if (LittleFS.exists(ERROR_LOG_PATH))
  {
    LittleFS.remove(ERROR_LOG_PATH);
  }

  if (lines == nullptr || count == 0)
  {
    FS_Close();
    return;
  }

  File file = LittleFS.open(ERROR_LOG_PATH, FILE_WRITE);
  if (!file)
  {
    Serial.println("Fehler: Error-Log Datei konnte nicht geschrieben werden.");
    FS_Close();
    return;
  }

  for (size_t i = 0; i < count; ++i)
  {
    file.println(lines[i]);
  }
  file.close();
  FS_Close();
}

static size_t errorLogReadAllLines(char lines[][ERROR_LOG_LINE_SIZE], size_t maxLines)
{
  size_t count = 0;
  if (!FS_Open())
  {
    return 0;
  }

  if (!LittleFS.exists(ERROR_LOG_PATH))
  {
    FS_Close();
    return 0;
  }

  File file = LittleFS.open(ERROR_LOG_PATH, "r");
  if (!file)
  {
    FS_Close();
    return 0;
  }

  while (file.available() && count < maxLines)
  {
    int len = file.readBytesUntil('\n', lines[count], ERROR_LOG_LINE_SIZE - 1);
    if (len < 0)
      break;
    while (len > 0 && lines[count][len - 1] == '\r')
    {
      len--;
    }
    lines[count][len] = '\0';
    count++;
  }
  file.close();
  FS_Close();
  return count;
}

static void errorLogPrint()
{
  char lines[MAX_ERROR_LOG_ENTRIES][ERROR_LOG_LINE_SIZE];
  size_t count = errorLogReadAllLines(lines, MAX_ERROR_LOG_ENTRIES);
  Serial.println("\n=== Fehler-Log ===");
  if (count == 0)
  {
    Serial.println("Keine Fehler gefunden.");
  }
  else
  {
    for (size_t i = 0; i < count; ++i)
    {
      Serial.printf("%2u: %s\n", (unsigned int)(i + 1), lines[i]);
    }
  }
  Serial.println("==================\n");
}

static void errorLogPrintInfo()
{
  char lines[MAX_ERROR_LOG_ENTRIES][ERROR_LOG_LINE_SIZE];
  size_t count = errorLogReadAllLines(lines, MAX_ERROR_LOG_ENTRIES);
  if (!FS_Open())
  {
    Serial.println("Fehler: LittleFS nicht erreichbar.");
    return;
  }
  size_t fileSize = 0;
  if (LittleFS.exists(ERROR_LOG_PATH))
  {
    File file = LittleFS.open(ERROR_LOG_PATH, "r");
    if (file)
    {
      fileSize = file.size();
      file.close();
    }
  }
  FS_Close();

  Serial.println("\n=== Fehler-Log Info ===");
  Serial.printf("Eintraege: %u\n", (unsigned int)count);
  Serial.printf("Dateigroesse: %u bytes\n", (unsigned int)fileSize);
  Serial.println("Aktive Fehlerzustaende:");
  bool foundActive = false;
  for (size_t i = 0; i < ERROR_LOG_KEY_COUNT; ++i)
  {
    if (errorLogActiveStates[i])
    {
      Serial.printf(" - %s\n", ERROR_LOG_KEYS[i]);
      foundActive = true;
    }
  }
  if (!foundActive)
  {
    Serial.println(" - keine Aktivitaeten");
  }
  Serial.println("======================\n");
}

static void errorLogClear()
{
  if (FS_Open())
  {
    if (LittleFS.exists(ERROR_LOG_PATH))
    {
      LittleFS.remove(ERROR_LOG_PATH);
    }
    File file = LittleFS.open(ERROR_LOG_PATH, FILE_WRITE);
    if (file)
    {
      file.close();
    }
    FS_Close();
  }

  for (size_t i = 0; i < ERROR_LOG_KEY_COUNT; ++i)
  {
    errorLogActiveStates[i] = false;
  }
}

static void errorLogSetActive(const char *key, bool active)
{
  int idx = errorLogGetIndex(key);
  if (idx >= 0)
  {
    errorLogActiveStates[idx] = active;
  }
}

static bool errorLogIsActive(const char *key)
{
  int idx = errorLogGetIndex(key);
  if (idx >= 0)
  {
    return errorLogActiveStates[idx];
  }
  return false;
}

static inline void errorLogFormatUnixDateTime(uint32_t unixSeconds, char *out, size_t outSize)
{
  if (outSize == 0)
    return;

  uint32_t secondsOfDay = unixSeconds % 86400UL;
  int hour = secondsOfDay / 3600UL;
  int minute = (secondsOfDay % 3600UL) / 60UL;
  int second = secondsOfDay % 60UL;

  int z = (int)(unixSeconds / 86400UL) + 719468;
  int era = z / 146097;
  int doe = z - era * 146097;
  int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int year = yoe + era * 400;
  int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int mp = (5 * doy + 2) / 153;
  int day = doy - (153 * mp + 2) / 5 + 1;
  int month = mp + 3;
  if (month > 12)
  {
    month -= 12;
    year += 1;
  }

  snprintf(out, outSize, "%02d.%02d.%04d %02d:%02d:%02d", day, month, year, hour, minute, second);
}

static void errorLogAddOnce(const char *key, const char *message)
{
  if (errorLogIsActive(key))
  {
    return;
  }

  char lines[MAX_ERROR_LOG_ENTRIES + 1][ERROR_LOG_LINE_SIZE];
  size_t count = errorLogReadAllLines(lines, MAX_ERROR_LOG_ENTRIES);

  if (count == MAX_ERROR_LOG_ENTRIES)
  {
    for (size_t i = 1; i < count; ++i)
    {
      strcpy(lines[i - 1], lines[i]);
    }
    count--;
  }

  char entry[ERROR_LOG_LINE_SIZE];
  char timestamp[32] = "";

  if (unixTimeClockIsInitialized())
  {
    errorLogFormatUnixDateTime(unixTimeClockGet(), timestamp, sizeof(timestamp));
  }
  else if (gpsSerialData.utcTime[0] != '\0' && gpsSerialData.date[0] != '\0')
  {
    char formattedTime[16] = "";
    char formattedDate[16] = "";
    gpsSerialFormatUtcTime(gpsSerialData.utcTime, formattedTime, sizeof(formattedTime));
    gpsSerialFormatDate(gpsSerialData.date, formattedDate, sizeof(formattedDate));
    if (formattedTime[0] != '\0' && formattedDate[0] != '\0')
    {
      snprintf(timestamp, sizeof(timestamp), "%s %s", formattedDate, formattedTime);
    }
  }

  if (timestamp[0] == '\0')
  {
    unsigned long t = millis();
    unsigned long s = t / 1000;
    unsigned int hours = (unsigned int)((s / 3600) % 24);
    unsigned int minutes = (unsigned int)((s / 60) % 60);
    unsigned int seconds = (unsigned int)(s % 60);
    snprintf(timestamp, sizeof(timestamp), "%02u:%02u:%02u", hours, minutes, seconds);
  }

  snprintf(entry, sizeof(entry), "%s %s - %s", timestamp, key, message);
  strncpy(lines[count++], entry, ERROR_LOG_LINE_SIZE - 1);
  lines[count - 1][ERROR_LOG_LINE_SIZE - 1] = '\0';

  errorLogWriteLines(lines, count);
  errorLogSetActive(key, true);
}

static void errorLogResolve(const char *key)
{
  errorLogSetActive(key, false);
}

static bool errorLogHasEntries()
{
  for (size_t i = 0; i < ERROR_LOG_KEY_COUNT; ++i)
  {
    if (errorLogActiveStates[i])
    {
      return true;
    }
  }
  return false;
}

#endif // __ERROR_LOG_H__
