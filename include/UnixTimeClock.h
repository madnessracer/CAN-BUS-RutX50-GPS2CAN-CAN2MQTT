#ifndef UNIX_TIME_CLOCK_H
#define UNIX_TIME_CLOCK_H

#include <Arduino.h>
#include "GPSSerial.h"

static inline uint32_t &unixTimeClockSeconds()
{
  static uint32_t value = 0;
  return value;
}

static inline unsigned long &unixTimeClockMillisAtUpdate()
{
  static unsigned long value = 0;
  return value;
}

static inline bool &unixTimeClockInitialized()
{
  static bool value = false;
  return value;
}

static inline bool &unixTimeClockHasGpsSync()
{
  static bool value = false;
  return value;
}

static inline uint32_t unixTimeClockComputeSeconds(uint32_t baseSeconds, unsigned long baseMillis)
{
  unsigned long elapsedMillis = millis() - baseMillis;
  return baseSeconds + elapsedMillis / 1000UL;
}

static inline bool unixTimeClockParseGpsUtcDateTime(uint32_t &outSeconds)
{
  if (gpsSerialData.utcTime[0] == '\0' || gpsSerialData.date[0] == '\0')
  {
    return false;
  }

  const char *utc = gpsSerialData.utcTime;
  const char *date = gpsSerialData.date;

  if (strlen(utc) < 6 || strlen(date) < 6)
  {
    return false;
  }

  for (int i = 0; i < 6; ++i)
  {
    if (!isdigit((unsigned char)utc[i]) || !isdigit((unsigned char)date[i]))
    {
      return false;
    }
  }

  int hour = (utc[0] - '0') * 10 + (utc[1] - '0');
  int minute = (utc[2] - '0') * 10 + (utc[3] - '0');
  int second = (utc[4] - '0') * 10 + (utc[5] - '0');
  int day = (date[0] - '0') * 10 + (date[1] - '0');
  int month = (date[2] - '0') * 10 + (date[3] - '0');
  int year = (date[4] - '0') * 10 + (date[5] - '0');

  if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60 ||
      day < 1 || day > 31 || month < 1 || month > 12)
  {
    return false;
  }

  year += 2000;

  int adjustedYear = year;
  int adjustedMonth = month;
  if (adjustedMonth <= 2)
  {
    adjustedYear -= 1;
    adjustedMonth += 12;
  }

  int era = adjustedYear / 400;
  int yoe = adjustedYear - era * 400;
  int doy = (153 * (adjustedMonth - 3) + 2) / 5 + day - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int daysSinceEpoch = era * 146097 + doe - 719468;

  if (daysSinceEpoch < 0)
  {
    return false;
  }

  outSeconds = (uint32_t)daysSinceEpoch * 86400UL + (uint32_t)hour * 3600UL + (uint32_t)minute * 60UL + (uint32_t)second;
  return true;
}

static inline void unixTimeClockSet(uint32_t seconds)
{
  unixTimeClockSeconds() = seconds;
  unixTimeClockMillisAtUpdate() = millis();
  unixTimeClockInitialized() = true;
}

static inline void unixTimeClockSyncToGps()
{
  uint32_t gpsSeconds;
  if (!unixTimeClockParseGpsUtcDateTime(gpsSeconds))
  {
    return;
  }

  if (!unixTimeClockInitialized() || gpsSeconds != unixTimeClockComputeSeconds(unixTimeClockSeconds(), unixTimeClockMillisAtUpdate()))
  {
    unixTimeClockSet(gpsSeconds);
    unixTimeClockHasGpsSync() = true;
  }
}

static inline void unixTimeClockLoop()
{
  if (gpsSerialData.utcTime[0] != '\0' && gpsSerialData.date[0] != '\0')
  {
    unixTimeClockSyncToGps();
  }
}

static inline bool unixTimeClockIsInitialized()
{
  return unixTimeClockInitialized();
}

static inline bool unixTimeClockHasGpsSynced()
{
  return unixTimeClockHasGpsSync();
}

static inline uint32_t unixTimeClockGet()
{
  if (!unixTimeClockInitialized())
  {
    return 0;
  }
  return unixTimeClockComputeSeconds(unixTimeClockSeconds(), unixTimeClockMillisAtUpdate());
}

#endif // UNIX_TIME_CLOCK_H
