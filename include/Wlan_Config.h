#ifndef __Wlan_Config_H__
#define __Wlan_Config_H__

#include <esp_task_wdt.h>

// WebTerminal status functions werden extern bereitgestellt.
void webTerminalAppendLine(const String &text);
void webTerminalAppendFormat(const char *format, ...);

const char *ssid;
const char *password;

static char ssid_buf[33] = {0};
static char password_buf[33] = {0};

byte OTA_On = 0;
unsigned long OTA_ActivatedAt = 0;
unsigned long OTA_AutoOffIntervalMs = 300000UL; // 5 Minuten Standard
static bool otaUploadActive = false;

inline bool OTA_UploadActive()
{
  return otaUploadActive;
}

int WiFi_Error = 0;
byte SSID_Speicher[50][33];
byte SSID_Byte_arrey[33];
String NeueSSID;

// Wer benötigt gerade WiFi?
static bool wifi_needed_ota = false;
static bool wifi_needed_webterm = false;

// Stellt WiFi-Verbindung her. Gibt true zurück wenn verbunden.
static bool WiFi_Connect()
{
  if (WiFi.status() == WL_CONNECTED) return true;

  String SSID_str = SSID_Lesen();
  SSID_str.toCharArray(ssid_buf, sizeof(ssid_buf));
  ssid = ssid_buf;

  String PASSWORD_str = PASSWORD_Lesen();
  PASSWORD_str.toCharArray(password_buf, sizeof(password_buf));
  password = password_buf;

  WiFi.mode(WIFI_STA); // sicherstellen dass STA-Modus aktiv ist (auch nach WiFi.mode(WIFI_OFF))
  WiFi.hostname(WIFI_Name);
  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setSleep(false); // WLAN-Powersave für OTA-Uploads abschalten

  unsigned long wifiStart = millis();
  const unsigned long wifiTimeout = 20000;
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < wifiTimeout)
  {
    esp_task_wdt_reset();
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi Verbindung fehlgeschlagen.");
    CAN_Send(IP_Send_to_CAN, 0x04);
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    return false;
  }

  // Warten bis IP-Adresse zugewiesen wurde (DHCP kann kurz verzögert sein)
  unsigned long ipStart = millis();
  while (WiFi.localIP() == IPAddress(0, 0, 0, 0) && millis() - ipStart < 3000)
  {
    esp_task_wdt_reset();
    delay(50);
  }

  if (WiFi.localIP() == IPAddress(0, 0, 0, 0))
  {
    Serial.println("WiFi: Keine IP-Adresse erhalten.");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    return false;
  }

  Serial.print("WiFi verbunden. IP: ");
  Serial.println(WiFi.localIP());
  IPAddress ip = WiFi.localIP();
  CAN_Send(IP_Send_to_CAN, 0x02, ip[0], ip[1], ip[2], ip[3]);
  return true;
}

// Trennt WiFi wenn weder OTA noch WebTerminal es benötigen.
static void WiFi_ReleaseIfUnneeded()
{
  if (!wifi_needed_ota && !wifi_needed_webterm)
  {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi ausgeschaltet.");
    CAN_Send(IP_Send_to_CAN, 0x03);
    ws2812SetBlinkRGB(0, 255, 0, 3000, 50);
  }
}

void OTA_Stop()
{
  if (OTA_On == 1)
  {
    OTA_On = 0;
    OTA_ActivatedAt = 0;
    ArduinoOTA.end();
    wifi_needed_ota = false;
    WiFi_ReleaseIfUnneeded();
  }
}

void OTA_Start()
{
  CAN_Send(IP_Send_to_CAN, 0x01);

  if (OTA_On == 1)
  {
    OTA_On = 0;
    ArduinoOTA.end();
  }

  wifi_needed_ota = true;
  if (!WiFi_Connect())
  {
    wifi_needed_ota = false;
    return;
  }

  ArduinoOTA.begin();
  OTA_ActivatedAt = millis();
  ws2812SetBlinkRGB(0, 255, 0, 1000, 50);

  IPAddress ip = WiFi.localIP();
  Serial.printf("WiFi-IP: %s\n", ip.toString().c_str());
  webTerminalAppendFormat("WiFi-IP: %s", ip.toString().c_str());
  webTerminalAppendLine("OTA aktiviert. Warten auf Upload...");

  // Callbacks nach begin() setzen
  ArduinoOTA.onStart([]()
                     {
    Serial.println("OTA Start: Watchdog auf 60s erhoehen");
    CAN_Send(IP_Send_to_CAN, 0x05);
    otaUploadActive = true;
    esp_task_wdt_init(60, true);
    esp_task_wdt_add(NULL); 
    ws2812SetBlinkRGB(0, 255, 0, 500, 50);
    webTerminalAppendLine("OTA Upload gestartet.");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        {
                          esp_task_wdt_reset();
                          webTerminalAppendFormat("OTA Fortschritt: %u%%", (unsigned int)((progress / (float)total) * 100.0f));
                        });

  ArduinoOTA.onEnd([]()
                   {
    Serial.println("OTA Ende: Watchdog zurueck auf 10s");
    otaUploadActive = false;
    incrementFirmwareVersion();
    CAN_Send(IP_Send_to_CAN, 0x06);
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(NULL);
    webTerminalAppendLine("OTA Upload abgeschlossen. Neustart...");
  });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("OTA Fehler [%u]: ", error);
    otaUploadActive = false;
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    CAN_Send(IP_Send_to_CAN, 0x07);
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(NULL); 
    ws2812SetBlinkRGB(255, 0, 0, 500, 50);
    webTerminalAppendFormat("OTA Fehler: %u", (unsigned int)error);
  });

  OTA_On = 1;
}

void WifiScan()
{
  Serial.println("Scan start");

  CAN_Send(Can_Input_Wifi, 0x01);

  byte n = WiFi.scanNetworks();
  Serial.println("Scan done");

  if (n == 0)
  {
    Serial.println("no networks found");
    CAN_Send(Can_Output_Wifi_Scan, 0x02);
  }
  else
  {
    if (n > 10)
    {
      n = 10;
    }

    Serial.print(n);

    CAN_Send(Can_Output_Wifi_Scan, 0x03, n);

    String SSID_gefunden;

    Serial.println(" networks found");
    Serial.println("SSID");
    for (int i = 0; i < n; ++i)
    {
      SSID_gefunden = WiFi.SSID(i);

      for (int a = SSID_gefunden.length(); a < 32; ++a)
      {
        SSID_gefunden += " ";
      }

      String message = SSID_gefunden;
      byte plain[message.length()];
      message.getBytes(plain, message.length() + 1);

      for (int b = 0; b < 32; ++b)
      {
        SSID_Speicher[i][b] = plain[b];
      }

      delay(100);
      CAN_Send(Can_Output_wifi_SSID_Scan_Daten1, i + 1, plain[0], plain[1], plain[2], plain[3], plain[4], plain[5], plain[6]);
      delay(100);
      CAN_Send(Can_Output_wifi_SSID_Scan_Daten2, i + 1, plain[7], plain[8], plain[9], plain[10], plain[11], plain[12], plain[13]);
      delay(100);
      CAN_Send(Can_Output_wifi_SSID_Scan_Daten3, i + 1, plain[14], plain[15], plain[16], plain[17], plain[18], plain[19], plain[20]);
      delay(100);
      CAN_Send(Can_Output_wifi_SSID_Scan_Daten4, i + 1, plain[21], plain[22], plain[23], plain[24], plain[25], plain[26], plain[27]);
      delay(100);
      CAN_Send(Can_Output_wifi_SSID_Scan_Daten5, i + 1, plain[28], plain[29], plain[30], plain[31]);

      Serial.println(SSID_gefunden);

      delay(10);
    }
  }

  WiFi.scanDelete();
  WiFi_ReleaseIfUnneeded();
}

void WifiSsidAuswahl(byte wert)
{
  for (int b = 0; b < 32; ++b)
  {
    SSID_Byte_arrey[b] = SSID_Speicher[wert][b];
  }

  NeueSSID = String((char *)SSID_Byte_arrey);

  NeueSSID.trim();
  SSID_Schreiben(NeueSSID);
}

void Aktuelle_SSID_Senden()
{
  String SSID_gespeichert = SSID_Lesen();

  for (int a = SSID_gespeichert.length(); a < 32; ++a)
  {
    SSID_gespeichert += " ";
  }

  String message = SSID_gespeichert;
  byte plain[message.length()];
  message.getBytes(plain, message.length() + 1);

  delay(500);
  CAN_Send(Can_Output_wifi_SSID_Daten1, plain[0], plain[1], plain[2], plain[3], plain[4], plain[5], plain[6], plain[7]);
  delay(100);
  CAN_Send(Can_Output_wifi_SSID_Daten2, plain[8], plain[9], plain[10], plain[11], plain[12], plain[13], plain[14], plain[15]);
  delay(100);
  CAN_Send(Can_Output_wifi_SSID_Daten3, plain[16], plain[17], plain[18], plain[19], plain[20], plain[21], plain[22], plain[23]);
  delay(100);
  CAN_Send(Can_Output_wifi_SSID_Daten4, plain[24], plain[25], plain[26], plain[27], plain[28], plain[29], plain[30], plain[31]);
}

void Aktuelle_PASSWORT_Senden()
{
  String SSID_gespeichert = PASSWORD_Lesen();

  for (int a = SSID_gespeichert.length(); a < 32; ++a)
  {
    SSID_gespeichert += " ";
  }

  String message = SSID_gespeichert;
  byte plain[message.length()];
  message.getBytes(plain, message.length() + 1);

  delay(500);
  CAN_Send(Can_Output_wifi_Passwort_Daten1, plain[0], plain[1], plain[2], plain[3], plain[4], plain[5], plain[6], plain[7]);
  delay(100);
  CAN_Send(Can_Output_wifi_Passwort_Daten2, plain[8], plain[9], plain[10], plain[11], plain[12], plain[13], plain[14], plain[15]);
  delay(100);
  CAN_Send(Can_Output_wifi_Passwort_Daten3, plain[16], plain[17], plain[18], plain[19], plain[20], plain[21], plain[22], plain[23]);
  delay(100);
  CAN_Send(Can_Output_wifi_Passwort_Daten4, plain[24], plain[25], plain[26], plain[27], plain[28], plain[29], plain[30], plain[31]);
}

#endif