#ifndef UNIX_TIME_CLOCK_H
#define UNIX_TIME_CLOCK_H

#include <Arduino.h>
#include <time.h>
#include "GPSSerial.h"

#if defined(__has_include)
#  if __has_include(<timezonedb_lookup.h>)
#    include <timezonedb_lookup.h>
#    define UNIX_TIME_CLOCK_HAS_TIMEZONE_LOOKUP 1
#  else
#    define UNIX_TIME_CLOCK_HAS_TIMEZONE_LOOKUP 0
#  endif
#else
#  define UNIX_TIME_CLOCK_HAS_TIMEZONE_LOOKUP 0
#endif

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

static inline bool unixTimeClockIsLeapYear(int year)
{
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static inline int unixTimeClockDaysInMonth(int year, int month)
{
  static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month != 2)
    return days[month];
  return days[month] + (unixTimeClockIsLeapYear(year) ? 1 : 0);
}

static inline uint32_t unixTimeClockEpochFromDate(int year, int month, int day, int hour, int minute, int second)
{
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
  return (uint32_t)daysSinceEpoch * 86400UL + (uint32_t)hour * 3600UL + (uint32_t)minute * 60UL + (uint32_t)second;
}

static inline void unixTimeClockUtcBreakdown(time_t epoch, struct tm &tmOut)
{
  uint32_t seconds = (uint32_t)epoch;
  tmOut.tm_sec = seconds % 60;
  seconds /= 60;
  tmOut.tm_min = seconds % 60;
  seconds /= 60;
  tmOut.tm_hour = seconds % 24;
  uint32_t days = seconds / 24;
  tmOut.tm_wday = (int)((days + 4) % 7);

  int z = (int)(days + 719468);
  int era = z / 146097;
  int doe = z - era * 146097;
  int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int y = yoe + era * 400;
  int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int mp = (5 * doy + 2) / 153;

  tmOut.tm_mday = doy - (153 * mp + 2) / 5 + 1;
  tmOut.tm_mon = mp + 3;
  if (tmOut.tm_mon > 12)
  {
    tmOut.tm_mon -= 12;
    y += 1;
  }
  tmOut.tm_mon -= 1;
  tmOut.tm_year = y - 1900;
  tmOut.tm_yday = doy;
  tmOut.tm_isdst = 0;
}

static inline bool unixTimeClockParsePosixOffset(const char *str, long &outSeconds, const char *&outNext)
{
  if (str == nullptr || *str == '\0')
  {
    return false;
  }

  bool negative = false;
  if (*str == '+')
  {
    negative = false;
    ++str;
  }
  else if (*str == '-')
  {
    negative = true;
    ++str;
  }

  long value = 0;
  int parts[3] = {0, 0, 0};
  int part = 0;
  while (*str != '\0' && part < 3)
  {
    if (*str == ':')
    {
      ++part;
      ++str;
      continue;
    }
    if (!isdigit((unsigned char)*str))
    {
      break;
    }
    parts[part] = parts[part] * 10 + (*str - '0');
    ++str;
  }

  value = parts[0] * 3600L + parts[1] * 60L + parts[2];
  outSeconds = negative ? -value : value;
  outNext = str;
  return true;
}

static inline bool unixTimeClockParsePosixRule(const char *rule, int &month, int &week, int &weekday, int &hour, int &minute, int &second, char &suffix)
{
  if (rule == nullptr || rule[0] != 'M')
    return false;
  rule++;

  month = 0;
  while (isdigit((unsigned char)*rule))
  {
    month = month * 10 + (*rule - '0');
    ++rule;
  }
  if (*rule != '.')
    return false;
  rule++;

  week = 0;
  while (isdigit((unsigned char)*rule))
  {
    week = week * 10 + (*rule - '0');
    ++rule;
  }
  if (*rule != '.')
    return false;
  rule++;

  weekday = 0;
  while (isdigit((unsigned char)*rule))
  {
    weekday = weekday * 10 + (*rule - '0');
    ++rule;
  }

  hour = 2;
  minute = 0;
  second = 0;
  suffix = 'w';

  if (*rule == '/')
  {
    ++rule;
    hour = 0;
    minute = 0;
    second = 0;
    while (isdigit((unsigned char)*rule))
    {
      hour = hour * 10 + (*rule - '0');
      ++rule;
    }
    if (*rule == ':')
    {
      ++rule;
      while (isdigit((unsigned char)*rule))
      {
        minute = minute * 10 + (*rule - '0');
        ++rule;
      }
      if (*rule == ':')
      {
        ++rule;
        while (isdigit((unsigned char)*rule))
        {
          second = second * 10 + (*rule - '0');
          ++rule;
        }
      }
    }
    if (*rule == 's' || *rule == 'S' || *rule == 'w' || *rule == 'W' || *rule == 'u' || *rule == 'U' || *rule == 'g' || *rule == 'G' || *rule == 'z' || *rule == 'Z')
    {
      suffix = *rule;
      ++rule;
    }
  }

  return true;
}

static inline uint32_t unixTimeClockComputeRuleDay(int year, int month, int week, int weekday)
{
  uint32_t firstDayEpoch = unixTimeClockEpochFromDate(year, month, 1, 0, 0, 0);
  int firstDow = (int)((firstDayEpoch / 86400UL + 4) % 7); // 0 = Sunday
  int targetDow = weekday % 7;
  int day = 1 + ((targetDow - firstDow + 7) % 7);
  if (week < 5)
  {
    day += (week - 1) * 7;
  }
  else
  {
    int dim = unixTimeClockDaysInMonth(year, month);
    while (day + 7 <= dim)
      day += 7;
  }
  return (uint32_t)day;
}

static inline bool unixTimeClockComputePosixTransition(int year, const char *rule, int offsetBeforeSeconds, time_t &outUtcTime)
{
  int month, week, weekday, hour, minute, second;
  char suffix;
  if (!unixTimeClockParsePosixRule(rule, month, week, weekday, hour, minute, second, suffix))
    return false;

  uint32_t day = unixTimeClockComputeRuleDay(year, month, week, weekday);
  uint32_t localEpoch = unixTimeClockEpochFromDate(year, month, day, hour, minute, second);

  if (suffix == 'u' || suffix == 'U' || suffix == 'g' || suffix == 'G' || suffix == 'z' || suffix == 'Z')
  {
    outUtcTime = (time_t)localEpoch;
  }
  else if (suffix == 's')
  {
    outUtcTime = (time_t)((int64_t)localEpoch - offsetBeforeSeconds);
  }
  else
  {
    outUtcTime = (time_t)((int64_t)localEpoch - offsetBeforeSeconds);
  }
  return true;
}

static inline bool unixTimeClockParsePosixTz(const char *posixTz, long &stdOffsetSeconds, long &dstOffsetSeconds, const char *&startRule, const char *&endRule)
{
  if (posixTz == nullptr || posixTz[0] == '\0')
    return false;

  const char *p = posixTz;
  while (isalpha((unsigned char)*p))
    ++p;
  if (p == posixTz)
    return false;

  long stdOffsetRaw = 0;
  if (!unixTimeClockParsePosixOffset(p, stdOffsetRaw, p))
    return false;
  stdOffsetSeconds = -stdOffsetRaw;

  if (*p == '\0')
  {
    dstOffsetSeconds = stdOffsetSeconds;
    startRule = nullptr;
    endRule = nullptr;
    return true;
  }

  const char *dstNameStart = p;
  while (isalpha((unsigned char)*p))
    ++p;
  if (p == dstNameStart)
    return false;

  long dstOffsetRaw = 0;
  if (*p != ',' && *p != '\0')
  {
    if (!unixTimeClockParsePosixOffset(p, dstOffsetRaw, p))
      return false;
    dstOffsetSeconds = -dstOffsetRaw;
  }
  else
  {
    dstOffsetSeconds = stdOffsetSeconds + 3600;
  }

  if (*p != ',')
  {
    startRule = nullptr;
    endRule = nullptr;
    return true;
  }

  ++p;
  startRule = p;
  while (*p != ',' && *p != '\0')
    ++p;
  if (*p != ',')
    return false;
  endRule = p + 1;
  return true;
}

static inline bool unixTimeClockComputeSignalOffsetFromPosix(const char *posixTz, time_t utcTime, long &outOffsetSeconds, bool &outDstActive)
{
  long stdOffsetSeconds = 0;
  long dstOffsetSeconds = 0;
  const char *startRule = nullptr;
  const char *endRule = nullptr;
  if (!unixTimeClockParsePosixTz(posixTz, stdOffsetSeconds, dstOffsetSeconds, startRule, endRule))
  {
    return false;
  }

  if (startRule == nullptr || endRule == nullptr)
  {
    outDstActive = false;
    outOffsetSeconds = stdOffsetSeconds;
    return true;
  }

  int year;
  int month;
  int day;
  {
    uint32_t d = (uint32_t)(utcTime / 86400);
    int z = (int)d + 719468;
    int era = z / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp + 3;
    if (month > 12)
    {
      month -= 12;
      year += 1;
    }
  }

  time_t startUtc;
  time_t endUtc;
  if (!unixTimeClockComputePosixTransition(year, startRule, (int)stdOffsetSeconds, startUtc) ||
      !unixTimeClockComputePosixTransition(year, endRule, (int)dstOffsetSeconds, endUtc))
  {
    outDstActive = false;
    outOffsetSeconds = stdOffsetSeconds;
    return true;
  }

  if (startUtc < endUtc)
  {
    outDstActive = (utcTime >= startUtc && utcTime < endUtc);
  }
  else
  {
    outDstActive = !(utcTime >= endUtc && utcTime < startUtc);
  }

  outOffsetSeconds = outDstActive ? dstOffsetSeconds : stdOffsetSeconds;
  return true;
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

static inline bool unixTimeClockComputeOffsetFromZone(const char *zoneName, time_t utcTime, long &outOffsetSeconds, bool &outDstActive)
{
#if UNIX_TIME_CLOCK_HAS_TIMEZONE_LOOKUP
  if (zoneName == nullptr || zoneName[0] == '\0')
  {
    return false;
  }

  const char *posixTz = lookup_posix_timezone_tz(zoneName);
  if (posixTz == nullptr)
  {
    return false;
  }

  return unixTimeClockComputeSignalOffsetFromPosix(posixTz, utcTime, outOffsetSeconds, outDstActive);
#else
  (void)zoneName;
  (void)utcTime;
  (void)outOffsetSeconds;
  (void)outDstActive;
  return false;
#endif
}

static inline uint32_t &unixTimeClockLocalSeconds()
{
  static uint32_t value = 0;
  return value;
}

static inline unsigned long &unixTimeClockLocalMillisAtUpdate()
{
  static unsigned long value = 0;
  return value;
}

static inline bool &unixTimeClockLocalInitialized()
{
  static bool value = false;
  return value;
}

static inline long &unixTimeClockOffsetSeconds()
{
  static long value = 0;
  return value;
}

static inline void unixTimeClockSet(uint32_t seconds)
{
  unixTimeClockSeconds() = seconds;
  unixTimeClockMillisAtUpdate() = millis();
  unixTimeClockOffsetSeconds() = 0;
  unixTimeClockLocalInitialized() = false;
  unixTimeClockInitialized() = true;
}

static inline void unixTimeClockSetWithOffset(uint32_t seconds, long offsetSeconds)
{
  unixTimeClockSeconds() = seconds;
  unixTimeClockMillisAtUpdate() = millis();
  unixTimeClockOffsetSeconds() = offsetSeconds;
  if (offsetSeconds >= 0)
  {
    unixTimeClockLocalSeconds() = seconds + (uint32_t)offsetSeconds;
  }
  else
  {
    unixTimeClockLocalSeconds() = seconds - (uint32_t)(-offsetSeconds);
  }
  unixTimeClockLocalMillisAtUpdate() = millis();
  unixTimeClockLocalInitialized() = true;
  unixTimeClockInitialized() = true;
}

static inline bool unixTimeClockSetToUtcIfDifferent(uint32_t gpsSeconds)
{
  bool localWasInitialized = unixTimeClockLocalInitialized();
  long oldOffsetSeconds = unixTimeClockOffsetSeconds();

  if (!unixTimeClockInitialized() || gpsSeconds != unixTimeClockComputeSeconds(unixTimeClockSeconds(), unixTimeClockMillisAtUpdate()))
  {
    unixTimeClockSet(gpsSeconds);
    if (localWasInitialized)
    {
      if (oldOffsetSeconds >= 0)
      {
        unixTimeClockLocalSeconds() = gpsSeconds + (uint32_t)oldOffsetSeconds;
      }
      else
      {
        unixTimeClockLocalSeconds() = gpsSeconds - (uint32_t)(-oldOffsetSeconds);
      }
      unixTimeClockLocalMillisAtUpdate() = millis();
      unixTimeClockLocalInitialized() = true;
    }
    unixTimeClockHasGpsSync() = true;
    return true;
  }
  return false;
}

static inline void unixTimeClockSyncToGps()
{
  uint32_t gpsSeconds;
  if (!unixTimeClockParseGpsUtcDateTime(gpsSeconds))
  {
    return;
  }

  unixTimeClockSetToUtcIfDifferent(gpsSeconds);
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

static inline uint32_t unixTimeClockGetLocal()
{
  if (!unixTimeClockLocalInitialized())
  {
    return 0;
  }
  return unixTimeClockComputeSeconds(unixTimeClockLocalSeconds(), unixTimeClockLocalMillisAtUpdate());
}

extern bool zoneDetectGetStatus(char *timezone, size_t timezoneSize, char *country, size_t countrySize, bool &valid, bool &hasGpsPosition, unsigned long &lastUpdateMs, float &latitude, float &longitude);

static inline uint32_t unixTimeClockGetEffectiveLocalUnix(uint32_t utcSeconds, long &outOffsetSeconds, bool &outDstActive, char *outTimezone, size_t outTimezoneSize)
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
      if (unixTimeClockComputeOffsetFromZone(outTimezone, (time_t)utcSeconds, computedOffset, computedDstActive))
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

#endif // UNIX_TIME_CLOCK_H
