#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "splash_logo.h"

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

class DisplayManager {
public:
  void begin(const char* deviceName);
  void update(const SpoolInfo slots[NUM_SLOTS], bool wifiConnected,
              bool mqttConnected);
  void showMessage(const char* line1, const char* line2 = nullptr,
                   const char* line3 = nullptr, const char* line4 = nullptr);
  void showBootScreen();

private:
  void drawStatusBar(bool wifiConnected);
  void drawSlotGrid(const SpoolInfo slots[NUM_SLOTS]);
  void drawFooter(bool mqttConnected);

  Adafruit_SSD1306* display;
  char deviceName[32];
  unsigned long lastUpdate;
};

#endif
