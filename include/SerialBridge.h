#ifndef SERIAL_BRIDGE_H
#define SERIAL_BRIDGE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "FileSystem.h"
#include "CAN_SUBs.h"
#include "Wlan_Config.h"

extern int WiFi_Error;
extern const char WIFI_Name[];

constexpr uint16_t BRIDGE_PORT = 2323;

class SerialBridgeClass : public Stream
{
public:
  static constexpr unsigned long AUTO_LOGOFF_MS = 5UL * 60UL * 1000UL; // 5 Minuten

  SerialBridgeClass()
    : hwSerial(::Serial), server(BRIDGE_PORT), active(false), lastClientMillis(0)
  {
  }

  void begin(unsigned long baud)
  {
    hwSerial.begin(baud);
  }

  bool bridgeActive() const
  {
    return active;
  }

  bool clientConnected()
  {
    return client.connected();
  }

  bool beginBridge()
  {
    if (active)
      return true;

    wifi_needed_bridge = true;
    hwSerial.println("[BRIDGE] Verbindung wird aufgebaut...");
    CAN_SendEx(true, 1, IP_Send_to_CAN, 0x08);

    if (!WiFi_Connect())
    {
      wifi_needed_bridge = false;
      return false;
    }

    server.begin();
    active = true;
    lastClientMillis = millis();
    CAN_SendEx(true, 5, IP_Send_to_CAN, 0x09,
               WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
    hwSerial.printf("[BRIDGE] aktiv, IP=%s, Port=%u\n", WiFi.localIP().toString().c_str(), BRIDGE_PORT);
    return true;
  }

  void stopBridge()
  {
    if (!active)
      return;

    if (client && client.connected())
      client.stop();

    CAN_SendEx(true, 1, IP_Send_to_CAN, 0x0A);
    server.end();
    wifi_needed_bridge = false;
    WiFi_ReleaseIfUnneeded();
    active = false;
    hwSerial.println("[BRIDGE] deaktiviert.");
  }

  void handleBridge()
  {
    if (!active)
      return;

    if (client && !client.connected())
      client.stop();

    if ((!client || !client.connected()) && server.hasClient())
    {
      if (client && client.connected())
        client.stop();

      client = server.available();
      if (client)
      {
        client.setNoDelay(true);
        hwSerial.println("[BRIDGE] Verbindung hergestellt.");
        client.println("ESP Debug-Bridge verbunden.");
        client.println("Tippe 'debug' um das Debug-Menue zu starten.");
      }
    }

    if (clientConnected())
    {
      lastClientMillis = millis();
    }
    else if (millis() - lastClientMillis >= AUTO_LOGOFF_MS)
    {
      hwSerial.println("[BRIDGE] Kein Client mehr, auto logoff.");
      stopBridge();
    }
  }

  virtual int available() override
  {
    if (client && client.connected())
      return client.available();
    return hwSerial.available();
  }

  virtual int read() override
  {
    if (client && client.connected())
      return client.read();
    if (hwSerial.available())
      return hwSerial.read();
    return -1;
  }

  virtual int peek() override
  {
    if (client && client.connected())
      return client.peek();
    if (hwSerial.available())
      return hwSerial.peek();
    return -1;
  }

  virtual void flush() override
  {
    if (client && client.connected())
      client.flush();
    else
      hwSerial.flush();
  }

  virtual size_t write(int c)
  {
    return write((uint8_t)c);
  }

  virtual size_t write(uint8_t c) override
  {
    if (client && client.connected())
      return client.write(c);
    return hwSerial.write(c);
  }

  virtual size_t write(const uint8_t *buffer, size_t size) override
  {
    if (client && client.connected())
      return client.write(buffer, size);
    return hwSerial.write(buffer, size);
  }

  using Print::write;

  int printf(const char *format, ...)
  {
    char tmp[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(tmp, sizeof(tmp), format, args);
    va_end(args);
    if (len > 0)
      write((const uint8_t *)tmp, (size_t)len);
    return len;
  }

private:
  HardwareSerial &hwSerial;
  WiFiServer server;
  WiFiClient client;
  bool active;
  unsigned long lastClientMillis;
};

extern SerialBridgeClass SerialBridge;

#endif // SERIAL_BRIDGE_H
