
#ifndef CAN_IDS_H
#define CAN_IDS_H

constexpr char WIFI_Name[] = "RutX50-CAN-GPS-MQTT"; // Geräte-Hostname für WiFi-Verbindung
constexpr uint8_t OtaDevieID = 0x0D;   // 13 in Dezimal, Geräte-ID für OTA-Updates über CAN

// Geräde IDs für CAN-Kommunikation
constexpr uint16_t MessageBasisID = 0x258; // 600 in Dezimal, Basis-ID für CAN-Nachrichten nur bist betriebsstundenzähler online ist

//constexpr uint16_t MessageBasisID = 0x51E; // 1310 in Dezimal, Basis-ID für CAN-Nachrichten
constexpr uint16_t SteuerID = 0x527; // 1327 in Dezimal, ID für das Senden von IP-Adresse an CAN  
constexpr uint16_t IP_Send_to_CAN = 0x528; // 1320 in Dezimal, ID für das Senden von IP-Adresse an CAN  

// Can-Bus IDs für die OTA-Übertragung von SSID und Passwort
constexpr uint16_t Can_Input_Wifi = 3000;
constexpr uint16_t Can_Output_Wifi_Scan = (Can_Input_Wifi + 1);
constexpr uint16_t Can_Output_wifi_SSID_Scan_Daten1 = (Can_Input_Wifi + 2);
constexpr uint16_t Can_Output_wifi_SSID_Scan_Daten2 = (Can_Input_Wifi + 3);
constexpr uint16_t Can_Output_wifi_SSID_Scan_Daten3 = (Can_Input_Wifi + 4);
constexpr uint16_t Can_Output_wifi_SSID_Scan_Daten4 = (Can_Input_Wifi + 5);
constexpr uint16_t Can_Output_wifi_SSID_Scan_Daten5 = (Can_Input_Wifi + 6);

constexpr uint16_t Can_Output_wifi_SSID_Daten1 = (Can_Input_Wifi + 10);
constexpr uint16_t Can_Output_wifi_SSID_Daten2 = (Can_Input_Wifi + 11);
constexpr uint16_t Can_Output_wifi_SSID_Daten3 = (Can_Input_Wifi + 12);
constexpr uint16_t Can_Output_wifi_SSID_Daten4 = (Can_Input_Wifi + 13);

constexpr uint16_t Can_Output_wifi_Passwort_Daten1 = (Can_Input_Wifi + 14);
constexpr uint16_t Can_Output_wifi_Passwort_Daten2 = (Can_Input_Wifi + 15);
constexpr uint16_t Can_Output_wifi_Passwort_Daten3 = (Can_Input_Wifi + 16);
constexpr uint16_t Can_Output_wifi_Passwort_Daten4 = (Can_Input_Wifi + 17);

#endif