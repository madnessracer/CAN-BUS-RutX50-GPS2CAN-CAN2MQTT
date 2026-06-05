#ifndef GPS_SERIAL_H
#define GPS_SERIAL_H

#include <Arduino.h>

static const char GPSSERIAL_PREFIX_GPGGA[] = "$GPGGA";
static const char GPSSERIAL_PREFIX_GPRMC[] = "$GPRMC";

struct GPSSerialData
{
  bool validGPGGA;
  bool validGPRMC;
  char rawGPGGA[128];
  char rawGPRMC[128];
  char utcTime[16];
  char status[4];
  char latitude[16];
  char latitudeDir[2];
  char longitude[16];
  char longitudeDir[2];
  char fixQuality[4];
  char satellites[4];
  char hdop[8];
  char altitude[12];
  char speedKnots[12];
  char trackAngle[12];
  char date[8];
  char magneticVariation[12];
  char magneticVariationDir[2];
  char modeIndicator[4];
};

extern GPSSerialData gpsSerialData;

static inline void gpsSerialResetData()
{
  gpsSerialData.validGPGGA = false;
  gpsSerialData.validGPRMC = false;
  gpsSerialData.rawGPGGA[0] = '\0';
  gpsSerialData.rawGPRMC[0] = '\0';
  gpsSerialData.utcTime[0] = '\0';
  gpsSerialData.status[0] = '\0';
  gpsSerialData.latitude[0] = '\0';
  gpsSerialData.latitudeDir[0] = '\0';
  gpsSerialData.longitude[0] = '\0';
  gpsSerialData.longitudeDir[0] = '\0';
  gpsSerialData.fixQuality[0] = '\0';
  gpsSerialData.satellites[0] = '\0';
  gpsSerialData.hdop[0] = '\0';
  gpsSerialData.altitude[0] = '\0';
  gpsSerialData.speedKnots[0] = '\0';
  gpsSerialData.trackAngle[0] = '\0';
  gpsSerialData.date[0] = '\0';
  gpsSerialData.magneticVariation[0] = '\0';
  gpsSerialData.magneticVariationDir[0] = '\0';
  gpsSerialData.modeIndicator[0] = '\0';
}

static inline void gpsSerialCopyField(char *dest, const char *src, size_t destSize)
{
  if (destSize == 0)
    return;
  if (src == nullptr)
  {
    dest[0] = '\0';
    return;
  }
  strncpy(dest, src, destSize - 1);
  dest[destSize - 1] = '\0';
}

static inline void gpsSerialCopyFieldIfEmpty(char *dest, const char *src, size_t destSize)
{
  if (dest == nullptr || destSize == 0)
    return;
  if (dest[0] != '\0')
    return;
  gpsSerialCopyField(dest, src, destSize);
}

static inline const char *gpsSerialValueOrDash(const char *value)
{
  return (value != nullptr && value[0] != '\0') ? value : "-";
}

static inline void gpsSerialFormatUtcTime(const char *utc, char *out, size_t outSize)
{
  if (outSize == 0)
    return;
  if (utc == nullptr || utc[0] == '\0')
  {
    out[0] = '\0';
    return;
  }

  // hhmmss[.sss]
  if (strlen(utc) >= 6 && isdigit((unsigned char)utc[0]) && isdigit((unsigned char)utc[1]) && isdigit((unsigned char)utc[2]) && isdigit((unsigned char)utc[3]) && isdigit((unsigned char)utc[4]) && isdigit((unsigned char)utc[5]))
  {
    snprintf(out, outSize, "%c%c:%c%c:%c%c", utc[0], utc[1], utc[2], utc[3], utc[4], utc[5]);
  }
  else
  {
    gpsSerialCopyField(out, utc, outSize);
  }
}

static inline void gpsSerialFormatDate(const char *date, char *out, size_t outSize)
{
  if (outSize == 0)
    return;
  if (date == nullptr || date[0] == '\0')
  {
    out[0] = '\0';
    return;
  }

  // ddmmyy
  if (strlen(date) >= 6 && isdigit((unsigned char)date[0]) && isdigit((unsigned char)date[1]) && isdigit((unsigned char)date[2]) && isdigit((unsigned char)date[3]) && isdigit((unsigned char)date[4]) && isdigit((unsigned char)date[5]))
  {
    int year = (date[4] - '0') * 10 + (date[5] - '0');
    snprintf(out, outSize, "%c%c.%c%c.%02d", date[0], date[1], date[2], date[3], year);
  }
  else
  {
    gpsSerialCopyField(out, date, outSize);
  }
}

static inline bool gpsSerialParseNmeaLatLong(const char *value, double &outDegrees)
{
  if (value == nullptr || value[0] == '\0')
    return false;

  size_t len = strlen(value);
  const char *dot = strchr(value, '.');
  if (dot == nullptr || dot == value || dot == value + 1)
    return false;

  int degreesDigits = (dot - value > 4) ? 3 : 2;
  if (degreesDigits >= (int)len)
    return false;

  char degStr[8] = {0};
  char minStr[16] = {0};
  strncpy(degStr, value, degreesDigits);
  strncpy(minStr, value + degreesDigits, sizeof(minStr) - 1);

  double degrees = atof(degStr);
  double minutes = atof(minStr);
  outDegrees = degrees + minutes / 60.0;
  return true;
}

static inline void gpsSerialFormatLatLong(const char *value, const char *dir, char *out, size_t outSize)
{
  if (outSize == 0)
    return;
  if (value == nullptr || value[0] == '\0')
  {
    out[0] = '\0';
    return;
  }

  double deg = 0.0;
  if (gpsSerialParseNmeaLatLong(value, deg))
  {
    if (dir != nullptr && dir[0] != '\0')
      snprintf(out, outSize, "%.4f %s", deg, dir);
    else
      snprintf(out, outSize, "%.4f", deg);
  }
  else
  {
    gpsSerialCopyField(out, value, outSize);
  }
}

static inline void gpsSerialPrintLiveData()
{
  char utcFormatted[16] = "";
  gpsSerialFormatUtcTime(gpsSerialData.utcTime, utcFormatted, sizeof(utcFormatted));
  Serial.printf("UTC Time       : %s\n", gpsSerialValueOrDash(utcFormatted));
  Serial.printf("Status         : %s\n", gpsSerialValueOrDash(gpsSerialData.status));
  char latFormatted[32] = "";
  gpsSerialFormatLatLong(gpsSerialData.latitude, gpsSerialData.latitudeDir, latFormatted, sizeof(latFormatted));
  Serial.printf("Latitude       : %s\n", gpsSerialValueOrDash(latFormatted));
  char lonFormatted[32] = "";
  gpsSerialFormatLatLong(gpsSerialData.longitude, gpsSerialData.longitudeDir, lonFormatted, sizeof(lonFormatted));
  Serial.printf("Longitude      : %s\n", gpsSerialValueOrDash(lonFormatted));
  Serial.printf("Fix Quality    : %s\n", gpsSerialValueOrDash(gpsSerialData.fixQuality));
  Serial.printf("Satellites     : %s\n", gpsSerialValueOrDash(gpsSerialData.satellites));
  Serial.printf("HDOP           : %s\n", gpsSerialValueOrDash(gpsSerialData.hdop));
  Serial.printf("Altitude       : %s\n", gpsSerialValueOrDash(gpsSerialData.altitude));
  char speedKmH[16] = "";
  if (gpsSerialData.speedKnots[0] != '\0')
  {
    float knots = atof(gpsSerialData.speedKnots);
    float kmh = knots * 1.852f;
    snprintf(speedKmH, sizeof(speedKmH), "%.2f", kmh);
  }
  Serial.printf("Speed (km/h)   : %s\n", gpsSerialValueOrDash(speedKmH));
  Serial.printf("Track Angle    : %s\n", gpsSerialValueOrDash(gpsSerialData.trackAngle));
  char dateFormatted[16] = "";
  gpsSerialFormatDate(gpsSerialData.date, dateFormatted, sizeof(dateFormatted));
  Serial.printf("Date           : %s\n", gpsSerialValueOrDash(dateFormatted));
  Serial.printf("Mag Var        : %s %s\n", gpsSerialValueOrDash(gpsSerialData.magneticVariation), gpsSerialValueOrDash(gpsSerialData.magneticVariationDir));
  Serial.printf("Mode Indicator : %s\n", gpsSerialValueOrDash(gpsSerialData.modeIndicator));
}

static inline void gpsSerialParseLine(const char *line)
{
  if (line == nullptr || line[0] != '$')
    return;

  char buffer[128];
  strncpy(buffer, line, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  char *fields[24] = {0};
  int fieldCount = 0;
  fields[fieldCount++] = buffer;
  for (char *p = buffer; *p && fieldCount < (int)sizeof(fields) / sizeof(fields[0]); ++p)
  {
    if (*p == ',')
    {
      *p = '\0';
      fields[fieldCount++] = p + 1;
    }
  }

  if (strcmp(fields[0], GPSSERIAL_PREFIX_GPGGA) == 0)
  {
    gpsSerialData.validGPGGA = true;
    gpsSerialCopyField(gpsSerialData.rawGPGGA, line, sizeof(gpsSerialData.rawGPGGA));
    gpsSerialCopyFieldIfEmpty(gpsSerialData.utcTime, fields[1], sizeof(gpsSerialData.utcTime));
    gpsSerialCopyFieldIfEmpty(gpsSerialData.latitude, fields[2], sizeof(gpsSerialData.latitude));
    gpsSerialCopyFieldIfEmpty(gpsSerialData.latitudeDir, fields[3], sizeof(gpsSerialData.latitudeDir));
    gpsSerialCopyFieldIfEmpty(gpsSerialData.longitude, fields[4], sizeof(gpsSerialData.longitude));
    gpsSerialCopyFieldIfEmpty(gpsSerialData.longitudeDir, fields[5], sizeof(gpsSerialData.longitudeDir));
    gpsSerialCopyField(gpsSerialData.fixQuality, fields[6], sizeof(gpsSerialData.fixQuality));
    gpsSerialCopyField(gpsSerialData.satellites, fields[7], sizeof(gpsSerialData.satellites));
    gpsSerialCopyField(gpsSerialData.hdop, fields[8], sizeof(gpsSerialData.hdop));
    gpsSerialCopyField(gpsSerialData.altitude, fields[9], sizeof(gpsSerialData.altitude));
  }
  else if (strcmp(fields[0], GPSSERIAL_PREFIX_GPRMC) == 0)
  {
    gpsSerialData.validGPRMC = true;
    gpsSerialCopyField(gpsSerialData.rawGPRMC, line, sizeof(gpsSerialData.rawGPRMC));
    gpsSerialCopyField(gpsSerialData.utcTime, fields[1], sizeof(gpsSerialData.utcTime));
    gpsSerialCopyField(gpsSerialData.status, fields[2], sizeof(gpsSerialData.status));
    gpsSerialCopyField(gpsSerialData.latitude, fields[3], sizeof(gpsSerialData.latitude));
    gpsSerialCopyField(gpsSerialData.latitudeDir, fields[4], sizeof(gpsSerialData.latitudeDir));
    gpsSerialCopyField(gpsSerialData.longitude, fields[5], sizeof(gpsSerialData.longitude));
    gpsSerialCopyField(gpsSerialData.longitudeDir, fields[6], sizeof(gpsSerialData.longitudeDir));
    gpsSerialCopyField(gpsSerialData.speedKnots, fields[7], sizeof(gpsSerialData.speedKnots));
    gpsSerialCopyField(gpsSerialData.trackAngle, fields[8], sizeof(gpsSerialData.trackAngle));
    gpsSerialCopyField(gpsSerialData.date, fields[9], sizeof(gpsSerialData.date));
    gpsSerialCopyField(gpsSerialData.magneticVariation, fields[10], sizeof(gpsSerialData.magneticVariation));
    gpsSerialCopyField(gpsSerialData.magneticVariationDir, fields[11], sizeof(gpsSerialData.magneticVariationDir));
    gpsSerialCopyField(gpsSerialData.modeIndicator, fields[12], sizeof(gpsSerialData.modeIndicator));
  }
}

#endif // GPS_SERIAL_H
