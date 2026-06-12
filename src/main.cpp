#include <Arduino.h>

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <config.h>
#include <FS.h>
#include <LittleFS.h>
#include "Can-Bus IDs.h"
#include "driver/twai.h"
#include <Wire.h>
#include <esp_task_wdt.h> // *** WATCHDOG
#include "Ws2812.h"

#define WDT_TIMEOUT_SEC 10 // *** WATCHDOG

#include <FileSystem.h>
#include "AHT10.h"
#include <CAN_SUBs.h>
#include <ErrorLog.h>
#include <CANSerial.h>
#include <Wlan_Config.h>
#include <CAN_INPUT_OTA.h>
#include "WebTerminal.h"
#include "GPSSerial.h"
#include "UnixTimeClock.h"
#include "DebugMenu.h"
#include "CAN-OTA.h"
#include "CANPing.h"

HardwareSerial *can_tx_ser = &Serial;
HardwareSerial *can_rx_ser = &Serial1;
CanLiveMode canLiveMode = CAN_LIVE_OFF;
bool canLiveModeWeb = false;
bool gpsLiveMode = false;
bool gpsLiveRawModeEnabled = false;
bool gpsLiveModeWeb = false;
bool gpsLiveRawModeWeb = false;
GPSSerialData gpsSerialData;

static const uint32_t canIdsToSerial[] = {
    0x320,
    0x334};

static uint8_t statusLedR = 0;
static uint8_t statusLedG = 0;
static uint8_t statusLedB = 0;
static unsigned long statusLedInterval = 0;
static unsigned long statusLedDuration = 0;

static unsigned long getEffectiveBlinkInterval()
{
  unsigned long interval = ws2812GetInterval();
  return interval > 0 ? interval : 3000;
}

static unsigned long getEffectiveBlinkDuration()
{
  unsigned long duration = ws2812GetDuration();
  return duration > 0 ? duration : 50;
}

static void gpsSerialPrintLiveDataToWebTerminal()
{
  char utcFormatted[16] = "";
  gpsSerialFormatUtcTime(gpsSerialData.utcTime, utcFormatted, sizeof(utcFormatted));
  webTerminalAppendFormat("UTC Time       : %s", gpsSerialValueOrDash(utcFormatted));
  webTerminalAppendFormat("Status         : %s", gpsSerialValueOrDash(gpsSerialData.status));

  char latFormatted[32] = "";
  gpsSerialFormatLatLong(gpsSerialData.latitude, gpsSerialData.latitudeDir, latFormatted, sizeof(latFormatted));
  webTerminalAppendFormat("Latitude       : %s", gpsSerialValueOrDash(latFormatted));

  char lonFormatted[32] = "";
  gpsSerialFormatLatLong(gpsSerialData.longitude, gpsSerialData.longitudeDir, lonFormatted, sizeof(lonFormatted));
  webTerminalAppendFormat("Longitude      : %s", gpsSerialValueOrDash(lonFormatted));

  webTerminalAppendFormat("Fix Quality    : %s", gpsSerialValueOrDash(gpsSerialData.fixQuality));
  webTerminalAppendFormat("Satellites     : %s", gpsSerialValueOrDash(gpsSerialData.satellites));
  webTerminalAppendFormat("HDOP           : %s", gpsSerialValueOrDash(gpsSerialData.hdop));
  webTerminalAppendFormat("Altitude       : %s", gpsSerialValueOrDash(gpsSerialData.altitude));

  char speedKmH[16] = "";
  if (gpsSerialData.speedKnots[0] != '\0')
  {
    float knots = atof(gpsSerialData.speedKnots);
    float kmh = knots * 1.852f;
    snprintf(speedKmH, sizeof(speedKmH), "%.2f", kmh);
  }
  webTerminalAppendFormat("Speed (km/h)   : %s", gpsSerialValueOrDash(speedKmH));
  webTerminalAppendFormat("Track Angle    : %s", gpsSerialValueOrDash(gpsSerialData.trackAngle));

  char dateFormatted[16] = "";
  gpsSerialFormatDate(gpsSerialData.date, dateFormatted, sizeof(dateFormatted));
  webTerminalAppendFormat("Date           : %s", gpsSerialValueOrDash(dateFormatted));
  webTerminalAppendFormat("Mag Var        : %s %s", gpsSerialValueOrDash(gpsSerialData.magneticVariation), gpsSerialValueOrDash(gpsSerialData.magneticVariationDir));
  webTerminalAppendFormat("Mode Indicator : %s", gpsSerialValueOrDash(gpsSerialData.modeIndicator));
}

static void setStatusLedBlink(uint8_t r, uint8_t g, uint8_t b)
{
  unsigned long interval = getEffectiveBlinkInterval();
  unsigned long duration = getEffectiveBlinkDuration();

  if (statusLedR == r && statusLedG == g && statusLedB == b &&
      statusLedInterval == interval && statusLedDuration == duration &&
      ws2812GetMode() == WS2812_BLINK)
  {
    return;
  }

  statusLedR = r;
  statusLedG = g;
  statusLedB = b;
  statusLedInterval = interval;
  statusLedDuration = duration;

  ws2812SetBlinkRGB(r, g, b, interval, duration);
}

static void updateCanStatusLed()
{
  bool hasCanError = errorLogIsActive("CAN_BUS_OFF") ||
                     errorLogIsActive("CAN_STATUS_FAIL") ||
                     errorLogIsActive("CAN_ERR_WARN") ||
                     errorLogIsActive("CAN_TX_FAIL");

  if (hasCanError)
  {
    setStatusLedBlink(255, 0, 0);
    return;
  }

  if (errorLogHasEntries())
  {
    setStatusLedBlink(255, 255, 0);
    return;
  }

  setStatusLedBlink(0, 255, 0);
}

void setup()
{

  Serial.begin(115200);
  Serial1.begin(CAN_BAUD_RATE, SERIAL_8N1, CAN_RX_SER, CAN_TX_SER);
  can_tx_ser = &Serial1;
  can_rx_ser = &Serial1;
  Serial2.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_SER, GPS_TX_SER);
  gpsSerialResetData();

  clearTerminalScreen();
  loadDebugAutoExitSetting();

  Serial.println("------------------------------------------------");
  Serial.println();

  // Reset-Grund ausgeben (hilft bei Diagnose von Reboot-Schleifen)
  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.print("Reset-Grund: ");
  switch (resetReason)
  {
  case ESP_RST_POWERON:
    Serial.println("Einschalten");
    break;
  case ESP_RST_EXT:
    Serial.println("Externer Reset");
    break;
  case ESP_RST_SW:
    Serial.println("Software-Reset");
    break;
  case ESP_RST_PANIC:
    Serial.println("PANIC / Exception!");
    break;
  case ESP_RST_INT_WDT:
    Serial.println("Interrupt-Watchdog!");
    break;
  case ESP_RST_TASK_WDT:
    Serial.println("Task-Watchdog!");
    break;
  case ESP_RST_WDT:
    Serial.println("Sonstiger Watchdog!");
    break;
  case ESP_RST_DEEPSLEEP:
    Serial.println("Deep-Sleep Aufwachen");
    break;
  case ESP_RST_BROWNOUT:
    Serial.println("Brownout (Unterspannung)!");
    break;
  default:
    Serial.printf("Unbekannt (%d)\n", (int)resetReason);
    break;
  }

  Serial.println("Bootvorgang gestartet...");

  ws2812Init();

  incrementBootCount();

  Serial.printf("Firmware-Version: %lu, Boot-Zaehler: %lu\n", getFirmwareVersion(), getBootCount());

  pinMode(CAN_SE_PIN, OUTPUT);
  digitalWrite(CAN_SE_PIN, LOW);

  Wire.begin(SDA, SCL);
  Wire.setClock(100000);
  pinMode(PCA9555_INT_PIN, INPUT_PULLUP);
  FS_Open();
  zoneDetectTaskInit();
  if (PCA9555_Init())
  {
    Serial.println("PCA9555 initialisiert");
  }
  else
  {
    Serial.println("PCA9555 nicht gefunden");
  }
  delay(100); // Kurze Verzögerung, um sicherzustellen, dass der Transceiver bereit ist
  Serial.println("Can Transceiver aktiv");

  // CAN-Bus initialisieren mit 250 kbit/s
  if (!CAN_Init((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, s_rxQueueLen))
  {
    Serial.println("Fehler: TWAI Initialisierung fehlgeschlagen");
  }

  // CAN-OTA initialisieren, nachdem CAN/TWAI gestartet ist.
  CAN_OTA::init(OtaDevieID);
  Serial.println("CAN-OTA bereit (Node-ID: " + String(OtaDevieID) + ")");

  CANPing::init(OtaDevieID);

  CAN_SendEx(true, 1, IP_Send_to_CAN, 0x03);

  webTerminalBegin();
  if (webTerminalLoadSavedState())
  {
    webTerminalSetEnabled(true);
  }

  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
  esp_task_wdt_add(NULL);

  ws2812SetBlinkRGB(0, 255, 0, 3000, 50);
  Serial.println("Watchdog aktiviert (10s Timeout)");
}

void loop()
{
  esp_task_wdt_reset();

  // CANPing Fast-Mode: maximal kurze Antwortzeit, alles andere aussetzen.
  if (CANPing::isFastMode())
  {
    if (Serial.available() > 0)
    {
      while (Serial.available() > 0)
      {
        Serial.read();
      }
      CANPing::stopFastModeAndDisable();
      return;
    }

    while (twai_receive(&rx_frame, 0) == ESP_OK)
    {
      if (rx_frame.extd)
      {
        CANPing::processMessage(rx_frame.identifier, rx_frame.data, rx_frame.data_length_code);
      }
    }
    return;
  }

  // CAN-OTA: Timeout überwachen, auch wenn aktuell keine Frames ankommen.
  CAN_OTA::tick();

  // CAN-OTA: Während eines Updates alles andere ignorieren – nur CAN-Frames für OTA verarbeiten
  if (CAN_OTA::isUpdateMode())
  {
    // WebTerminal beenden – nach OTA-Update wird der ESP neu gestartet
    if (g_webTerminalEnabled)
    {
      webTerminalSetEnabled(false);
    }
    while (twai_receive(&rx_frame, 0) == ESP_OK)
    {
      if (rx_frame.extd)
      {
        CAN_OTA::processMessage(rx_frame.identifier, rx_frame.data, rx_frame.data_length_code, true);
      }
    }
    return; // Alles andere (FRAM, Sensoren, CAN-Senden, Debug) überspringen
  }

  static unsigned long lastCanStatusCheck = 0;
  if (millis() - lastCanStatusCheck >= 1000)
  {
    lastCanStatusCheck = millis();
    if (!CAN_CheckBus((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, s_rxQueueLen))
    {
      Serial.println("Warnung: CAN Statuspruefung fehlgeschlagen.");
    }
    updateCanStatusLed();
  }

  if (OTA_On == 1)
  {
    ArduinoOTA.handle();
    if (OTA_ActivatedAt > 0 && OTA_AutoOffIntervalMs > 0 && millis() - OTA_ActivatedAt >= OTA_AutoOffIntervalMs)
    {
      Serial.printf("OTA Auto-Off: %lu Minuten erreicht.\n", OTA_AutoOffIntervalMs / 60000UL);
      OTA_Stop();
      // OTA wurde beendet, normale Verarbeitung kann weiterlaufen.
    }
    else if (OTA_UploadActive())
    {
      // Upload läuft aktuell. Nur WebTerminal weiter verarbeiten.
      webTerminalLoop();
      return;
    }
  }

  updateAHT10();

  handleSerialCanInput();

  handleDebugActivation();

  processDebugMenu();
  webTerminalLoop();

  static char gpsLineBuf[128];
  static size_t gpsLineLen = 0;
  while (Serial2.available())
  {
    int c = Serial2.read();
    if (c < 0)
      break;
    if (c == '\r')
      continue;
    if (c == '\n')
    {
      if (gpsLineLen > 0)
      {
        gpsLineBuf[gpsLineLen] = '\0';
        bool isGPRMC = strncmp(gpsLineBuf, GPSSERIAL_PREFIX_GPRMC, sizeof(GPSSERIAL_PREFIX_GPRMC) - 1) == 0;
        bool isGPGGA = strncmp(gpsLineBuf, GPSSERIAL_PREFIX_GPGGA, sizeof(GPSSERIAL_PREFIX_GPGGA) - 1) == 0;
        if (isGPRMC || isGPGGA)
        {
          gpsSerialParseLine(gpsLineBuf);
          unixTimeClockLoop();

          double latDeg = 0.0;
          double lonDeg = 0.0;
          bool validLat = gpsSerialParseNmeaLatLong(gpsSerialData.latitude, latDeg);
          bool validLon = gpsSerialParseNmeaLatLong(gpsSerialData.longitude, lonDeg);
          if (gpsSerialData.latitudeDir[0] == 'S') latDeg = -latDeg;
          if (gpsSerialData.longitudeDir[0] == 'W') lonDeg = -lonDeg;

          bool hasFix = (gpsSerialData.status[0] == 'A') || (gpsSerialData.fixQuality[0] > '0');
          if (hasFix && validLat && validLon)
          {
            zoneDetectUpdateGpsCoordinates(latDeg, lonDeg, true);
          }
          else
          {
            zoneDetectUpdateGpsCoordinates(0.0, 0.0, false);
          }
        }

        if (debugMenuState == DEBUG_GPS_LIVE)
        {
          if (gpsLiveRawModeEnabled)
          {
            Serial.println(gpsLineBuf);
          }
          else if (isGPRMC || isGPGGA)
          {
            if (isGPRMC)
            {
              clearTerminalScreen();
              gpsSerialPrintLiveData();
            }
          }
        }
        else if (gpsLiveModeWeb)
        {
          if (isGPRMC && !gpsLiveRawModeWeb)
          {
            g_webTerminalOutput = "";
          }
          if (gpsLiveRawModeWeb)
          {
            webTerminalAppendFormat("%s", gpsLineBuf);
          }
          else if (isGPRMC || isGPGGA)
          {
            gpsSerialPrintLiveDataToWebTerminal();
          }
        }

        gpsLineLen = 0;
      }
      continue;
    }
    if (gpsLineLen < sizeof(gpsLineBuf) - 1)
    {
      gpsLineBuf[gpsLineLen++] = (char)c;
    }
    else
    {
      gpsLineLen = 0;
    }
  }

  // Empfange CAN-Nachricht mit Timeout
  if (twai_receive(&rx_frame, pdMS_TO_TICKS(3)) == ESP_OK)
  {
    if (rx_frame.extd) // Prüfe auf Extended Frame
    {

      // CAN Live Mode: Zeige auch normale TWAI-Empfangsdaten.
      if (canLiveTwaiEnabled())
      {
        logCanTwaiFrame(rx_frame);
      }

      // CAN-OTA: START-Kommando kann im Normalmodus ankommen → zuerst prüfen
      CAN_OTA::processMessage(rx_frame);
      if (CAN_OTA::isUpdateMode())
        return; // OTA gestartet, normale Verarbeitung überspringen

      // CANPing: Ping-Request beantworten
      CANPing::processMessage(rx_frame.identifier, rx_frame.data, rx_frame.data_length_code);

      CanInputOTA();

      bool forwardToSerial = false;
      for (uint8_t i = 0; i < sizeof(canIdsToSerial) / sizeof(canIdsToSerial[0]); ++i)
      {
        if (rx_frame.identifier == canIdsToSerial[i])
        {
          forwardToSerial = true;
          break;
        }
      }

      if (forwardToSerial)
      {
        sendCanMessageToCanTxSer(rx_frame);
      }

      if (rx_frame.identifier == SteuerID)
      {
        if (rx_frame.data_length_code >= 2 && rx_frame.data[0] == 0x01 && rx_frame.data[1] == 0x00)
        {
          OTA_Stop();
        }
        else if (rx_frame.data_length_code >= 2 && rx_frame.data[0] == 0x01 && rx_frame.data[1] == 0x01)
        {
          OTA_Start();
        }
        else if (rx_frame.data_length_code >= 2 && rx_frame.data[0] == 0x01 && rx_frame.data[1] == 0x02)
        {
          webTerminalSetEnabled(false);
        }
        else if (rx_frame.data_length_code >= 2 && rx_frame.data[0] == 0x01 && rx_frame.data[1] == 0x03)
        {
          webTerminalSetEnabled(true);
        }
      }
      else if (rx_frame.identifier == FetSteuerID)
      {
        if (rx_frame.data_length_code >= 2)
        {
          uint8_t fetNum = rx_frame.data[0];
          uint8_t fetState = rx_frame.data[1];
          if (fetNum >= 1 && fetNum <= 8 && (fetState == 0x00 || fetState == 0x01))
          {
            if (!PCA9555_SetOutput(fetNum - 1, fetState == 0x01))
            {
              Serial.printf("FET%d CAN-Steuerung fehlgeschlagen\n", fetNum);
            }
          }
          else
          {
            Serial.println("Ungueltige FET CAN-Steuerungsdaten");
          }
        }
      }
    }
  }
}