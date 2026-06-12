#ifndef ZONEDETECTTASK_H
#define ZONEDETECTTASK_H

#include <Arduino.h>

void zoneDetectTaskInit();
void zoneDetectRequestImmediateLookup();
bool zoneDetectGetStatus(char *timezone, size_t timezoneSize, char *country, size_t countrySize, bool &valid, bool &hasGpsPosition, unsigned long &lastUpdateMs, float &latitude, float &longitude);
void zoneDetectUpdateGpsCoordinates(double latitude, double longitude, bool valid);
bool zoneDetectIsDatabaseLoaded();
bool zoneDetectComputeOffsetForTimezone(const char *zoneName, long &outOffsetSeconds, bool &outDstActive);

#include <LittleFS.h>
#include <esp_heap_caps.h>
#include "GPSSerial.h"
#include "UnixTimeClock.h"
#include <zonedetect.h>

inline const char *ZONEDETECT_DB_PATHS[] = {"/timezone16.bin"};
inline const size_t ZONEDETECT_DB_PATH_COUNT = sizeof(ZONEDETECT_DB_PATHS) / sizeof(ZONEDETECT_DB_PATHS[0]);
inline ZoneDetect *sZoneDetectLibrary = nullptr;
inline size_t sZoneDetectDbLength = 0;

inline portMUX_TYPE sZoneDetectMux = portMUX_INITIALIZER_UNLOCKED;
inline bool sZoneDetectValid = false;
inline char sZoneDetectTimezone[64] = "";
inline char sZoneDetectCountry[64] = "";
inline unsigned long sZoneDetectLastUpdateMs = 0;
inline float sZoneDetectLastLatitude = 0.0f;
inline float sZoneDetectLastLongitude = 0.0f;
inline bool sZoneDetectHasLastPosition = false;
inline bool sZoneDetectForceLookup = true;
inline float sZoneDetectDistanceThresholdKm = 5.0f;
inline bool sZoneDetectDbLoaded = false;

inline portMUX_TYPE sZoneDetectGpsMux = portMUX_INITIALIZER_UNLOCKED;
inline bool sZoneDetectGpsValid = false;
inline double sZoneDetectGpsLatitude = 0.0;
inline double sZoneDetectGpsLongitude = 0.0;

inline void zoneDetectStoreResult(const char *timezone, const char *country)
{
  portENTER_CRITICAL(&sZoneDetectMux);
  sZoneDetectValid = (timezone != nullptr && timezone[0] != '\0');
  strncpy(sZoneDetectTimezone, timezone ? timezone : "", sizeof(sZoneDetectTimezone) - 1);
  sZoneDetectTimezone[sizeof(sZoneDetectTimezone) - 1] = '\0';
  strncpy(sZoneDetectCountry, country ? country : "", sizeof(sZoneDetectCountry) - 1);
  sZoneDetectCountry[sizeof(sZoneDetectCountry) - 1] = '\0';
  sZoneDetectLastUpdateMs = millis();
  portEXIT_CRITICAL(&sZoneDetectMux);
}

extern const uint8_t timezone16_bin_start[] asm("_binary_data_timezone16_bin_start");
extern const uint8_t timezone16_bin_end[] asm("_binary_data_timezone16_bin_end");

inline bool zoneDetectLoadDatabase()
{
  if (sZoneDetectLibrary != nullptr)
  {
    return true;
  }

  size_t fileSize = timezone16_bin_end - timezone16_bin_start;
  Serial.printf("ZoneDetect: Lade eingebettete Datenbank aus Flash (Gr├Â├ƒe %u bytes)...\n", (unsigned)fileSize);

  sZoneDetectLibrary = ZDOpenDatabaseFromMemory((void *)timezone16_bin_start, fileSize);
  if (sZoneDetectLibrary == nullptr)
  {
    Serial.println("ZoneDetect: Fehler beim Parsen der eingebetteten DB (ZDOpenDatabaseFromMemory = null).");
    sZoneDetectDbLoaded = false;
    return false;
  }

  sZoneDetectDbLoaded = true;
  Serial.println("ZoneDetect: Eingebettete Datenbank erfolgreich geladen.");
  return true;
}

inline void zoneDetectUnloadDatabase()
{
  if (sZoneDetectLibrary)
  {
    ZDCloseDatabase(sZoneDetectLibrary);
    sZoneDetectLibrary = nullptr;
  }
  sZoneDetectDbLoaded = false;
}

inline bool zoneDetectParseCoordinates(double &latitude, double &longitude)
{
  bool result = false;
  portENTER_CRITICAL(&sZoneDetectGpsMux);
  if (sZoneDetectGpsValid)
  {
    latitude = sZoneDetectGpsLatitude;
    longitude = sZoneDetectGpsLongitude;
    result = true;
  }
  portEXIT_CRITICAL(&sZoneDetectGpsMux);
  return result;
}

bool zoneDetectComputeOffsetForTimezone(const char *zoneName, long &outOffsetSeconds, bool &outDstActive)
{
  uint32_t utcSeconds;
  if (!unixTimeClockParseGpsUtcDateTime(utcSeconds))
  {
    return false;
  }

  return unixTimeClockComputeOffsetFromZone(zoneName, (time_t)utcSeconds, outOffsetSeconds, outDstActive);
}

inline double zoneDetectDistanceKm(double lat1, double lon1, double lat2, double lon2)
{
  static const double R = 6371.0;
  const double dLat = radians(lat2 - lat1);
  const double dLon = radians(lon2 - lon1);
  const double a = sin(dLat / 2.0) * sin(dLat / 2.0) + cos(radians(lat1)) * cos(radians(lat2)) * sin(dLon / 2.0) * sin(dLon / 2.0);
  const double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  return (float)(R * c);
}

inline bool zoneDetectExtractResult(double latitude, double longitude, char *timezone, size_t timezoneSize, char *country, size_t countrySize)
{
  if (sZoneDetectLibrary == nullptr)
  {
    return false;
  }

  ZoneDetectResult *results = ZDLookup(sZoneDetectLibrary, (float)latitude, (float)longitude, nullptr);
  if (results == nullptr)
  {
    return false;
  }

  bool foundTimezone = false;
  bool foundCountry = false;
  timezone[0] = '\0';
  country[0] = '\0';

  if (ZDGetTableType(sZoneDetectLibrary) == 'T')
  {
    const char *prefix = nullptr;
    const char *zoneId = nullptr;
    for (unsigned int i = 0; i < results[0].numFields; ++i)
    {
      if (results[0].fieldNames[i] == nullptr || results[0].data[i] == nullptr)
        continue;
      if (strcmp(results[0].fieldNames[i], "TimezoneIdPrefix") == 0)
      {
        prefix = results[0].data[i];
      }
      else if (strcmp(results[0].fieldNames[i], "TimezoneId") == 0)
      {
        zoneId = results[0].data[i];
      }
    }
    if (zoneId != nullptr)
    {
      if (prefix != nullptr && prefix[0] != '\0')
      {
        snprintf(timezone, timezoneSize, "%s%s", prefix, zoneId);
      }
      else
      {
        strncpy(timezone, zoneId, timezoneSize - 1);
        timezone[timezoneSize - 1] = '\0';
      }
      foundTimezone = (timezone[0] != '\0');
    }
  }
  else if (ZDGetTableType(sZoneDetectLibrary) == 'C')
  {
    for (unsigned int i = 0; i < results[0].numFields; ++i)
    {
      if (results[0].fieldNames[i] && results[0].data[i] && strcmp(results[0].fieldNames[i], "Name") == 0)
      {
        strncpy(country, results[0].data[i], countrySize - 1);
        country[countrySize - 1] = '\0';
        foundCountry = true;
        break;
      }
    }
  }

  if (!foundTimezone && !foundCountry)
  {
    ZDFreeResults(results);
    return false;
  }

  ZDFreeResults(results);
  return true;
}

inline void zoneDetectTask(void *param)
{
  // Warte bis LittleFS bereit ist und lade DB ÔÇô retry alle 5s
  while (!zoneDetectLoadDatabase())
  {
    zoneDetectStoreResult("", "");
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  while (true)
  {
    double lat = 0.0;
    double lon = 0.0;
    if (zoneDetectParseCoordinates(lat, lon))
    {
      bool shouldLookup = false;
      float prevLat = 0.0f;
      float prevLon = 0.0f;
      bool prevPositionValid = false;

      portENTER_CRITICAL(&sZoneDetectMux);
      shouldLookup = sZoneDetectForceLookup || !sZoneDetectHasLastPosition;
      if (!shouldLookup && sZoneDetectHasLastPosition)
      {
        prevLat = sZoneDetectLastLatitude;
        prevLon = sZoneDetectLastLongitude;
        prevPositionValid = true;
      }
      portEXIT_CRITICAL(&sZoneDetectMux);

      if (!shouldLookup && prevPositionValid)
      {
        shouldLookup = zoneDetectDistanceKm(prevLat, prevLon, lat, lon) >= sZoneDetectDistanceThresholdKm;
      }

      if (shouldLookup)
      {
        char timezone[64] = "";
        char country[64] = "";
        if (zoneDetectExtractResult(lat, lon, timezone, sizeof(timezone), country, sizeof(country)))
        {
          zoneDetectStoreResult(timezone, country);

          long offsetSeconds = 0;
          bool dstActive = false;
          if (zoneDetectComputeOffsetForTimezone(timezone, offsetSeconds, dstActive))
          {
            uint32_t utcSeconds = 0;
            if (unixTimeClockParseGpsUtcDateTime(utcSeconds))
            {
              unixTimeClockSetWithOffset(utcSeconds, offsetSeconds);
            }
          }
        }
        else
        {
          zoneDetectStoreResult("unknown", "");
        }

        portENTER_CRITICAL(&sZoneDetectMux);
        sZoneDetectLastLatitude = (float)lat;
        sZoneDetectLastLongitude = (float)lon;
        sZoneDetectHasLastPosition = true;
        sZoneDetectForceLookup = false;
        portEXIT_CRITICAL(&sZoneDetectMux);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void zoneDetectTaskInit()
{
  if (xTaskCreatePinnedToCore(zoneDetectTask, "ZoneDetect", 16384, nullptr, 1, nullptr, 0) != pdPASS)
  {
    Serial.println("ZoneDetect: Task-Erstellung fehlgeschlagen!");
  }
}

void zoneDetectRequestImmediateLookup()
{
  portENTER_CRITICAL(&sZoneDetectMux);
  sZoneDetectForceLookup = true;
  portEXIT_CRITICAL(&sZoneDetectMux);
}

bool zoneDetectGetStatus(char *timezone, size_t timezoneSize, char *country, size_t countrySize, bool &valid, bool &hasGpsPosition, unsigned long &lastUpdateMs, float &latitude, float &longitude)
{
  bool result = false;
  portENTER_CRITICAL(&sZoneDetectMux);
  valid = sZoneDetectValid;
  hasGpsPosition = sZoneDetectGpsValid;
  lastUpdateMs = sZoneDetectLastUpdateMs;
  if (timezone && timezoneSize > 0)
  {
    strncpy(timezone, sZoneDetectTimezone, timezoneSize - 1);
    timezone[timezoneSize - 1] = '\0';
  }
  if (country && countrySize > 0)
  {
    strncpy(country, sZoneDetectCountry, countrySize - 1);
    country[countrySize - 1] = '\0';
  }

  if (sZoneDetectHasLastPosition)
  {
    latitude = sZoneDetectLastLatitude;
    longitude = sZoneDetectLastLongitude;
  }
  else if (sZoneDetectGpsValid)
  {
    latitude = sZoneDetectGpsLatitude;
    longitude = sZoneDetectGpsLongitude;
  }
  else
  {
    latitude = 0.0f;
    longitude = 0.0f;
  }

  result = true;
  portEXIT_CRITICAL(&sZoneDetectMux);
  return result;
}

bool zoneDetectIsDatabaseLoaded()
{
  return sZoneDetectDbLoaded;
}

void zoneDetectUpdateGpsCoordinates(double latitude, double longitude, bool valid)
{
  portENTER_CRITICAL(&sZoneDetectGpsMux);
  sZoneDetectGpsLatitude = latitude;
  sZoneDetectGpsLongitude = longitude;
  sZoneDetectGpsValid = valid;
  portEXIT_CRITICAL(&sZoneDetectGpsMux);
}


#endif // ZONEDETECTTASK_H
