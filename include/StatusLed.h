#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <Arduino.h>
#include "ErrorLog.h"
#include "Ws2812.h"

inline uint8_t statusLedR = 0;
inline uint8_t statusLedG = 0;
inline uint8_t statusLedB = 0;
inline unsigned long statusLedInterval = 0;
inline unsigned long statusLedDuration = 0;

static inline unsigned long getEffectiveBlinkInterval()
{
  unsigned long interval = ws2812GetInterval();
  return interval > 0 ? interval : 3000;
}

static inline unsigned long getEffectiveBlinkDuration()
{
  unsigned long duration = ws2812GetDuration();
  return duration > 0 ? duration : 50;
}

static inline void setStatusLedBlink(uint8_t r, uint8_t g, uint8_t b)
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

inline void updateCanStatusLed()
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

#endif // STATUS_LED_H
