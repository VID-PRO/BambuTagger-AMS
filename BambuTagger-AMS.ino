/*
 * BambuTagger AMS - Multi-Spool NFC Tag Reader for Bambu Lab
 *
 * Arduino IDE: ESP32 Dev Module (Tools > Board > ESP32 > ESP32 Dev Module)
 *
 * Required libraries (install via Tools > Manage Libraries):
 *   - MFRC522-spi-i2c-uart-async
 *   - Adafruit NeoPixel by Adafruit    (v1.12.3+)
 *   - Adafruit GFX Library by Adafruit (v1.11.11+)
 *   - Adafruit SSD1306   by Adafruit   (v2.5.12+)
 *   - PubSubClient   by Nick O'Leary   (v2.8+)
 *   - ArduinoJson    by Benoit Blanchon (v6.21.5+)
 *
 * Wiring: see config.h for pin assignments
 */

#include <ESPmDNS.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Update.h>

#include "config.h"
#include "rfid_manager.h"
#include "led_manager.h"
#include "display_manager.h"
#include "web_server.h"
#include "bambu_printer.h"

SystemConfig cfg;
RfidManager rfidManager;
LedManager ledManager;
DisplayManager displayManager;
WebInterface webInterface;
BambuPrinter bambuPrinter;
DNSServer dnsServer;

bool wifiConnected = false;
bool apMode = false;
String localIP = "";
const byte DNS_PORT = 53;

void handleReboot();
void connectWiFi();
void startCaptivePortal();
void updateLedStatus();

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.print(F("=== BambuTagger AMS v"));
  Serial.print(FIRMWARE_VERSION);
  Serial.println(F(" ==="));
  Serial.println(F("Multi-Spool NFC Tag Reader for Bambu Lab"));

  loadConfig(cfg);

  displayManager.begin(cfg.deviceName);
  displayManager.showBootScreen();

  Serial.print(F("Device: "));
  Serial.println(cfg.deviceName);

  ledManager.begin();
  ledManager.setAllLeds(LED_IDLE);
  ledManager.update();

  rfidManager.begin();
  Serial.println(F("RFID readers initialized"));

  connectWiFi();

  if (wifiConnected) {
    bambuPrinter.begin(cfg);
  }

  webInterface.begin(cfg, &rfidManager, &bambuPrinter, handleReboot, performOTAUpdate);
  if (wifiConnected) {
    Serial.println(F("Web server started on port 80"));
    Serial.println(localIP);
  } else {
    Serial.println(F("Web server started on AP 192.168.4.1"));
  }

  ledManager.setAllLeds(wifiConnected ? LED_WIFI_CONNECTED : LED_WIFI_DISCONNECTED);
  ledManager.update();

  Serial.println(F("Setup complete"));
}

void loop() {
  rfidManager.loop();

  webInterface.handleClient();
  if (apMode) {
    dnsServer.processNextRequest();
  }
  if (wifiConnected) {
    bambuPrinter.update();
  }

  static unsigned long lastReconnectCheck = 0;
  static unsigned long lastLedUpdate = 0;
  static unsigned long lastDisplayUpdate = 0;
  static unsigned long lastMqttUpdate = 0;
  unsigned long now = millis();

  if (apMode && strlen(cfg.wifiSSID) > 0 && now - lastReconnectCheck > 30000) {
    lastReconnectCheck = now;
    Serial.println(F("AP Mode: retrying STA connection..."));
    WiFi.begin(cfg.wifiSSID, cfg.wifiPassword);
  }

  if (WiFi.status() == WL_CONNECTED && apMode) {
    apMode = false;
    wifiConnected = true;
    localIP = WiFi.localIP().toString();
    dnsServer.stop();
    Serial.print(F("WiFi connected! IP: "));
    Serial.println(localIP);
    displayManager.showMessage("WiFi Connected", localIP.c_str());
    bambuPrinter.begin(cfg);
    if (MDNS.begin(cfg.deviceName)) {
      Serial.println(F("mDNS responder started"));
    }
  }

  if (now - lastLedUpdate > 200) {
    lastLedUpdate = now;
    updateLedStatus();
    ledManager.update();
  }

  if (now - lastDisplayUpdate > 1000) {
    lastDisplayUpdate = now;
    SpoolInfo displaySlots[NUM_SLOTS];
    for (uint8_t i = 0; i < NUM_SLOTS; i++) {
      rfidManager.getSpoolInfo(i, displaySlots[i]);
    }
    bool mqttOk = bambuPrinter.isConnected();
    displayManager.update(displaySlots, wifiConnected, mqttOk,
                          &bambuPrinter, cfg.amsUnit);
  }

  if (now - lastMqttUpdate > (cfg.mqttUpdateIntervalMs > 0 ? cfg.mqttUpdateIntervalMs : 5000)) {
    lastMqttUpdate = now;
    if (wifiConnected) {
      bool mqttOk = bambuPrinter.isConnected();
      webInterface.updateStatus(wifiConnected, localIP.c_str(), mqttOk,
                                (bambuPrinter.getState() == PRINTER_CONNECTED));

      if (cfg.mqttEnabled && mqttOk) {
        for (uint8_t i = 0; i < NUM_SLOTS; i++) {
          SpoolInfo info;
          if (rfidManager.getSpoolInfo(i, info) && info.present && info.tagReadSuccess) {
            bambuPrinter.sendSpoolData(i, info);
          }
        }
      }
    }
  }
}

void connectWiFi() {
  if (strlen(cfg.wifiSSID) == 0) {
    Serial.println(F("No WiFi credentials configured — starting AP"));
    wifiConnected = false;
    startCaptivePortal();
    return;
  }

  Serial.print(F("Connecting to WiFi: "));
  Serial.println(cfg.wifiSSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSSID, cfg.wifiPassword);

  ledManager.setAllLeds(LED_WIFI_DISCONNECTED);
  ledManager.update();

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    apMode = false;
    localIP = WiFi.localIP().toString();
    Serial.print(F("Connected! IP: "));
    Serial.println(localIP);
    displayManager.showMessage("WiFi Connected", localIP.c_str());

    if (MDNS.begin(cfg.deviceName)) {
      Serial.println(F("mDNS responder started"));
    }
  } else {
    wifiConnected = false;
    Serial.println(F("WiFi connection failed — starting AP"));
    displayManager.showMessage("WiFi Failed!", "Opening AP mode...");
    startCaptivePortal();
  }
}

void startCaptivePortal() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(cfg.deviceName, NULL);
  delay(100);

  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  localIP = "192.168.4.1";

  dnsServer.start(DNS_PORT, "*", apIP);

  Serial.print(F("AP started: "));
  Serial.print(cfg.deviceName);
  Serial.println(F(" (open)"));
  Serial.println(F("Connect and visit http://192.168.4.1"));

  char apMsg1[32];
  char apMsg2[32];
  char apMsg3[32];
  snprintf(apMsg1, sizeof(apMsg1), "WiFi: %s", cfg.deviceName);
  snprintf(apMsg2, sizeof(apMsg2), "IP: 192.168.4.1");
  snprintf(apMsg3, sizeof(apMsg3), "Configure WiFi");
  displayManager.showMessage(apMsg1, apMsg2, apMsg3);
}

static uint8_t hexToByte(const char* hex) {
  uint8_t val = 0;
  for (uint8_t i = 0; i < 2; i++) {
    char c = hex[i];
    val <<= 4;
    if (c >= '0' && c <= '9') val |= (c - '0');
    else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
    else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
  }
  return val;
}

void updateLedStatus() {
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    SpoolInfo info;
    bool present = rfidManager.getSpoolInfo(i, info);

    if (!wifiConnected && !apMode) {
      ledManager.setSlotLed(i, LED_WIFI_DISCONNECTED);
    } else if (apMode) {
      ledManager.setSlotLed(i, LED_IDLE);
    } else if (!present) {
      ledManager.setSlotLed(i, LED_IDLE);
    } else if (info.tagReadSuccess && info.colorHex[0]) {
      uint8_t r = hexToByte(info.colorHex);
      uint8_t g = hexToByte(info.colorHex + 2);
      uint8_t b = hexToByte(info.colorHex + 4);
      ledManager.setSlotColor(i, r, g, b);
    } else if (info.tagReadSuccess) {
      ledManager.setSlotLed(i, LED_TAG_OK);
    } else {
      ledManager.setSlotLed(i, LED_TAG_PRESENT);
    }
  }
}

void handleReboot() {
  Serial.println(F("Reboot requested via web..."));
  delay(500);
  ESP.restart();
}

void performOTAUpdate() {
  displayManager.showMessage("OTA Update", "Checking version...");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  String url = String("https://api.github.com/repos/") + OTA_REPO + "/releases/latest";
  if (!http.begin(client, url)) {
    displayManager.showMessage("OTA Update", "", "HTTP begin failed");
    delay(3000);
    return;
  }
  http.addHeader("User-Agent", String("BambuTagger-AMS/") + FIRMWARE_VERSION);

  int code = http.GET();
  if (code != 200) {
    displayManager.showMessage("OTA Update", "", "GitHub API error");
    delay(3000);
    http.end();
    return;
  }

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    displayManager.showMessage("OTA Update", "", "JSON parse error");
    delay(3000);
    return;
  }

  const char* tag = doc["tag_name"] | "";
  if (!tag[0]) {
    displayManager.showMessage("OTA Update", "", "No release found");
    delay(3000);
    return;
  }

  if (strcmp(tag, FIRMWARE_VERSION) == 0) {
    displayManager.showMessage("OTA Update", "", "Already up to date");
    delay(3000);
    return;
  }

  JsonArray assets = doc["assets"];
  if (!assets) {
    displayManager.showMessage("OTA Update", "", "No assets");
    delay(3000);
    return;
  }

  String binUrl;
  for (JsonObject asset : assets) {
    const char* name = asset["name"] | "";
    if (strstr(name, ".bin")) {
      binUrl = asset["browser_download_url"] | "";
      break;
    }
  }

  if (!binUrl.length()) {
    displayManager.showMessage("OTA Update", "", "No .bin found");
    delay(3000);
    return;
  }

  displayManager.showMessage("OTA Update", "Downloading...", tag);
  if (!http.begin(client, binUrl)) {
    displayManager.showMessage("OTA Update", "", "Download failed");
    delay(3000);
    return;
  }
  http.addHeader("User-Agent", String("BambuTagger-AMS/") + FIRMWARE_VERSION);

  code = http.GET();
  if (code != 200) {
    displayManager.showMessage("OTA Update", "", "Download error");
    delay(3000);
    http.end();
    return;
  }

  int len = http.getSize();
  if (len <= 0) {
    displayManager.showMessage("OTA Update", "", "Unknown size");
    delay(3000);
    http.end();
    return;
  }

  if (!Update.begin(len)) {
    displayManager.showMessage("OTA Update", "", "Update begin failed");
    delay(3000);
    http.end();
    return;
  }

  size_t written = Update.writeStream(http.getStream());
  http.end();

  if (written != (size_t)len) {
    displayManager.showMessage("OTA Update", "", "Write mismatch");
    delay(3000);
    return;
  }

  if (!Update.end()) {
    displayManager.showMessage("OTA Update", "", "Update end failed");
    delay(3000);
    return;
  }

  displayManager.showMessage("OTA Update", "", "Success! Rebooting...");
  delay(2000);
  ESP.restart();
}
