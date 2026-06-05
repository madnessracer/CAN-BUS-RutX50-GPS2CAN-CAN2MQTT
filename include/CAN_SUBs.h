#ifndef __CAN_SUBs_H__
#define __CAN_SUBs_H__

#include <cstdarg>
#include "driver/twai.h"
#include "ErrorLog.h"

// TWAI-Nachrichten-Strukturen
static twai_message_t rx_frame; // NUR zum Empfangen
static twai_message_t tx;       // NUR zum Senden

static const uint32_t CAN_BUS_SPEED = 250000; // 250 kbit/s

static inline twai_timing_config_t CAN_GetTimingConfig(uint32_t speed)
{
  switch (speed)
  {
    case 100000:
      return TWAI_TIMING_CONFIG_100KBITS();
    case 125000:
      return TWAI_TIMING_CONFIG_125KBITS();
    case 250000:
      return TWAI_TIMING_CONFIG_250KBITS();
    case 500000:
      return TWAI_TIMING_CONFIG_500KBITS();
    case 1000000:
      return TWAI_TIMING_CONFIG_1MBITS();
    default:
      Serial.printf("Unbekannte CAN-Geschwindigkeit %lu, verwende 250kBit/s\n", speed);
      return TWAI_TIMING_CONFIG_250KBITS();
  }
}

static uint8_t s_rxQueueLen = 50; // rxQueueLen = 50 (anpassbar)
static bool canBusIsOff = false;
static bool canSendAllowed = true;
static unsigned long canSendPauseUntil = 0;
static unsigned int canTxFailStreak = 0;

static inline bool CAN_IsBusOff()
{
  return canBusIsOff;
}

static inline bool CAN_CanSend()
{
  if (millis() >= canSendPauseUntil)
  {
    if (!canSendAllowed)
    {
      canSendAllowed = true;
    }
  }
  if (!canSendAllowed)
  {
    return false;
  }
  if (millis() < canSendPauseUntil)
  {
    return false;
  }
  return true;
}

static inline void CAN_UpdateSendPause(bool failed)
{
  if (failed)
  {
    canTxFailStreak++;
    if (canTxFailStreak == 1)
    {
      errorLogAddOnce("CAN_TX_FAIL", "TWAI Senden fehlgeschlagen.");
    }
    if (canTxFailStreak >= 3)
    {
      canSendAllowed = false;
      canSendPauseUntil = millis() + 5000;
    }
  }
  else
  {
    if (errorLogIsActive("CAN_TX_FAIL"))
    {
      Serial.println("TWAI Senden erfolgreich.");
      errorLogResolve("CAN_TX_FAIL");
    }
    canTxFailStreak = 0;
    canSendAllowed = true;
    canSendPauseUntil = 0;
  }
}

static inline bool CAN_Init(gpio_num_t txPin, gpio_num_t rxPin, uint8_t rxQueueLen)
{
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(txPin, rxPin, TWAI_MODE_NORMAL);
  g_config.rx_queue_len = rxQueueLen;

  twai_timing_config_t t_config = CAN_GetTimingConfig(CAN_BUS_SPEED);
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t ret = twai_driver_install(&g_config, &t_config, &f_config);
  if (ret != ESP_OK)
  {
    Serial.printf("TWAI install fehlgeschlagen: 0x%X\n", ret);
    return false;
  }

  ret = twai_start();
  if (ret != ESP_OK)
  {
    Serial.printf("TWAI start fehlgeschlagen: 0x%X\n", ret);
    return false;
  }

  uint32_t alertsToEnable = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED | TWAI_ALERT_ERR_PASS | TWAI_ALERT_ABOVE_ERR_WARN;
  twai_reconfigure_alerts(alertsToEnable, NULL);

  canBusIsOff = false;
  canSendAllowed = true;
  canSendPauseUntil = 0;
  canTxFailStreak = 0;
  Serial.printf("TWAI (CAN) Treiber initialisiert - %lukBit/s, Alerts aktiv\n", CAN_BUS_SPEED / 1000);
  return true;
}

static inline void CAN_Stop()
{
  twai_stop();
  twai_driver_uninstall();
}

static inline bool CAN_Recover(gpio_num_t txPin, gpio_num_t rxPin, uint8_t rxQueueLen)
{
  Serial.println("TWAI Recovery: stoppe und initialisiere neu...");
  CAN_Stop();
  delay(100);
  bool recovered = CAN_Init(txPin, rxPin, rxQueueLen);
  if (recovered)
  {
    canBusIsOff = false;
  }
  return recovered;
}

static inline bool CAN_CheckBus(gpio_num_t txPin, gpio_num_t rxPin, uint8_t rxQueueLen)
{
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK)
  {
    Serial.println("TWAI Statusabfrage fehlgeschlagen.");
    errorLogAddOnce("CAN_STATUS_FAIL", "TWAI Statusabfrage fehlgeschlagen.");
    return false;
  }

  if (errorLogIsActive("CAN_STATUS_FAIL"))
  {
    Serial.println("TWAI Statusabfrage erfolgreich.");
    errorLogResolve("CAN_STATUS_FAIL");
  }

  if (status.state == TWAI_STATE_BUS_OFF)
  {
    if (!canBusIsOff)
    {
      Serial.println("TWAI Bus-Off erkannt.");
      errorLogAddOnce("CAN_BUS_OFF", "TWAI Bus-Off erkannt.");
      canBusIsOff = true;
      canSendAllowed = false;
      canSendPauseUntil = millis() + 10000;
    }
    bool recovered = CAN_Recover(txPin, rxPin, rxQueueLen);
    if (!recovered)
    {
      Serial.println("TWAI Recovery fehlgeschlagen, erneut versuchen...");
      return false;
    }
    Serial.println("TWAI Recovery erfolgreich.");
    canSendAllowed = true;
    return true;
  }

  if (canBusIsOff)
  {
    Serial.println("TWAI Bus wiederhergestellt.");
    if (errorLogIsActive("CAN_BUS_OFF"))
    {
      errorLogResolve("CAN_BUS_OFF");
    }
    canBusIsOff = false;
    canSendAllowed = true;
    canSendPauseUntil = 0;
    canTxFailStreak = 0;
  }

  if (status.tx_error_counter >= 128 || status.rx_error_counter >= 128)
  {
    char warnBuf[80];
    snprintf(warnBuf, sizeof(warnBuf), "TX=%u RX=%u", status.tx_error_counter, status.rx_error_counter);
    if (!errorLogIsActive("CAN_ERR_WARN"))
    {
      Serial.printf("TWAI Fehlerwarnung: %s\n", warnBuf);
      errorLogAddOnce("CAN_ERR_WARN", warnBuf);
    }
  }
  else if (errorLogIsActive("CAN_ERR_WARN"))
  {
    Serial.println("TWAI Fehlerwarnung behoben.");
    errorLogResolve("CAN_ERR_WARN");
  }

  return true;
}

static inline void CAN_Send(uint MesageID, byte MesageByte1 = 0, byte MesageByte2 = 0, byte MesageByte3 = 0, byte MesageByte4 = 0, byte MesageByte5 = 0, byte MesageByte6 = 0, byte MesageByte7 = 0, byte MesageByte8 = 0)
{
  tx.extd = 1; // Extended Frame (29-bit ID)
  tx.identifier = MesageID;
  tx.data_length_code = 8;
  tx.data[0] = MesageByte1;
  tx.data[1] = MesageByte2;
  tx.data[2] = MesageByte3;
  tx.data[3] = MesageByte4;
  tx.data[4] = MesageByte5;
  tx.data[5] = MesageByte6;
  tx.data[6] = MesageByte7;
  tx.data[7] = MesageByte8;

  if (!CAN_CanSend())
  {
    return;
  }

  esp_err_t ret = twai_transmit(&tx, pdMS_TO_TICKS(1000)); // Timeout 1 Sekunde
  if (ret != ESP_OK)
  {
    CAN_UpdateSendPause(true);
  }
  else
  {
    CAN_UpdateSendPause(false);
  }
}

static inline void CAN_SendEx(bool frameExtended, uint8_t dlc, uint MesageID, ...)
{
  if (dlc < 1) dlc = 1;
  if (dlc > 8) dlc = 8;

  tx.extd = frameExtended ? 1 : 0; // 1 für Extended, 0 für Standard
  tx.identifier = MesageID;
  tx.data_length_code = dlc;

  va_list args;
  va_start(args, MesageID);
  for (uint8_t i = 0; i < dlc; ++i) {
    int v = va_arg(args, int);
    tx.data[i] = (uint8_t)v;
  }
  va_end(args);

  for (uint8_t i = dlc; i < 8; ++i) tx.data[i] = 0;
  if (!CAN_CanSend())
  {
    return;
  }

  esp_err_t ret = twai_transmit(&tx, pdMS_TO_TICKS(1000)); // Timeout 1 Sekunde
  if (ret != ESP_OK)
  {
    CAN_UpdateSendPause(true);
  }
  else
  {
    CAN_UpdateSendPause(false);
  }
}
#endif