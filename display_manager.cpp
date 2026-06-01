#include "display_manager.h"

void DisplayManager::begin(const char* devName) {
  strncpy(deviceName, devName, sizeof(deviceName) - 1);
  Wire.begin(I2C_SDA, I2C_SCL);
  display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
  if (!display->begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    display = nullptr;
    return;
  }
  display->setTextSize(1);
  display->setTextColor(SSD1306_WHITE);
  display->cp437(true);
  lastUpdate = 0;
}

void DisplayManager::update(const SpoolInfo slots[NUM_SLOTS], bool wifiConnected,
                            bool mqttConnected, BambuPrinter* printer,
                            uint8_t amsUnit, float temp, float humidity) {
  if (!display) return;
  unsigned long now = millis();
  if (now - lastUpdate < 500) return;
  lastUpdate = now;

  display->clearDisplay();
  drawStatusBar(wifiConnected);
  if (mqttConnected && printer && printer->isAmsDetected(amsUnit)) {
    drawPrinterSlots(printer, amsUnit);
  } else {
    drawSlotGrid(slots);
  }
  drawFooter(mqttConnected, printer ? printer->isPrinterOnline() : false, temp, humidity);
  display->display();
}

void DisplayManager::drawStatusBar(bool wifiConnected) {
  display->fillRect(0, 0, SCREEN_WIDTH, 8, SSD1306_WHITE);
  display->setTextColor(SSD1306_BLACK);
  display->setCursor(2, 0);
  display->print(deviceName);

  const char* wifiText = wifiConnected ? "WiFi" : "NO WIFI";
  int16_t x = SCREEN_WIDTH - (strlen(wifiText) * 6);
  display->setCursor(x, 0);
  display->print(wifiText);
}

void DisplayManager::drawSlotGrid(const SpoolInfo slots[NUM_SLOTS]) {
  display->setTextColor(SSD1306_WHITE);

  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    uint8_t y = 9 + (i * 10);

    display->setCursor(0, y);
    display->printf("%d:", i + 1);

    if (slots[i].present && slots[i].tagReadSuccess) {
      char matShort[11];
      strncpy(matShort, slots[i].materialType, 10);
      matShort[10] = '\0';
      display->setCursor(12, y);
      display->print(matShort);

      if (slots[i].colorHex[0]) {
        display->setCursor(78, y);
        display->print("#");
        display->print(slots[i].colorHex);
      }

      if (slots[i].totalGrams > 0) {
        uint8_t pct = (uint16_t)((uint32_t)slots[i].remainingGrams * 100 / slots[i].totalGrams);
        display->setCursor(SCREEN_WIDTH - 22, y);
        display->printf("%3d%%", pct);
      }
    } else if (slots[i].present) {
      display->setCursor(12, y);
      display->print("reading...");
    } else {
      display->setCursor(12, y);
      display->print("empty");
    }
  }
}

void DisplayManager::drawPrinterSlots(BambuPrinter* printer, uint8_t amsUnit) {
  display->setTextColor(SSD1306_WHITE);

  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    uint8_t y = 9 + (i * 10);

    display->setCursor(0, y);
    display->printf("%d:", i + 1);

    const char* ttype = printer->getAmsTrayType(amsUnit, i);
    if (ttype && ttype[0]) {
      char matShort[11];
      strncpy(matShort, ttype, 10);
      matShort[10] = '\0';
      display->setCursor(12, y);
      display->print(matShort);

      const char* col = printer->getAmsTrayColor(amsUnit, i);
      if (col && col[0]) {
        // Show first 6 chars to fit screen (skip alpha)
        char col6[7];
        strncpy(col6, col, 6); col6[6] = '\0';
        display->setCursor(72, y);
        display->print("#");
        display->print(col6);
      }
    } else {
      display->setCursor(12, y);
      display->print("empty");
    }
  }
}

void DisplayManager::drawFooter(bool mqttConnected, bool printerOnline, float temp, float humidity) {
  display->drawFastHLine(0, 55, SCREEN_WIDTH, SSD1306_WHITE);
  display->setCursor(0, 57);
  if (temp > -99) {
    display->printf("%.0fC %.0f%%", temp, humidity);
  } else {
    display->print(mqttConnected ? "MQTT:OK" : "MQTT:--");
  }
  const char* ptrText = printerOnline ? "PTR:OK" : "PTR:--";
  int16_t x = SCREEN_WIDTH - (strlen(ptrText) * 6);
  display->setCursor(x, 57);
  display->print(ptrText);
}

void DisplayManager::showOtaProgress(const char* line1, const char* line2,
                                      const char* line3, int pct) {
  if (!display) return;
  display->clearDisplay();
  drawStatusBar(true);
  display->setTextColor(SSD1306_WHITE);
  display->setCursor(0, 14);
  if (line1) display->println(line1);
  if (line2) { display->setCursor(0, 24); display->println(line2); }
  if (line3) { display->setCursor(0, 36); display->println(line3); }
  if (pct >= 0) {
    int barY = 48;
    int barW = SCREEN_WIDTH - 8;
    display->drawRect(2, barY, barW + 4, 6, SSD1306_WHITE);
    if (pct > 0) display->fillRect(4, barY + 1, (barW * pct) / 100, 4, SSD1306_WHITE);
    display->setCursor(SCREEN_WIDTH / 2 - 12, barY + 8);
    display->printf("%d%%", pct);
  }
  drawFooter(false, false);
  display->display();
}

void DisplayManager::showMessage(const char* line1, const char* line2,
                                  const char* line3, const char* line4) {
  if (!display) return;
  display->clearDisplay();
  display->setTextColor(SSD1306_WHITE);
  display->setCursor(0, 0);
  display->println(line1);
  if (line2) display->println(line2);
  if (line3) display->println(line3);
  if (line4) display->println(line4);
  display->display();
}

void DisplayManager::showBootScreen() {
  if (!display) return;
  display->clearDisplay();
  display->drawBitmap(0, 0, SPLASH_BITMAP, SPLASH_WIDTH, SPLASH_HEIGHT, SSD1306_WHITE);
  display->display();
}
