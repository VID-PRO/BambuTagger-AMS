#include "led_manager.h"

void LedManager::begin() {
  brightness = LED_BRIGHTNESS;
  strip = new Adafruit_NeoPixel(LED_COUNT, LED_DATA_PIN, NEO_GRB + NEO_KHZ800);
  strip->begin();
  strip->setBrightness(brightness);
  strip->clear();
  strip->show();

  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    currentMode[i] = LED_OFF;
    customR[i] = 0;
    customG[i] = 0;
    customB[i] = 0;
  }
  lastUpdate = 0;
}

void LedManager::setSlotLed(uint8_t slot, LedMode mode) {
  if (slot >= NUM_SLOTS) return;
  currentMode[slot] = mode;
}

void LedManager::setSlotColor(uint8_t slot, uint8_t r, uint8_t g, uint8_t b) {
  if (slot >= NUM_SLOTS) return;
  currentMode[slot] = LED_TAG_COLOR;
  customR[slot] = r;
  customG[slot] = g;
  customB[slot] = b;
}

void LedManager::setAllLeds(LedMode mode) {
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    currentMode[i] = mode;
  }
}

void LedManager::setBrightness(uint8_t b) {
  brightness = b;
  strip->setBrightness(brightness);
}

void LedManager::update() {
  unsigned long now = millis();
  if (now - lastUpdate < 50) return;
  lastUpdate = now;

  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    uint8_t r = 0, g = 0, b = 0;

    switch (currentMode[i]) {
      case LED_OFF:
        break;
      case LED_IDLE:
        b = 10;
        break;
      case LED_READING:
        r = 0; g = 0; b = 255;
        break;
      case LED_TAG_PRESENT:
        r = 128; g = 128; b = 0;
        break;
      case LED_TAG_COLOR:
        r = customR[i];
        g = customG[i];
        b = customB[i];
        break;
      case LED_ERROR:
        r = 255; g = 0; b = 0;
        break;
      case LED_WIFI_DISCONNECTED:
        r = 255; g = 64; b = 0;
        break;
      case LED_WIFI_CONNECTED:
        r = 0; g = 0; b = 64;
        break;
    }

    strip->setPixelColor(i, strip->Color(r, g, b));
  }
  strip->show();
}
