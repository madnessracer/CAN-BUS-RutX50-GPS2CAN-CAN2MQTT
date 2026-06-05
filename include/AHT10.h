#ifndef AHT10_H
#define AHT10_H

#include <Arduino.h>
#include <Wire.h>
#include "Can-Bus IDs.h"
#include "CAN_SUBs.h"
#include "ErrorLog.h"
#include "GPSSerial.h"

enum AHT10State
{
  AHT10_IDLE,
  AHT10_MEASURING
};

static bool aht10Enabled = true;
static float aht10LastTemperature = NAN;
static float aht10LastHumidity = NAN;
static unsigned long aht10LastReadMillis = 0;
static AHT10State aht10State = AHT10_IDLE;
static unsigned long aht10MeasureStartMillis = 0;
static bool aht10PendingRequest = false;
static uint32_t aht10LastGpsDateTime = 0;

static bool startAHT10Measurement()
{
  Wire.beginTransmission(0x38);
  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0)
  {
    return false;
  }

  aht10MeasureStartMillis = millis();
  aht10State = AHT10_MEASURING;
  return true;
}

static bool readAHT10Result(float &temperature, float &humidity)
{
  if (Wire.requestFrom((uint8_t)0x38, (uint8_t)6) != 6)
  {
    return false;
  }

  uint8_t data[6];
  for (uint8_t i = 0; i < 6; ++i)
  {
    data[i] = Wire.read();
  }

  if (data[0] & 0x80)
  {
    return false;
  }

  uint32_t rawHumidity = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | ((uint32_t)data[3] >> 4);
  uint32_t rawTemperature = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | (uint32_t)data[5];

  humidity = (rawHumidity * 100.0f) / 1048576.0f;
  temperature = (rawTemperature * 200.0f) / 1048576.0f - 50.0f;
  return true;
}

static inline uint32_t packDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
  uint32_t val = 0;
  val |= ((year - 2000) & 0x3F) << 0; // Jahr: Bit 0-5
  val |= (month & 0x0F) << 6;         // Monat: Bit 6-9
  val |= (day & 0x1F) << 10;          // Tag: Bit 10-14
  val |= (hour & 0x1F) << 15;         // Stunde: Bit 15-19
  val |= (minute & 0x3F) << 20;       // Minute: Bit 20-25
  val |= (second & 0x3F) << 26;       // Sekunde: Bit 26-31
  return val;
}

static inline bool gpsDateTimeFromGps(uint16_t &year, uint8_t &month, uint8_t &day, uint8_t &hour, uint8_t &minute, uint8_t &second)
{
  if (!gpsSerialData.validGPRMC || gpsSerialData.status[0] != 'A')
    return false;

  if (strlen(gpsSerialData.date) < 6 || strlen(gpsSerialData.utcTime) < 6)
    return false;

  const char *date = gpsSerialData.date;
  const char *time = gpsSerialData.utcTime;
  if (!isdigit((unsigned char)date[0]) || !isdigit((unsigned char)date[1]) ||
      !isdigit((unsigned char)date[2]) || !isdigit((unsigned char)date[3]) ||
      !isdigit((unsigned char)date[4]) || !isdigit((unsigned char)date[5]) ||
      !isdigit((unsigned char)time[0]) || !isdigit((unsigned char)time[1]) ||
      !isdigit((unsigned char)time[2]) || !isdigit((unsigned char)time[3]) ||
      !isdigit((unsigned char)time[4]) || !isdigit((unsigned char)time[5]))
  {
    return false;
  }

  day = (uint8_t)((date[0] - '0') * 10 + (date[1] - '0'));
  month = (uint8_t)((date[2] - '0') * 10 + (date[3] - '0'));
  uint8_t yearOffset = (uint8_t)((date[4] - '0') * 10 + (date[5] - '0'));
  year = 2000 + yearOffset;

  hour = (uint8_t)((time[0] - '0') * 10 + (time[1] - '0'));
  minute = (uint8_t)((time[2] - '0') * 10 + (time[3] - '0'));
  second = (uint8_t)((time[4] - '0') * 10 + (time[5] - '0'));

  return true;
}

static void sendAHT10ToCan(float temperature, float humidity)
{
  uint8_t tempByte = (uint8_t)constrain(round(temperature + 50.0f), 0.0f, 255.0f);
  uint8_t humidityByte = (uint8_t)constrain(round(humidity), 0.0f, 100.0f);

  uint16_t year;
  uint8_t month, day, hour, minute, second;
  if (gpsDateTimeFromGps(year, month, day, hour, minute, second))
  {
    uint32_t dateTime = packDateTime(year, month, day, hour, minute, second);
    if (dateTime != aht10LastGpsDateTime)
    {
      aht10LastGpsDateTime = dateTime;
      CAN_SendEx(true, 6, MessageBasisID,
                 (uint8_t)dateTime,
                 (uint8_t)(dateTime >> 8),
                 (uint8_t)(dateTime >> 16),
                 (uint8_t)(dateTime >> 24),
                 tempByte,
                 humidityByte);
      return;
    }
  }

  CAN_SendEx(true, 2, MessageBasisID, tempByte, humidityByte);
}

static void finishAHT10Measurement()
{
  float temperature = 0.0f;
  float humidity = 0.0f;
  if (!readAHT10Result(temperature, humidity))
  {
    errorLogAddOnce("AHT10_FAIL", "AHT10 I2C-Lesen fehlgeschlagen.");
  }
  else
  {
    if (errorLogIsActive("AHT10_FAIL"))
    {
      errorLogResolve("AHT10_FAIL");
    }

    aht10LastTemperature = temperature;
    aht10LastHumidity = humidity;
    aht10LastReadMillis = millis();
    sendAHT10ToCan(temperature, humidity);
  }

  aht10State = AHT10_IDLE;
  aht10PendingRequest = false;
}

static void updateAHT10()
{
  unsigned long now = millis();

  if (aht10State == AHT10_MEASURING)
  {
    if (now - aht10MeasureStartMillis >= 80)
    {
      finishAHT10Measurement();
    }
    return;
  }

  if (!aht10Enabled)
  {
    return;
  }

  if (now - aht10LastReadMillis >= 1000 && !aht10PendingRequest)
  {
    if (startAHT10Measurement())
    {
      aht10PendingRequest = true;
    }
    else
    {
      errorLogAddOnce("AHT10_FAIL", "AHT10 I2C Start fehlgeschlagen.");
    }
  }
}

static void printAHT10Status()
{
  Serial.print("\n=== AHT10 Status ===\n");
  Serial.printf("AHT10 aktiv: %s\n", aht10Enabled ? "ja" : "nein");
  Serial.printf("AHT10 Zustand: %s\n", aht10State == AHT10_MEASURING ? "Messung laeuft" : "bereit");
  if (!isnan(aht10LastTemperature))
  {
    Serial.printf("Letzte Temperatur: %.1f °C\n", aht10LastTemperature);
  }
  else
  {
    Serial.print("Letzte Temperatur: n/a\n");
  }

  if (!isnan(aht10LastHumidity))
  {
    Serial.printf("Letzte Luftfeuchte: %.1f %%\n", aht10LastHumidity);
  }
  else
  {
    Serial.print("Letzte Luftfeuchte: n/a\n");
  }
  Serial.print("===================\n");
}

#endif // AHT10_H
