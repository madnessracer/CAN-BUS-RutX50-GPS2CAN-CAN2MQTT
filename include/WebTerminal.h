#ifndef WEB_TERMINAL_H
#define WEB_TERMINAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <driver/twai.h>
#include <ErrorLog.h>
#include <Ws2812.h>
#include <PCA9555.h>
#include <CANPing.h>
#include <AHT10.h>
#include "GpsState.h"
#include "Wlan_Config.h"
#include "FileSystem.h"
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

bool parseCanMessageLine(const char *line, uint32_t &messageId, uint8_t &dlc, uint8_t data[8]);
bool parseCanMessageLine(const String &line, uint32_t &messageId, uint8_t &dlc, uint8_t data[8]);
void CAN_SendEx(bool frameExtended, uint8_t dlc, uint messageId, ...);

enum CanLiveMode { CAN_LIVE_OFF = 0, CAN_LIVE_SERIAL = 1, CAN_LIVE_TWAI = 2, CAN_LIVE_ALL = 3 };
extern CanLiveMode canLiveMode;
extern bool canLiveModeWeb;
extern bool gpsLiveModeWeb;
extern bool gpsLiveRawModeWeb;

static WebServer webServer(80);
static bool g_webTerminalEnabled = false;
static bool g_webServerStarted = false;
static String g_webTerminalOutput;
static const size_t WEBTERM_MAX_OUTPUT = 16000;

static bool g_webTerminalWifiMode = false;
static int g_webTerminalWifiScanCount = 0;
static char g_webTerminalWifiScanSSIDs[20][33];
static char g_webTerminalPendingSSID[33];
static constexpr unsigned long WEBTERM_INACTIVITY_TIMEOUT_MS = 300000UL;
static unsigned long g_webTerminalLastActivity = 0;

static const char kWebTerminalPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP Web-Terminal</title>
  <style>
    body { background:#111; color:#eee; font-family: monospace; margin:0; padding:0; }
    header { padding:12px; background:#222; }
    h1 { margin:0; font-size:1.3rem; }
    #output { width:100%; height:70vh; background:#000; color:#0f0; padding:12px; overflow:auto; box-sizing:border-box; white-space:pre-wrap; }
    #controls { display:flex; gap:8px; padding:12px; background:#111; }
    #cmd { flex:1; padding:10px; background:#222; border:1px solid #444; color:#eee; }
    button { padding:10px 14px; background:#008; border:none; color:#fff; cursor:pointer; }
    button:disabled { opacity:.6; cursor:not-allowed; }
  </style>
</head>
<body>
  <header>
    <h1>ESP Web-Terminal</h1>
    <div>Nutze <code>help</code> fuer die Befehle.</div>
  </header>
  <div id="output">Lade...</div>
  <div id="controls">
    <input id="cmd" type="text" placeholder="Befehl eingeben..." autocomplete="off">
    <button id="send">Senden</button>
    <button id="helpBtn">Help</button>
    <button id="statusBtn">Status</button>
    <button id="otaOnBtn">OTA on</button>
    <button id="otaOffBtn">OTA off</button>
  </div>
  <script>
    const output = document.getElementById('output');
    const cmd = document.getElementById('cmd');
    const send = document.getElementById('send');
    const helpBtn = document.getElementById('helpBtn');
    const statusBtn = document.getElementById('statusBtn');
    const otaOnBtn = document.getElementById('otaOnBtn');
    const otaOffBtn = document.getElementById('otaOffBtn');

    async function refreshOutput() {
      try {
        const res = await fetch('/output');
        if (!res.ok) throw new Error('Fehler beim Laden');
        const text = await res.text();
        output.textContent = text;
        output.scrollTop = output.scrollHeight;
      } catch (e) {
        output.textContent = 'Verbindung fehlgeschlagen. Stelle sicher, dass WiFi aktiv ist und der ESP erreichbar ist.';
      }
    }

    async function sendCommandText(text) {
      if (!text) return;
      output.textContent = '';
      send.disabled = true;
      try {
        await fetch('/cmd', { method:'POST', body: text });
        cmd.value = '';
        await refreshOutput();
      } catch (e) {
        output.textContent = 'Fehler beim Senden des Befehls.';
      }
      send.disabled = false;
      cmd.focus();
    }

    async function sendCommand() {
      sendCommandText(cmd.value.trim());
    }

    send.addEventListener('click', sendCommand);
    cmd.addEventListener('keydown', e => {
      if (e.key === 'Enter') {
        e.preventDefault();
        sendCommand();
      }
    });

    helpBtn.addEventListener('click', () => sendCommandText('help'));
    statusBtn.addEventListener('click', () => sendCommandText('status'));
    otaOnBtn.addEventListener('click', () => sendCommandText('ota on'));
    otaOffBtn.addEventListener('click', () => sendCommandText('ota off'));

    setInterval(refreshOutput, 1500);
    refreshOutput();
  </script>
</body>
</html>
)rawliteral";

static inline void trimWebTerminalOutput()
{
  if (g_webTerminalOutput.length() > WEBTERM_MAX_OUTPUT)
  {
    g_webTerminalOutput = g_webTerminalOutput.substring(g_webTerminalOutput.length() - WEBTERM_MAX_OUTPUT);
  }
}

static constexpr const char WEBTERM_STATE_PATH[] = "/webterm_enabled.txt";

inline void saveWebTerminalEnabledState(bool enabled)
{
  FS_Open();
  if (enabled)
  {
    writeFile(LittleFS, WEBTERM_STATE_PATH, "1");
  }
  else
  {
    deleteFile(LittleFS, WEBTERM_STATE_PATH);
  }
}

inline bool webTerminalLoadSavedState()
{
  FS_Open();
  readFile(LittleFS, WEBTERM_STATE_PATH);
  return FileBuffer[0] == '1';
}

inline void webTerminalAppendLine(const String &text)
{
  g_webTerminalOutput += text;
  g_webTerminalOutput += "\n";
  trimWebTerminalOutput();
}

inline void webTerminalAppendFormat(const char *format, ...)
{
  char buf[192];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  g_webTerminalOutput += buf;
  g_webTerminalOutput += "\n";
  trimWebTerminalOutput();
}

static inline void webTerminalTrimWhitespace(char *text)
{
  if (text == nullptr)
    return;
  size_t start = 0;
  while (text[start] && isspace((unsigned char)text[start]))
    start++;
  size_t end = strlen(text);
  while (end > start && isspace((unsigned char)text[end - 1]))
    end--;
  if (start > 0)
    memmove(text, text + start, end - start);
  text[end - start] = '\0';
}

static inline void toLowerCase(char *text)
{
  if (text == nullptr)
    return;
  for (size_t i = 0; text[i]; ++i)
    text[i] = tolower((unsigned char)text[i]);
}

static void printWebTerminalHelp()
{
  webTerminalAppendLine("Web-Terminal Befehle:");
  webTerminalAppendLine("help");
  webTerminalAppendLine("status");
  webTerminalAppendLine("temp");
  webTerminalAppendLine("ota");
  webTerminalAppendLine("ota on");
  webTerminalAppendLine("ota off");
  webTerminalAppendLine("ota autooff <Minuten>");
  webTerminalAppendLine("ota autooff off");
  webTerminalAppendLine("aht10 status");
  webTerminalAppendLine("aht10 on");
  webTerminalAppendLine("aht10 off");
  webTerminalAppendLine("aht10 now");
  webTerminalAppendLine("wifi");
  webTerminalAppendLine("pca status");
  webTerminalAppendLine("pca in");
  webTerminalAppendLine("pca out");
  webTerminalAppendLine("pca out <1-8> on|off");
  webTerminalAppendLine("pca init");
  webTerminalAppendLine("pca live");
  webTerminalAppendLine("can status");
  webTerminalAppendLine("can live <serial|twai|all>");
  webTerminalAppendLine("can stop");
  webTerminalAppendLine("gps live [roh]");
  webTerminalAppendLine("gps stop");
  webTerminalAppendLine("can test <count>");
  webTerminalAppendLine("sendcan 0xID;DLC;DATA");
  webTerminalAppendLine("canping on|off|status|fast");
  webTerminalAppendLine("error log");
  webTerminalAppendLine("error info");
  webTerminalAppendLine("error clear");
  webTerminalAppendLine("webterm on");
  webTerminalAppendLine("webterm off");
  webTerminalAppendLine("webterm status");
  webTerminalAppendLine("reboot");
}

static void printWebTerminalWifiHelp()
{
  webTerminalAppendLine("WLAN Befehle:");
  webTerminalAppendLine("wifi help");
  webTerminalAppendLine("wifi ssid");
  webTerminalAppendLine("wifi pwd");
  webTerminalAppendLine("wifi scan");
  webTerminalAppendLine("wifi select <Nummer>");
  webTerminalAppendLine("wifi password <Passwort>");
  webTerminalAppendLine("wifi back");
  if (g_webTerminalPendingSSID[0] != '\0')
  {
    webTerminalAppendFormat("Aktuelle Auswahl: %s", g_webTerminalPendingSSID);
  }
}

static void scanWifiNetworksWeb()
{
  if (WiFi.getMode() != WIFI_STA)
  {
    WiFi.mode(WIFI_STA);
    delay(300);
  }
  WiFi.disconnect(false);
  delay(200);

  webTerminalAppendLine("Scanne WLAN-Netzwerke... Bitte warten.");
  esp_task_wdt_reset();
  int scanResult = WiFi.scanNetworks(false, false);
  if (scanResult <= 0)
  {
    if (scanResult == WIFI_SCAN_FAILED)
      webTerminalAppendLine("Scan fehlgeschlagen.");
    else
      webTerminalAppendLine("Keine Netzwerke gefunden.");
    g_webTerminalWifiScanCount = 0;
    return;
  }

  g_webTerminalWifiScanCount = min(scanResult, 20);
  webTerminalAppendFormat("%d Netzwerke gefunden:", scanResult);
  for (int i = 0; i < g_webTerminalWifiScanCount; ++i)
  {
    const char *ssid = WiFi.SSID(i).c_str();
    strncpy(g_webTerminalWifiScanSSIDs[i], ssid, sizeof(g_webTerminalWifiScanSSIDs[i]) - 1);
    g_webTerminalWifiScanSSIDs[i][sizeof(g_webTerminalWifiScanSSIDs[i]) - 1] = '\0';
    webTerminalAppendFormat("%d. %s  %d dBm  Kanal %d", i + 1, g_webTerminalWifiScanSSIDs[i], WiFi.RSSI(i), WiFi.channel(i));
  }
  if (scanResult > 20)
  {
    webTerminalAppendLine("Mehr als 20 Netzwerke gefunden; nur die ersten 20 werden angezeigt.");
  }
  webTerminalAppendLine("Wahle ein Netz mit: wifi select <Nummer>");
}

static void selectWebTerminalWifiSSID(int index)
{
  if (index < 1 || index > g_webTerminalWifiScanCount)
  {
    webTerminalAppendLine("Ungueltige Auswahl. Verwende: wifi select <Nummer>");
    return;
  }
  const char *sourceSSID = g_webTerminalWifiScanSSIDs[index - 1];
  strncpy(g_webTerminalPendingSSID, sourceSSID, sizeof(g_webTerminalPendingSSID) - 1);
  g_webTerminalPendingSSID[sizeof(g_webTerminalPendingSSID) - 1] = '\0';
  webTerminalAppendFormat("Ausgewaehlte SSID: %s", g_webTerminalPendingSSID);
  webTerminalAppendLine("Gib das Passwort ein mit: wifi password <Passwort>");
}

static void printPcaStatusWeb()
{
  if (!PCA9555_IsInitialized())
  {
    webTerminalAppendLine("PCA9555 nicht initialisiert.");
    return;
  }
  uint8_t outputPort0 = 0;
  uint8_t outputPort1 = 0;
  uint8_t inputPort0 = 0;
  uint8_t inputPort1 = 0;
  if (!PCA9555_ReadOutputs(outputPort0, outputPort1) || !PCA9555_ReadInputs(inputPort0, inputPort1))
  {
    webTerminalAppendLine("PCA9555 nicht erreichbar.");
    return;
  }
  webTerminalAppendLine("PCA9555 Status:");
  webTerminalAppendLine("Ausgänge:");
  for (uint8_t i = 0; i < 8; ++i)
  {
    webTerminalAppendFormat("  FET%u: %s", i + 1,
                            (outputPort0 & (1 << i)) ? "ON" : "OFF");
  }
  webTerminalAppendLine("Eingänge:");
  for (uint8_t i = 0; i < 8; ++i)
  {
    webTerminalAppendFormat("  OPTO%u: %s", i + 1,
                            (inputPort1 & (1 << i)) ? "HIGH" : "LOW");
  }
}

static void printPcaInputsWeb()
{
  if (!PCA9555_IsInitialized())
  {
    webTerminalAppendLine("PCA9555 nicht initialisiert.");
    return;
  }
  uint8_t inputPort0 = 0;
  uint8_t inputPort1 = 0;
  if (!PCA9555_ReadInputs(inputPort0, inputPort1))
  {
    webTerminalAppendLine("PCA9555 nicht erreichbar.");
    return;
  }
  webTerminalAppendLine("PCA9555 Eingänge:");
  for (uint8_t i = 0; i < 8; ++i)
  {
    webTerminalAppendFormat("  OPTO%u: %s", i + 1,
                            (inputPort1 & (1 << i)) ? "HIGH" : "LOW");
  }
}

static void printPcaOutputsWeb()
{
  if (!PCA9555_IsInitialized())
  {
    webTerminalAppendLine("PCA9555 nicht initialisiert.");
    return;
  }
  uint8_t outputPort0 = 0;
  uint8_t outputPort1 = 0;
  if (!PCA9555_ReadOutputs(outputPort0, outputPort1))
  {
    webTerminalAppendLine("PCA9555 nicht erreichbar.");
    return;
  }
  webTerminalAppendLine("PCA9555 Ausgänge:");
  for (uint8_t i = 0; i < 8; ++i)
  {
    webTerminalAppendFormat("  FET%u: %s", i + 1,
                            (outputPort0 & (1 << i)) ? "ON" : "OFF");
  }
}

static void printAHT10StatusWeb()
{
  webTerminalAppendFormat("AHT10 aktiv: %s", aht10Enabled ? "ja" : "nein");
  webTerminalAppendFormat("AHT10 Zustand: %s", aht10State == AHT10_MEASURING ? "Messung laeuft" : "bereit");
  if (!isnan(aht10LastTemperature))
  {
    webTerminalAppendFormat("Letzte Temperatur: %.1f °C", aht10LastTemperature);
  }
  if (!isnan(aht10LastHumidity))
  {
    webTerminalAppendFormat("Letzte Luftfeuchte: %.1f %%", aht10LastHumidity);
  }
}

static void printOtaStatusWeb()
{
  webTerminalAppendFormat("OTA aktiviert: %u", OTA_On);
  if (OTA_On)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      webTerminalAppendFormat("WiFi-IP: %s", WiFi.localIP().toString().c_str());
    }
    if (OTA_UploadActive())
    {
      webTerminalAppendLine("OTA Upload: aktiv");
    }
    else
    {
      webTerminalAppendLine("OTA Upload: bereit");
    }
  }
  else
  {
    webTerminalAppendLine("OTA ist inaktiv.");
  }
  if (OTA_AutoOffIntervalMs > 0)
  {
    webTerminalAppendFormat("OTA Auto-Off: %lu Minuten", OTA_AutoOffIntervalMs / 60000UL);
  }
  else
  {
    webTerminalAppendLine("OTA Auto-Off: deaktiviert");
  }
}

static void printErrorLogWeb()
{
  char lines[MAX_ERROR_LOG_ENTRIES][ERROR_LOG_LINE_SIZE];
  size_t count = errorLogReadAllLines(lines, MAX_ERROR_LOG_ENTRIES);
  webTerminalAppendLine("=== Fehler-Log ===");
  if (count == 0)
  {
    webTerminalAppendLine("Keine Fehler gefunden.");
  }
  else
  {
    for (size_t i = 0; i < count; ++i)
    {
      webTerminalAppendFormat("%u: %s", (unsigned int)(i + 1), lines[i]);
    }
  }
  webTerminalAppendLine("==================");
}

static void printErrorLogInfoWeb()
{
  char lines[MAX_ERROR_LOG_ENTRIES][ERROR_LOG_LINE_SIZE];
  size_t count = errorLogReadAllLines(lines, MAX_ERROR_LOG_ENTRIES);
  if (!FS_Open())
  {
    webTerminalAppendLine("Fehler: LittleFS nicht erreichbar.");
    return;
  }
  size_t fileSize = 0;
  if (LittleFS.exists(ERROR_LOG_PATH))
  {
    File file = LittleFS.open(ERROR_LOG_PATH, FILE_READ);
    if (file)
    {
      fileSize = file.size();
      file.close();
    }
  }
  FS_Close();

  webTerminalAppendLine("=== Fehler-Log Info ===");
  webTerminalAppendFormat("Eintraege: %u", (unsigned int)count);
  webTerminalAppendFormat("Dateigroesse: %u bytes", (unsigned int)fileSize);
  webTerminalAppendLine("Aktive Fehlerzustaende:");
  bool foundActive = false;
  for (size_t i = 0; i < ERROR_LOG_KEY_COUNT; ++i)
  {
    if (errorLogIsActive(ERROR_LOG_KEYS[i]))
    {
      webTerminalAppendFormat(" - %s", ERROR_LOG_KEYS[i]);
      foundActive = true;
    }
  }
  if (!foundActive)
  {
    webTerminalAppendLine(" - keine Aktivitaeten");
  }
  webTerminalAppendLine("======================");
}

static void printDebugStatusWeb()
{
  webTerminalAppendFormat("OTA aktiviert: %u", OTA_On);
  if (OTA_AutoOffIntervalMs > 0)
  {
    webTerminalAppendFormat("OTA Auto-Off: %lu Minuten", OTA_AutoOffIntervalMs / 60000UL);
  }
  else
  {
    webTerminalAppendLine("OTA Auto-Off: deaktiviert");
  }
  webTerminalAppendFormat("WiFi-Status: %d", WiFi.status());
  webTerminalAppendFormat("Web-Terminal: %s", g_webTerminalEnabled ? "aktiv" : "deaktiviert");
  if (!isnan(aht10LastTemperature))
  {
    webTerminalAppendFormat("AHT10 Temperatur: %.1f °C", aht10LastTemperature);
  }
  if (!isnan(aht10LastHumidity))
  {
    webTerminalAppendFormat("AHT10 Luftfeuchte: %.1f %%", aht10LastHumidity);
  }
}

static void handleWebTerminalCommand(const char *command)
{
  char cmdBuf[128];
  strncpy(cmdBuf, command, sizeof(cmdBuf) - 1);
  cmdBuf[sizeof(cmdBuf) - 1] = '\0';
  webTerminalTrimWhitespace(cmdBuf);

  if (cmdBuf[0] == '\0')
  {
    webTerminalAppendLine("Leeres Kommando.");
    return;
  }

  char cmdLower[128];
  strncpy(cmdLower, cmdBuf, sizeof(cmdLower) - 1);
  cmdLower[sizeof(cmdLower) - 1] = '\0';
  toLowerCase(cmdLower);

  if (strcmp(cmdLower, "help") == 0)
  {
    printWebTerminalHelp();
    return;
  }

  if (strcmp(cmdLower, "webterm on") == 0)
  {
    if (!g_webTerminalEnabled)
    {
      g_webTerminalEnabled = true;
      webTerminalAppendLine("Web-Terminal aktiviert.");
    }
    else
    {
      webTerminalAppendLine("Web-Terminal ist bereits aktiv.");
    }
    return;
  }

  if (strcmp(cmdLower, "webterm off") == 0)
  {
    g_webTerminalEnabled = false;
    webTerminalAppendLine("Web-Terminal deaktiviert.");
    return;
  }

  if (strcmp(cmdLower, "webterm status") == 0)
  {
    webTerminalAppendFormat("Web-Terminal: %s", g_webTerminalEnabled ? "aktiv" : "deaktiviert");
    return;
  }

  if (g_webTerminalWifiMode)
  {
    if (strcmp(cmdLower, "wifi help") == 0)
    {
      printWebTerminalWifiHelp();
      return;
    }
    if (strcmp(cmdLower, "wifi ssid") == 0)
    {
      webTerminalAppendFormat("Gespeicherte SSID: %s", SSID_Lesen().c_str());
      return;
    }
    if (strcmp(cmdLower, "wifi pwd") == 0)
    {
      webTerminalAppendFormat("Gespeichertes Passwort: %s", PASSWORD_Lesen().c_str());
      return;
    }
    if (strcmp(cmdLower, "wifi scan") == 0)
    {
      scanWifiNetworksWeb();
      return;
    }
    if (strncmp(cmdLower, "wifi select ", 12) == 0)
    {
      int index = atoi(cmdLower + 12);
      if (index < 1 || index > g_webTerminalWifiScanCount)
      {
        webTerminalAppendLine("Ungueltige Auswahl. Verwende: wifi select <Nummer>");
        return;
      }
      selectWebTerminalWifiSSID(index);
      return;
    }
    if (strncmp(cmdLower, "wifi password ", 14) == 0)
    {
      if (g_webTerminalPendingSSID[0] == '\0')
      {
        webTerminalAppendLine("Keine SSID ausgewaehlt. Verwende wifi select <Nummer>.");
        return;
      }
      const char *pwd = cmdBuf + 14;
      char pwdBuf[64];
      strncpy(pwdBuf, pwd, sizeof(pwdBuf) - 1);
      pwdBuf[sizeof(pwdBuf) - 1] = '\0';
      webTerminalTrimWhitespace(pwdBuf);
      if (pwdBuf[0] == '\0')
      {
        webTerminalAppendLine("Kein Passwort angegeben.");
        return;
      }
      SSID_Schreiben(g_webTerminalPendingSSID);
      PASSWORD_Schreiben(String(pwdBuf));
      webTerminalAppendLine("WLAN-Daten gespeichert.");
      g_webTerminalPendingSSID[0] = '\0';
      return;
    }
    if (strcmp(cmdLower, "wifi back") == 0)
    {
      g_webTerminalWifiMode = false;
      webTerminalAppendLine("WLAN-Menue verlassen.");
      return;
    }
    if (strcmp(cmdLower, "wifi") == 0)
    {
      printWebTerminalWifiHelp();
      return;
    }
  }

  if (strcmp(cmdLower, "wifi") == 0)
  {
    g_webTerminalWifiMode = true;
    printWebTerminalWifiHelp();
    return;
  }

  if (strcmp(cmdLower, "status") == 0)
  {
    IPAddress ip = WiFi.localIP();
    char ipBuf[16];
    snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    webTerminalAppendFormat("WiFi: %s", ipBuf);
    webTerminalAppendFormat("Web-Terminal: %s", g_webTerminalEnabled ? "aktiv" : "deaktiviert");
    webTerminalAppendFormat("OTA aktiviert: %u", OTA_On);
    if (OTA_On)
    {
      webTerminalAppendLine(OTA_UploadActive() ? "OTA Upload: aktiv" : "OTA Upload: bereit");
    }
    return;
  }

  if (strcmp(cmdLower, "temp") == 0)
  {
    webTerminalAppendFormat("Temperatur: %.1f °C", temperatureRead());
    return;
  }

  if (strcmp(cmdLower, "aht10 status") == 0)
  {
    printAHT10StatusWeb();
    return;
  }
  if (strcmp(cmdLower, "aht10 on") == 0)
  {
    aht10Enabled = true;
    webTerminalAppendLine("AHT10 periodische Messung eingeschaltet.");
    return;
  }
  if (strcmp(cmdLower, "aht10 off") == 0)
  {
    aht10Enabled = false;
    webTerminalAppendLine("AHT10 periodische Messung ausgeschaltet.");
    return;
  }
  if (strcmp(cmdLower, "aht10 now") == 0)
  {
    if (aht10State == AHT10_IDLE)
    {
      if (startAHT10Measurement())
      {
        webTerminalAppendLine("AHT10 Messung gestartet, Ergebnis in ca. 80 ms.");
      }
      else
      {
        webTerminalAppendLine("AHT10 Messung konnte nicht gestartet werden.");
      }
    }
    else
    {
      webTerminalAppendLine("AHT10 Messung bereits aktiv, bitte warten.");
    }
    return;
  }

  if (strcmp(cmdLower, "ota") == 0)
  {
    printOtaStatusWeb();
    return;
  }
  if (strcmp(cmdLower, "ota on") == 0)
  {
    webTerminalAppendLine("OTA einschalten...");
    OTA_Start();
    if (WiFi.status() == WL_CONNECTED)
    {
      webTerminalAppendFormat("WiFi-IP: %s", WiFi.localIP().toString().c_str());
    }
    else
    {
      webTerminalAppendLine("WiFi-Verbindung fehlgeschlagen oder nicht verbunden.");
    }
    return;
  }
  if (strcmp(cmdLower, "ota off") == 0)
  {
    webTerminalAppendLine("OTA ausschalten...");
    OTA_Stop();
    return;
  }
  if (strcmp(cmdLower, "ota autooff off") == 0)
  {
    OTA_AutoOffIntervalMs = 0;
    webTerminalAppendLine("OTA Auto-Off deaktiviert.");
    return;
  }
  if (strncmp(cmdLower, "ota autooff ", 12) == 0)
  {
    const char *value = cmdBuf + 12;
    unsigned long minutes = strtoul(value, nullptr, 10);
    if (minutes > 0)
    {
      OTA_AutoOffIntervalMs = minutes * 60000UL;
      webTerminalAppendFormat("OTA Auto-Off auf %lu Minuten gesetzt.", minutes);
    }
    else
    {
      webTerminalAppendLine("Ungueltige Zeit. Gib eine Zahl in Minuten ein.");
    }
    return;
  }

  if (strcmp(cmdLower, "pca status") == 0)
  {
    printPcaStatusWeb();
    return;
  }
  if (strcmp(cmdLower, "pca in") == 0)
  {
    printPcaInputsWeb();
    return;
  }
  if (strcmp(cmdLower, "pca out") == 0)
  {
    printPcaOutputsWeb();
    return;
  }
  if (strcmp(cmdLower, "pca init") == 0)
  {
    if (PCA9555_Init())
    {
      webTerminalAppendLine("PCA9555 initialisiert.");
    }
    else
    {
      webTerminalAppendLine("PCA9555 nicht erreichbar.");
    }
    return;
  }
  if (strncmp(cmdLower, "pca out ", 8) == 0)
  {
    const char *params = cmdBuf + 8;
    char paramsBuf[64];
    strncpy(paramsBuf, params, sizeof(paramsBuf) - 1);
    paramsBuf[sizeof(paramsBuf) - 1] = '\0';
    webTerminalTrimWhitespace(paramsBuf);
    char *firstSpacePtr = strchr(paramsBuf, ' ');
    if (firstSpacePtr == nullptr)
    {
      webTerminalAppendLine("Verwendung: pca out <1-8> on|off");
      return;
    }
    *firstSpacePtr = '\0';
    int outputIndex = atoi(paramsBuf);
    char *state = firstSpacePtr + 1;
    for (char *p = state; *p; ++p)
      *p = tolower((unsigned char)*p);
    bool onState = strcmp(state, "on") == 0;
    bool offState = strcmp(state, "off") == 0;
    if (outputIndex < 1 || outputIndex > 8 || (!onState && !offState))
    {
      webTerminalAppendLine("Verwendung: pca out <1-8> on|off");
      return;
    }
    if (!PCA9555_SetOutput(outputIndex - 1, onState))
    {
      webTerminalAppendLine("PCA9555 nicht erreichbar oder nicht initialisiert.");
      return;
    }
    webTerminalAppendFormat("FET%u %s", (unsigned int)outputIndex, onState ? "eingeschaltet" : "ausgeschaltet");
    return;
  }
  if (strcmp(cmdLower, "pca live") == 0)
  {
    webTerminalAppendLine("PCA Live Mode: Gebe pca status, pca in oder pca out ein, um Status zu sehen.");
    return;
  }

  if (strcmp(cmdLower, "can status") == 0)
  {
    twai_status_info_t status;
    if (twai_get_status_info(&status) != ESP_OK)
    {
      webTerminalAppendLine("Fehler: TWAI Status konnte nicht gelesen werden.");
      return;
    }
    webTerminalAppendFormat("Zustand: %d", status.state);
    webTerminalAppendFormat("TX Fehler: %u", status.tx_error_counter);
    webTerminalAppendFormat("RX Fehler: %u", status.rx_error_counter);
    webTerminalAppendFormat("Bus Fehler: %u", status.bus_error_count);
    webTerminalAppendFormat("Arbitration: %u", status.arb_lost_count);
    return;
  }
  if (strcmp(cmdLower, "can live serial") == 0)
  {
    canLiveMode = CAN_LIVE_SERIAL;
    canLiveModeWeb = true;
    webTerminalAppendLine("CAN Live Mode eingeschaltet (seriell).");
    return;
  }
  if (strcmp(cmdLower, "can live all") == 0)
  {
    canLiveMode = CAN_LIVE_ALL;
    canLiveModeWeb = true;
    webTerminalAppendLine("CAN Live Mode eingeschaltet (TWAI + seriell).");
    return;
  }
  if (strcmp(cmdLower, "can all") == 0)
  {
    canLiveMode = CAN_LIVE_ALL;
    canLiveModeWeb = true;
    webTerminalAppendLine("CAN All Mode eingeschaltet (TWAI + seriell).");
    return;
  }
  if (strcmp(cmdLower, "can live twai") == 0)
  {
    canLiveMode = CAN_LIVE_TWAI;
    canLiveModeWeb = true;
    webTerminalAppendLine("CAN Live Mode eingeschaltet (TWAI).");
    return;
  }
  if (strcmp(cmdLower, "can live off") == 0 || strcmp(cmdLower, "can stop") == 0)
  {
    canLiveMode = CAN_LIVE_OFF;
    canLiveModeWeb = false;
    webTerminalAppendLine("CAN Live Mode deaktiviert.");
    return;
  }
  if (strcmp(cmdLower, "gps live roh") == 0)
  {
    g_webTerminalOutput = "";
    gpsLiveModeWeb = true;
    gpsLiveRawModeWeb = true;
    webTerminalAppendLine("GPS Live Mode eingeschaltet (Rohdaten). Verwende 'gps stop' zum Beenden.");
    return;
  }
  if (strcmp(cmdLower, "gps live") == 0)
  {
    g_webTerminalOutput = "";
    gpsLiveModeWeb = true;
    gpsLiveRawModeWeb = false;
    webTerminalAppendLine("GPS Live Mode eingeschaltet. Verwende 'gps stop' zum Beenden.");
    return;
  }
  if (strcmp(cmdLower, "gps stop") == 0)
  {
    gpsLiveModeWeb = false;
    gpsLiveRawModeWeb = false;
    webTerminalAppendLine("GPS Live Mode deaktiviert.");
    return;
  }
  if (strncmp(cmdLower, "can test", 8) == 0)
  {
    const char *params = cmdBuf + 8;
    char paramsBuf[32];
    strncpy(paramsBuf, params, sizeof(paramsBuf) - 1);
    paramsBuf[sizeof(paramsBuf) - 1] = '\0';
    webTerminalTrimWhitespace(paramsBuf);
    unsigned long count = 100;
    if (paramsBuf[0] != '\0')
    {
      count = strtoul(paramsBuf, nullptr, 10);
    }
    if (count == 0)
    {
      webTerminalAppendLine("Ungueltige Anzahl. Verwende: can test oder can test <Anzahl>");
      return;
    }
    twai_status_info_t beforeStatus;
    twai_status_info_t afterStatus;
    twai_get_status_info(&beforeStatus);

    unsigned long receivedCount = 0;
    unsigned long matchedCount = 0;
    const uint32_t testId = 0x1FFFFFFF;
    const uint8_t testData[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    webTerminalAppendFormat("Sende %lu CAN-Testpakete...", count);
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
    webTerminalAppendFormat("Anzahl gesendeter Pakete: %lu", count);
    webTerminalAppendFormat("Empfangene Pakete: %lu", receivedCount);
    webTerminalAppendFormat("Identische Testpakete: %lu", matchedCount);
    webTerminalAppendFormat("Dauer: %lu ms", elapsed);
    webTerminalAppendFormat("Status vorher: %d", beforeStatus.state);
    webTerminalAppendFormat("Status nachher: %d", afterStatus.state);
    return;
  }

  if (strncmp(cmdLower, "canping", 7) == 0)
  {
    char paramsBuf[64];
    strncpy(paramsBuf, cmdBuf + 7, sizeof(paramsBuf) - 1);
    paramsBuf[sizeof(paramsBuf) - 1] = '\0';
    webTerminalTrimWhitespace(paramsBuf);
    toLowerCase(paramsBuf);
    if (strcmp(paramsBuf, "on") == 0)
    {
      CANPing::setEnabled(true);
      webTerminalAppendLine("CANPing aktiviert.");
      return;
    }
    if (strcmp(paramsBuf, "off") == 0)
    {
      CANPing::setEnabled(false);
      webTerminalAppendLine("CANPing deaktiviert.");
      return;
    }
    if (strcmp(paramsBuf, "status") == 0)
    {
      webTerminalAppendFormat("CANPing: %s", CANPing::isEnabled() ? "aktiv" : "deaktiviert");
      webTerminalAppendFormat("Modus: %s", CANPing::isFastMode() ? "FAST" : "normal");
      webTerminalAppendFormat("Pings beantwortet: %lu", (unsigned long)CANPing::getPingCount());
      webTerminalAppendFormat("Request-ID: 0x%08X", (unsigned int)(CANPing::CANPING_CMD_BASE + CANPing::node_id));
      webTerminalAppendFormat("Response-ID: 0x%08X", (unsigned int)(CANPing::CANPING_CMD_BASE + CANPing::CANPING_RESP_OFFSET + CANPing::node_id));
      return;
    }
    if (strcmp(paramsBuf, "fast") == 0)
    {
      CANPing::setFastMode(true);
      webTerminalAppendLine("CANPing FAST aktiv: Verwende can stop oder anderen Befehl, um den Modus zu verlassen.");
      return;
    }
    webTerminalAppendLine("Verwendung: canping on|off|status|fast");
    return;
  }

  if (strcmp(cmdLower, "error log") == 0 || strcmp(cmdLower, "error l") == 0)
  {
    printErrorLogWeb();
    return;
  }
  if (strcmp(cmdLower, "error info") == 0 || strcmp(cmdLower, "error i") == 0)
  {
    printErrorLogInfoWeb();
    return;
  }
  if (strcmp(cmdLower, "error clear") == 0 || strcmp(cmdLower, "error c") == 0)
  {
    errorLogClear();
    webTerminalAppendLine("Fehler-Log geloescht.");
    return;
  }

  if (strcmp(cmdLower, "autoexit off") == 0)
  {
    webTerminalAppendLine("Auto-Exit wird im Web-Terminal nicht unterstuetzt.");
    return;
  }
  if (strncmp(cmdLower, "autoexit ", 9) == 0)
  {
    webTerminalAppendLine("Auto-Exit wird im Web-Terminal nicht unterstuetzt.");
    return;
  }

  if (strncmp(cmdLower, "sendcan ", 8) == 0)
  {
    const char *payload = cmdBuf + 8;
    char payloadBuf[64];
    strncpy(payloadBuf, payload, sizeof(payloadBuf) - 1);
    payloadBuf[sizeof(payloadBuf) - 1] = '\0';
    webTerminalTrimWhitespace(payloadBuf);
    uint32_t messageId;
    uint8_t dlc;
    uint8_t data[8] = {0};
    if (!parseCanMessageLine(payloadBuf, messageId, dlc, data))
    {
      webTerminalAppendLine("Ungueltiges Format. Verwende: sendcan 0xID;DLC;DATA");
      return;
    }
    CAN_SendEx(true, dlc, messageId,
               data[0], data[1], data[2], data[3],
               data[4], data[5], data[6], data[7]);
    webTerminalAppendFormat("CAN gesendet: %s", payloadBuf);
    return;
  }

  if (strcmp(cmdLower, "reboot") == 0)
  {
    webTerminalAppendLine("Starte neu...");
    delay(50);
    ESP.restart();
    return;
  }

  webTerminalAppendFormat("Unbekannter Befehl: %s", command);
  webTerminalAppendLine("Tippe help fuer eine Liste der Befehle.");
}

static inline void webTerminalBegin()
{
  webServer.on("/", HTTP_GET, []() {
    IPAddress remoteIp = webServer.client().remoteIP();
    char ipBuf[16];
    snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", remoteIp[0], remoteIp[1], remoteIp[2], remoteIp[3]);
    Serial.print("[WebTerm] HTTP GET / von ");
    Serial.println(ipBuf);
    webServer.send_P(200, "text/html", kWebTerminalPage);
  });

  webServer.on("/output", HTTP_GET, []() {
    webServer.send(200, "text/plain", g_webTerminalOutput);
  });

  webServer.on("/cmd", HTTP_POST, []() {
    if (!g_webTerminalEnabled)
    {
      webServer.send(503, "text/plain", "Web-Terminal ist deaktiviert.");
      return;
    }
    String cmd = webServer.arg("plain");
    if (cmd.length() == 0)
    {
      webServer.send(400, "text/plain", "Kein Kommando empfangen.");
      return;
    }
    g_webTerminalLastActivity = millis();
    g_webTerminalOutput = "";
    handleWebTerminalCommand(cmd.c_str());
    webServer.send(200, "text/plain", "OK");
  });

  webServer.onNotFound([]() {
    webServer.send(404, "text/plain", "Nicht gefunden");
  });

  // webServer.begin() wird erst in webTerminalLoop() aufgerufen,
  // sobald WiFi verbunden ist (lwIP muss vorher initialisiert sein).
  webTerminalAppendLine("Web-Terminal Server bereit (wartet auf WiFi).");
}

static void webTerminalSetEnabled(bool enabled)
{
  if (g_webTerminalEnabled == enabled)
  {
    return;
  }
  g_webTerminalEnabled = enabled;

  if (enabled)
  {
    wifi_needed_webterm = true;
    if (WiFi_Connect())
    {
      g_webTerminalLastActivity = millis();
      webServer.begin();
      g_webServerStarted = true;
      saveWebTerminalEnabledState(true);
      IPAddress ip = WiFi.localIP();
      char ipBuf[16];
      snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
      Serial.println("Web-Terminal gestartet.");
      Serial.print("  Erreichbar unter: http://");
      Serial.println(ipBuf);
      webTerminalAppendFormat("URL: http://%s", ipBuf);
    }
    else
    {
      wifi_needed_webterm = false;
      g_webTerminalEnabled = false;
      deleteFile(LittleFS, WEBTERM_STATE_PATH);
      Serial.println("Web-Terminal: WiFi Verbindung fehlgeschlagen.");
    }
  }
  else
  {
    Serial.println("Web-Terminal: deaktiviert.");
    g_webServerStarted = false;
    wifi_needed_webterm = false;
    saveWebTerminalEnabledState(false);
    WiFi_ReleaseIfUnneeded();
  }
}

static inline void webTerminalLoop()
{
  if (!g_webTerminalEnabled) return;

  if (WEBTERM_INACTIVITY_TIMEOUT_MS > 0 && millis() - g_webTerminalLastActivity >= WEBTERM_INACTIVITY_TIMEOUT_MS)
  {
    webTerminalAppendLine("Web-Terminal Auto-Off: 5 Minuten Inaktivitaet erreicht. Deaktivierung.");
    g_webTerminalEnabled = false;
    g_webServerStarted = false;
    wifi_needed_webterm = false;
    saveWebTerminalEnabledState(false);
    WiFi_ReleaseIfUnneeded();
    return;
  }

  if (g_webServerStarted)
  {
    webServer.handleClient();
  }
}

static inline bool webTerminalIsEnabled()
{
  return g_webTerminalEnabled;
}

#endif // WEB_TERMINAL_H
