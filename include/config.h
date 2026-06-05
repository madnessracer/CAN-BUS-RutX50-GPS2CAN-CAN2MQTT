#ifndef CONFIG_H
#define CONFIG_H

// PIN Definitionen

// TWAI (CAN-Bus Hardware Controller)
#define CAN_TX_PIN 36  // TWAI TX
#define CAN_RX_PIN 37  // TWAI RX
#define CAN_SE_PIN 38  // CAN Transceiver Silent/Enable

#define LED_DATA_PIN 46 // WS2812 data pin (einzelnes WS2812 RGB-Modul)

#define CAN_TX_SER 41  // Can Mqtt in/out UART TX
#define CAN_RX_SER 42  // Can Mqtt in/out UART RX
#define CAN_BAUD_RATE 115200

#define GPS_RX_SER 35 // GPS UART RX
#define GPS_TX_SER 6 // GPS UART TX
#define GPS_BAUD_RATE 115200

#define SDA 47
#define SCL 48


#endif