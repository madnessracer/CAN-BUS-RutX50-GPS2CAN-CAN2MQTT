#pragma once

/*
  CAN-OTA Drop-in-Modul

  Diese Header-Datei implementiert die OTA-Update-Zustandsmaschine und die
  Firmware-Schreiblogik. Sie initialisiert nicht die CAN- / TWAI-Hardware.
  Deine bestehende Anwendung muss TWAI bereits in main.cpp einrichten und
  empfangene Nachrichten an CAN_OTA::processMessage() weiterreichen.

  Verwendung:
    CAN_OTA::init(NODE_ID);
    ...
    if (twai_receive(&rx_msg, timeout) == ESP_OK) {
      CAN_OTA::processMessage(rx_msg);
    }
*/

#include <Arduino.h>
#include <driver/twai.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_task_wdt.h>
#include <stdint.h>
#include <string.h>

#ifndef CAN_OTA_NODE_ID
#define CAN_OTA_NODE_ID 1 // Standard-Node-ID (kann durch Definition vor Einbindung überschrieben werden)
#endif

namespace CAN_OTA {

static constexpr uint32_t UPDATE_CMD_BASE = 0x1FFFFF00UL;
static constexpr uint32_t UPDATE_ACK_BASE = 0x1FFFFF40UL;
static constexpr uint32_t UPDATE_DATA_ID  = 0x1FFFFF80UL;

static constexpr uint8_t CMD_UPDATE_START = 1;
static constexpr uint8_t CMD_UPDATE_DATA  = 2;
static constexpr uint8_t CMD_UPDATE_END   = 3;
static constexpr uint8_t CMD_UPDATE_ACK   = 4;
static constexpr uint8_t CMD_UPDATE_NACK  = 5;
static constexpr uint16_t UPDATE_ACK_WINDOW = 64;
static constexpr uint32_t OTA_TIMEOUT_MS = 30000;

// Zustandsvariablen (static: ein einziges Exemplar pro Translation Unit)
static bool update_mode = false;
static bool ota_in_progress = false;
static const esp_partition_t *ota_partition = nullptr;
static esp_ota_handle_t ota_handle = 0;
static uint32_t expected_size = 0;
static uint16_t expected_crc = 0;
static uint32_t received_bytes = 0;
static uint16_t expected_seq = 0;
static uint16_t calculated_crc = 0xFFFF;
static uint8_t node_id = CAN_OTA_NODE_ID;
static uint32_t ota_start_ms = 0;
static uint8_t session_ack_window = 0; // wird per START-Kommando ausgehandelt

// Optionaler Callback für Frames, die nicht zum OTA-Protokoll gehören.
typedef void (*CanMessageCallback)(const twai_message_t&);
static CanMessageCallback on_non_ota_message = nullptr;

// Send-Callback: wird von sendAck() benutzt, um ACK-Frames zu senden.
// Muss gesetzt werden wenn kein TWAI-Treiber verwendet wird (z.B. ESP32-Arduino-CAN).
typedef void (*CanSendRawCallback)(uint32_t id, const uint8_t *data, uint8_t dlc, bool extd);
static CanSendRawCallback on_send_raw = nullptr;

static uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t length) {
  while (length--) {
    crc ^= (uint16_t)(*data++) << 8;
    for (uint8_t i = 0; i < 8; i++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

static void resetUpdateState() {
  update_mode = false;
  ota_in_progress = false;
  ota_handle = 0;
  ota_partition = nullptr;
  expected_size = 0;
  expected_crc = 0;
  received_bytes = 0;
  expected_seq = 0;
  calculated_crc = 0xFFFF;
  ota_start_ms = 0;
  session_ack_window = 0;
}

static void sendAck(uint8_t status, uint8_t info = 0) {
  uint32_t ack_id = UPDATE_ACK_BASE + node_id;
  uint8_t buf[2] = {status, info};
  if (on_send_raw) {
    on_send_raw(ack_id, buf, 2, true);
  } else {
    // Fallback: TWAI-Treiber (nur wenn per twai_driver_install() initialisiert)
    twai_message_t ack = {};
    ack.identifier = ack_id;
    ack.extd = 1;
    ack.data_length_code = 2;
    ack.data[0] = status;
    ack.data[1] = info;
    esp_err_t tx_err = twai_transmit(&ack, pdMS_TO_TICKS(100));
    (void)tx_err;
  }
}

static esp_err_t startOtaPartition(uint32_t size) {
  ota_partition = esp_ota_get_next_update_partition(nullptr);
  if (!ota_partition) {
    return ESP_ERR_NOT_FOUND;
  }
  if (size > ota_partition->size) {
    ota_partition = nullptr;
    return ESP_ERR_INVALID_SIZE;
  }
  // esp_ota_begin löscht je nach IDF-Version die gesamte OTA-Partition synchron (~10s+).
  // Daher WDT kurz deaktivieren, damit der Watchdog nicht feuert.
  esp_task_wdt_delete(NULL);
  esp_err_t err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();
  return err;
}

static void abortOta() {
  if (ota_in_progress && ota_handle) {
    esp_ota_abort(ota_handle);
  }
  resetUpdateState();
}

// Gibt true zurück wenn OTA erfolgreich abgeschlossen und Boot-Partition gesetzt.
static bool finishOta() {
  if (!ota_in_progress) {
    return false;
  }
  if (esp_ota_end(ota_handle) != ESP_OK) {
    abortOta();
    return false;
  }
  if (esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
    abortOta();
    return false;
  }
  ota_handle = 0;
  ota_partition = nullptr;
  ota_in_progress = false;
  update_mode = false;
  return true;
}

static void handleStartCommand(const twai_message_t &msg) {
  if (msg.data_length_code < 8 || msg.data[0] != CMD_UPDATE_START) {
    return;
  }
  if (update_mode) {
    sendAck(CMD_UPDATE_NACK, 0x01);
    return;
  }

  expected_size = (uint32_t)msg.data[2] | ((uint32_t)msg.data[3] << 8) | ((uint32_t)msg.data[4] << 16) | ((uint32_t)msg.data[5] << 24);
  expected_crc = (uint16_t)msg.data[6] | ((uint16_t)msg.data[7] << 8);

  // Byte[1]: gewünschte ACK-Fensterbreite vom Sender (0 = Firmware-Standard)
  session_ack_window = (msg.data[1] != 0) ? msg.data[1] : UPDATE_ACK_WINDOW;

  if (expected_size == 0) {
    sendAck(CMD_UPDATE_NACK, 0x02);
    return;
  }

  esp_err_t ota_err = startOtaPartition(expected_size);
  if (ota_err == ESP_ERR_NOT_FOUND) {
    sendAck(CMD_UPDATE_NACK, 0x03);
    return;
  }
  if (ota_err == ESP_ERR_INVALID_SIZE) {
    sendAck(CMD_UPDATE_NACK, 0x04); // Firmware zu groß für Partition
    return;
  }
  if (ota_err != ESP_OK) {
    sendAck(CMD_UPDATE_NACK, 0x05);
    return;
  }

  update_mode = true;
  ota_in_progress = true;
  received_bytes = 0;
  expected_seq = 0;
  calculated_crc = 0xFFFF;
  ota_start_ms = millis();
  sendAck(CMD_UPDATE_ACK, 0x00);
}

static void handleDataFrame(const twai_message_t &msg) {
  if (!update_mode || !ota_in_progress) {
    return;
  }
  if (msg.data_length_code < 2) {
    return;
  }

  uint16_t seq = ((uint16_t)msg.data[0] << 8) | msg.data[1];
  if (seq != expected_seq) {
    sendAck(CMD_UPDATE_NACK, 0x10);
    return;
  }

  uint8_t payload_len = msg.data_length_code - 2;
  if (payload_len > 6) {
    sendAck(CMD_UPDATE_NACK, 0x11);
    abortOta();
    return;
  }

  if (received_bytes + payload_len > expected_size) {
    sendAck(CMD_UPDATE_NACK, 0x13); // Mehr Daten als angekündigt
    abortOta();
    return;
  }

  esp_err_t err = esp_ota_write(ota_handle, msg.data + 2, payload_len);
  if (err != ESP_OK) {
    sendAck(CMD_UPDATE_NACK, 0x12);
    abortOta();
    return;
  }

  calculated_crc = crc16_update(calculated_crc, msg.data + 2, payload_len);
  received_bytes += payload_len;
  expected_seq++;
  ota_start_ms = millis(); // Timeout bei jedem Paket zurücksetzen

  bool is_final_packet = (received_bytes == expected_size);
  if (((seq + 1) % session_ack_window == 0) || is_final_packet) {
    sendAck(CMD_UPDATE_ACK, 0x01);
  }
}

static void handleEndCommand(const twai_message_t &msg) {
  if (!update_mode || !ota_in_progress || msg.data_length_code < 1 || msg.data[0] != CMD_UPDATE_END) {
    return;
  }

  if (received_bytes != expected_size) {
    sendAck(CMD_UPDATE_NACK, 0x20);
    abortOta();
    return;
  }

  if (calculated_crc != expected_crc) {
    sendAck(CMD_UPDATE_NACK, 0x22);
    abortOta();
    return;
  }

  if (!finishOta()) {
    sendAck(CMD_UPDATE_NACK, 0x23); // OTA-Abschluss oder Partition-Setzen fehlgeschlagen
    return;
  }
  incrementFirmwareVersion();
  sendAck(CMD_UPDATE_ACK, 0x02);
  vTaskDelay(pdMS_TO_TICKS(200)); // ACK senden lassen, dann neu starten
  esp_restart();
}

// Timeout-Überwachung. Zyklisch aus dem Haupt-Loop aufrufen (z.B. alle 100 ms).
// Bricht ein laufendes OTA ab, wenn OTA_TIMEOUT_MS ohne Datenpaket vergangen sind.
inline void tick() {
  if (update_mode && ota_start_ms > 0 &&
      (millis() - ota_start_ms > OTA_TIMEOUT_MS)) {
    abortOta();
  }
}

// OTA-Zustand initialisieren. Einmal aufrufen, nachdem die CAN/TWAI-Hardware
// in main.cpp konfiguriert wurde.
inline void init(uint8_t id = CAN_OTA_NODE_ID) {
  node_id = id;
  resetUpdateState();
}

// Gibt true zurück wenn gerade ein OTA-Update läuft.
inline bool isUpdateMode() { return update_mode; }

// Optionalen Callback für Nicht-OTA-Frames registrieren.
// Wird für jeden CAN-Frame aufgerufen, der nicht zum OTA-Protokoll gehört.
inline void setMessageCallback(CanMessageCallback cb) {
  on_non_ota_message = cb;
}

// Send-Callback registrieren. Muss aufgerufen werden wenn kein TWAI-Treiber
// verwendet wird (z.B. bei ESP32-Arduino-CAN). Der Callback erhält ID, Daten,
// DLC und ob Extended-Frame, und soll den Frame über den eigenen CAN-Stack senden.
inline void setSendCallback(CanSendRawCallback cb) {
  on_send_raw = cb;
}

// Verarbeite einen empfangenen CAN/TWAI-Rahmen. Die Anwendung muss empfangene
// Rahmen an diese Funktion weitergeben.
inline void processMessage(const twai_message_t &rx_msg) {
  if (!rx_msg.extd) {
    if (on_non_ota_message) on_non_ota_message(rx_msg);
    return;
  }

  uint32_t startCmdId = UPDATE_CMD_BASE + node_id;
  uint32_t endCmdId = UPDATE_CMD_BASE + node_id + 0x10UL;

  if (rx_msg.identifier == startCmdId) {
    handleStartCommand(rx_msg);
  } else if (rx_msg.identifier == endCmdId) {
    handleEndCommand(rx_msg);
  } else if (rx_msg.identifier == UPDATE_DATA_ID) {
    handleDataFrame(rx_msg);
  } else {
    if (on_non_ota_message) on_non_ota_message(rx_msg);
  }
}

// Vollständiger Loop-Handler: ersetzt tick() + twai_receive() im Haupt-Loop.
// Ruft tick() intern auf – kein separater tick()-Aufruf nötig.
// Nicht-OTA-Frames werden an den per setMessageCallback() registrierten Callback weitergegeben.
inline void loop() {
  tick(); // OTA-Timeout überwachen

  // Bus-Off-Recovery: TWAI-Treiber nach Fehler automatisch wiederanlaufen lassen
  twai_status_info_t twai_status = {};
  if (twai_get_status_info(&twai_status) == ESP_OK) {
    if (twai_status.state == TWAI_STATE_BUS_OFF) {
      if (update_mode) abortOta();
      twai_initiate_recovery();
      return;
    }
    if (twai_status.state == TWAI_STATE_RECOVERING) {
      return;
    }
  }

  // Erste Nachricht abwarten, dann gesamte Queue non-blocking leeren.
  // Verhindert Queue-Überlauf wenn esp_ota_write() einen Flash-Sektor löscht.
  twai_message_t rx_msg = {};
  if (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK) {
    processMessage(rx_msg);
    while (twai_receive(&rx_msg, 0) == ESP_OK) {
      processMessage(rx_msg);
    }
  }
}

// Alternative Schnittstelle für eigene CAN-Stacks: ID, Nutzdaten und DLC übergeben.
inline void processMessage(uint32_t identifier, const uint8_t *data, uint8_t dlc, bool extd = true) {
  if (!extd || dlc > 8) {
    return;
  }

  twai_message_t msg = {};
  msg.identifier = identifier;
  msg.extd = 1;
  msg.data_length_code = dlc;
  memcpy(msg.data, data, dlc);
  processMessage(msg);
}

} // namespace CAN_OTA
