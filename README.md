# CAN BUS RutX50 GPS2CAN-CAN2MQTT

## Übersicht

Dieses Projekt ist eine ESP32-S3 Firmware für einen CAN-Bus Gateway / Debug-Controller mit folgenden Funktionen:

- CAN/TWAI-Schnittstelle für Kommunikation mit Fahrzeug/CAN-Netz
- AHT10 Luftfeuchte-/Temperatursensor über I2C
- PCA9555 GPIO-Erweiterung für FET-Ausgänge und Optokoppler-Eingänge
- OTA-Funktionalität über ArduinoOTA
- Serielles Debug-Menü mit Live-Monitoring, Steuerbefehlen und Fehlerprotokoll
- Persistentes Fehler-Log auf LittleFS
- Nicht-blockierende Sensorerfassung und Timersteuerung mit `millis()`

## Hardware

### Verwendete Komponenten

- ESP32-S3-DevKitC-1
- AHT10 I2C-Temperatur-/Feuchtesensor
- PCA9555 I2C-GPIO-Expander (Adresse `0x20`)
- CAN-Bus Transceiver
- WS2812 RGB-LED
- GPS-Modul (UART)

### Pinbelegung (`include/config.h`)

- `CAN_TX_PIN` = 36
- `CAN_RX_PIN` = 37
- `CAN_SE_PIN` = 38
- `LED_DATA_PIN` = 46
- `CAN_TX_SER` = 41
- `CAN_RX_SER` = 42
- `GPS_RX_SER` = 35
- `GPS_TX_SER` = 6
- `SDA` = 47
- `SCL` = 48
- `PCA9555_INT_PIN` = 21

### PCA9555 Belegung

- `0x20` I2C Adresse
- Port 0 (P0.0..P0.7) = FET1..FET8 Ausgänge
- Port 1 (P1.0..P1.7) = OPTO1..OPTO8 Eingänge
- INT-Pin am ESP32-S3 = `IO21` (aktiv low)

## Firmware-Funktionen

### CAN/TWAI

- Initialisiert TWAI/CAN mit konfigurierten Pins
- Kann CAN-Nachrichten ausgeben und empfangen
- Unterstützt CAN Live Mode via Debug-Menü

### AHT10 Sensor

- Nicht-blockierende Messung mit 80 ms Wartezeit nach Anforderung
- Sende alle aktuellen Messwerte auf CAN
- AHT10-Status im Debug-Menü abfragbar
- Fehler werden ins persistenten Error-Log geschrieben

### PCA9555 Live-Monitoring

- `pca live` zeigt nur die aktuellen OPTO-Eingänge
- Terminal wird bei Änderungen gelöscht, sodass nur die aktuelle Zeile sichtbar bleibt
- INT-Pin wird zur Änderungserkennung verwendet

### OTA

- `ota` zeigt den OTA-Status
- `ota on` / `ota off` steuern OTA ein/aus
- `ota autooff X` deaktiviert OTA automatisch nach X Minuten
- `ota autooff off` deaktiviert Auto-Off

### Fehlerprotokoll

- Persistentes Logging auf `LittleFS` in `/error_log.txt`
- Maximal 20 Einträge
- Zeilen werden ohne `\r` gespeichert
- Fehlerzustände werden aktiv überwacht

## Debug-Menü Übersicht

### Allgemeine Befehle

- `help` – diese Hilfe anzeigen
- `exit` – Debug-Menü verlassen
- `reboot` – ESP neu starten
- `status` – Systemstatus anzeigen
- `temp` – ESP-Temperatur anzeigen

### AHT10

- `aht10 status` – AHT10-Status anzeigen
- `aht10 on | aht10 off` – AHT10 periodisch ein-/ausschalten
- `aht10 now` – AHT10 sofort messen und senden

### WLAN

- `wifi` – WLAN-Untermenü öffnen
- `ssid` – gespeicherte SSID anzeigen
- `pwd` – gespeichertes Passwort anzeigen
- `scan` – verfügbare WLAN-Netze scannen und speichern
- `back` – zurück zum Hauptmenü

### OTA

- `ota | ota on | ota off` – OTA-Status / starten / beenden
- `ota autooff X` – OTA nach X Minuten automatisch ausschalten
- `ota autooff off` – OTA Auto-Off deaktivieren

### PCA9555

- `pca status | pca in | pca out` – PCA9555 I/O anzeigen
- `pca out N on/off` – FET1..FET8 schalten
- `pca live | pca init` – Live-Ansicht / Neuinitialisierung

### WS2812

- `ws off` – WS2812 ausschalten
- `ws blink R G B I D` – Blinkfarbe R,G,B, Intervall I ms, Dauer D ms
- `ws on R G B` – Farbe dauerhaft einschalten

### CAN

- `can live` – CAN-Serial live verfolgen
- `can status` – aktuellen CAN/TWAI-Status anzeigen
- `can test [count]` – CAN-Testpakete senden und prüfen
- `sendcan 0xID;DLC;DATA` – CAN-Nachricht senden

### Fehler-Log

- `error l | error i | error c` – Fehler-Log anzeigen / Info / löschen

### Autoexit

- `autoexit X` – Debug nach X Minuten automatisch verlassen
- `autoexit off` – Auto-Exit deaktivieren

## Build und Deployment

### Voraussetzungen

- PlatformIO
- ESP32-S3-Toolchain
- USB-Verbindung oder OTA-Verbindung zum Gerät

### Build

```bash
platformio run
```

### Upload

```bash
platformio run --target upload --environment esp32s3
```

Falls OTA genutzt wird, prüfe `platformio.ini` auf `upload_port` und `upload_protocol`.

## Projektdateien

- `src/main.cpp` – Hauptprogramm, Setup, Loop, CAN/TWAI-Initialisierung, Debug-Aktivierung
- `include/config.h` – Pin- und Hardwarekonfiguration
- `include/AHT10.h` – AHT10-Messlogik, CAN-Senden und Status
- `include/PCA9555.h` – PCA9555-I2C-Kommunikation, Eingangs-/Ausgangslesen
- `include/DebugMenu.h` – Serielles Debug-Menü, Befehlsparser, Live-Modi
- `include/ErrorLog.h` – Persistentes Fehlerprotokoll, Log-Lesen/-Schreiben/-Löschen
- `include/CAN_SUBs.h` – CAN-Sendehilfen und Nachrichtenfunktionen
- `include/Wlan_Config.h` – WLAN-/OTA-bezogene Funktionen
- `include/FileSystem.h` – LittleFS / Dateisystem-Hilfen

## Hinweise

- Firmware nutzt `Wire.begin(SDA, SCL)` für I2C auf den konfigurierten Pins 47/48
- Der PCA9555-INT-Pin ist auf `IO21` als `INPUT_PULLUP` konfiguriert
- AHT10-Messungen sind nicht blockierend und arbeiten mit `millis()`
- Das Debug-Menü kann über die serielle Konsole gesteuert werden

---

Bei Bedarf kann die Dokumentation um zusätzliche Pinbelegungen, CAN-ID-Tabellen oder konkrete Projektanwendungsfälle ergänzt werden.