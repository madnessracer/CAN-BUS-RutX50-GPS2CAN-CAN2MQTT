#ifndef WS2812_H
#define WS2812_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <config.h>

enum Ws2812Mode
{
  WS2812_OFF = 0,
  WS2812_BLINK,
  WS2812_ON
};

static Adafruit_NeoPixel strip(1, LED_DATA_PIN, NEO_GRB + NEO_KHZ800);
static volatile Ws2812Mode ws2812Mode = WS2812_OFF;
static volatile uint32_t ws2812BlinkColor = 0;
static volatile unsigned long ws2812BlinkInterval = 2000;
static volatile unsigned long ws2812BlinkDuration = 10;
static unsigned long ws2812BlinkLast = 0;
static bool ws2812BlinkOn = false;
static TaskHandle_t ws2812TaskHandle = NULL;

static void ws2812SetMode(Ws2812Mode mode, uint32_t color, unsigned long intervalMs, unsigned long durationMs)
{
  ws2812Mode = mode;
  ws2812BlinkColor = color;
  ws2812BlinkInterval = intervalMs;
  ws2812BlinkDuration = durationMs;
  ws2812BlinkOn = false;
  ws2812BlinkLast = millis();
}

static void ws2812Off()
{
  ws2812SetMode(WS2812_OFF, 0, 0, 0);
  ws2812BlinkOn = false;
  strip.clear();
  strip.show();
}

static void ws2812SetBlinkRGB(uint8_t r, uint8_t g, uint8_t b, unsigned long intervalMs, unsigned long durationMs)
{
  ws2812SetMode(WS2812_BLINK, strip.Color(r, g, b), intervalMs, durationMs);
}

static void ws2812SetOnRGB(uint8_t r, uint8_t g, uint8_t b)
{
  ws2812SetMode(WS2812_ON, strip.Color(r, g, b), 0, 0);
}

static Ws2812Mode ws2812GetMode()
{
  return ws2812Mode;
}

static uint32_t ws2812GetColor()
{
  return ws2812BlinkColor;
}

static unsigned long ws2812GetInterval()
{
  return ws2812BlinkInterval;
}

static unsigned long ws2812GetDuration()
{
  return ws2812BlinkDuration;
}

static const char *ws2812GetModeName()
{
  switch (ws2812Mode)
  {
    case WS2812_OFF:
      return "OFF";
    case WS2812_BLINK:
      return "BLINK";
    case WS2812_ON:
      return "ON";
    default:
      return "UNKNOWN";
  }
}

static void ws2812Task(void *pvParameters)
{
  strip.begin();
  strip.setBrightness(50);
  strip.clear();
  strip.show();
  ws2812BlinkLast = millis();
  ws2812BlinkOn = false;

  for (;;)
  {
    unsigned long now = millis();

    if (ws2812Mode == WS2812_BLINK)
    {
      if (ws2812BlinkOn)
      {
        if (now - ws2812BlinkLast >= ws2812BlinkDuration)
        {
          ws2812BlinkOn = false;
          strip.clear();
          strip.show();
        }
      }
      else if (now - ws2812BlinkLast >= ws2812BlinkInterval)
      {
        ws2812BlinkOn = true;
        ws2812BlinkLast = now;
        strip.setPixelColor(0, ws2812BlinkColor);
        strip.show();
      }
    }
    else if (ws2812Mode == WS2812_ON)
    {
      if (!ws2812BlinkOn)
      {
        ws2812BlinkOn = true;
        strip.setPixelColor(0, ws2812BlinkColor);
        strip.show();
      }
    }
    else if (ws2812BlinkOn)
    {
      ws2812BlinkOn = false;
      strip.clear();
      strip.show();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void ws2812Init()
{
  strip.begin();
  strip.setBrightness(50);
  strip.clear();
  strip.show();
  xTaskCreatePinnedToCore(ws2812Task, "WS2812Task", 4096, NULL, 1, &ws2812TaskHandle, 1);
  ws2812Off();
}

#endif // WS2812_H
