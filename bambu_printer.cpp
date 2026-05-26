#include "bambu_printer.h"

BambuPrinter* BambuPrinter::instance = nullptr;

void BambuPrinter::begin(const SystemConfig &cfg) {
  config = cfg;
  state = PRINTER_DISCONNECTED;
  printerOnline = false;
  lastReconnectAttempt = 0;
  lastStatusRequest = 0;
  instance = this;
  amsDetected = false;
  amsExistBits = 0;

  for (uint8_t i = 0; i < MAX_DETECTED_AMS; i++) {
    detectedAms[i].id = i;
    detectedAms[i].connected = false;
    for (uint8_t t = 0; t < 4; t++) {
      detectedAms[i].trays[t][0] = '\0';
      detectedAms[i].trayColors[t][0] = '\0';
    }
  }

  tcpClient = nullptr;
  tlsClient = nullptr;
  mqttClient = nullptr;

  if (!config.mqttEnabled) return;

  if (config.mqttUseTLS) {
    tlsClient = new WiFiClientSecure();
    tlsClient->setInsecure();
    mqttClient = new PubSubClient(*tlsClient);
  } else {
    tcpClient = new WiFiClient();
    mqttClient = new PubSubClient(*tcpClient);
  }

  snprintf(mqttClientId, sizeof(mqttClientId), "BambuTagger-%06X", (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF));
  mqttClient->setServer(config.printerIP, config.printerPort);
  mqttClient->setCallback(staticMqttCallback);
  mqttClient->setBufferSize(MQTT_BUFFER_SIZE);
}

void BambuPrinter::update() {
  if (!config.mqttEnabled || !mqttClient) return;

  if (!mqttClient->connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 10000) {
      lastReconnectAttempt = now;
      reconnect();
    }
    state = PRINTER_DISCONNECTED;
    printerOnline = false;
    return;
  }

  mqttClient->loop();
  state = PRINTER_CONNECTED;

  unsigned long now = millis();
  if (now - lastStatusRequest > 30000) {
    lastStatusRequest = now;
    requestPrinterStatus();
  }
}

void BambuPrinter::reconnect() {
  if (!config.mqttEnabled || !mqttClient || !config.printerIP[0]) return;

  state = PRINTER_CONNECTING;

  char username[48];
  snprintf(username, sizeof(username), "bblp");
  if (mqttClient->connect(mqttClientId, username, config.printerAccessCode)) {
    state = PRINTER_CONNECTED;

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/#",
             config.mqttTopicPrefix, config.printerSerial);
    mqttClient->subscribe(topic);

    requestPrinterStatus();
  } else {
    state = PRINTER_ERROR;
  }
}

void BambuPrinter::sendSpoolData(uint8_t slot, const SpoolInfo &info) {
  if (!config.mqttEnabled || !mqttClient || !mqttClient->connected()) return;

  char payload[512];
  snprintf(payload, sizeof(payload),
           "{\"print\":{\"command\":\"ams_filament_setting\","
           "\"sequence_id\":\"%lu\","
           "\"ams_id\":%d,"
           "\"tray_id\":%d,"
           "\"tray_info_idx\":\"%s\","
           "\"tray_color\":\"%s\","
           "\"tray_type\":\"%s\","
           "\"remain\":%d,"
           "\"total\":%d,"
           "\"uid\":\"%s\"}}",
           millis(),
           config.amsUnit,
           (slot % 4),
           info.materialType,
           info.colorHex,
           info.materialType,
           info.remainingGrams,
           info.totalGrams,
           info.uid);

  char topic[128];
  snprintf(topic, sizeof(topic), "%s/%s/request",
           config.mqttTopicPrefix, config.printerSerial);

  mqttClient->publish(topic, payload);
}

void BambuPrinter::requestPrinterStatus() {
  if (!config.mqttEnabled || !mqttClient || !mqttClient->connected()) return;

  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"pushing\":{\"sequence_id\":\"%lu\",\"command\":\"pushall\"}}", millis());

  char topic[128];
  snprintf(topic, sizeof(topic), "%s/%s/request",
           config.mqttTopicPrefix, config.printerSerial);

  mqttClient->publish(topic, payload);
}

void BambuPrinter::mqttCallback(char* topic, byte* payload, unsigned int length) {
  printerOnline = true;

  if (length >= MQTT_BUFFER_SIZE) return;

  DynamicJsonDocument doc(MQTT_BUFFER_SIZE);
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) return;

  parseReport(doc);
}

void BambuPrinter::parseReport(JsonDocument &doc) {
  JsonObject printObj = doc["print"];
  if (!printObj) return;

  JsonObject amsObj = printObj["ams"];
  if (!amsObj) return;

  if (amsObj.containsKey("ams_exist_bits")) {
    JsonVariant v = amsObj["ams_exist_bits"];
    amsExistBits = v.is<const char*>() ? (uint8_t)strtoul(v.as<const char*>(), nullptr, 10) : v.as<uint8_t>();
  }

  if (amsExistBits == 0) {
    JsonArray arr = amsObj["ams"];
    if (arr) {
      for (JsonObject a : arr) {
        uint8_t id = a["id"] | 99;
        if (id < MAX_DETECTED_AMS) amsExistBits |= (1 << id);
        JsonArray trays = a["tray"];
        if (trays) {
          for (JsonObject t : trays) {
            uint8_t tid = t["id"] | 99;
            if (tid >= 4) continue;
            const char* mat = t["tray_info_idx"] | "";
            if (mat) strncpy(detectedAms[id].trays[tid], mat, 31);
            const char* col = t["tray_color"] | "";
            if (col) strncpy(detectedAms[id].trayColors[tid], col, 7);
          }
        }
      }
    }
  }

  if (amsExistBits > 0) {
    amsDetected = true;
    for (uint8_t i = 0; i < MAX_DETECTED_AMS; i++) {
      detectedAms[i].connected = (amsExistBits & (1 << i)) != 0;
    }
  }
}

bool BambuPrinter::isConnected() const {
  return mqttClient && mqttClient->connected();
}

PrinterState BambuPrinter::getState() const {
  return state;
}

bool BambuPrinter::isAmsDetected(uint8_t amsId) const {
  if (amsId >= MAX_DETECTED_AMS) return false;
  return amsDetected && detectedAms[amsId].connected;
}

uint8_t BambuPrinter::getAmsExistBits() const {
  return amsExistBits;
}

uint8_t BambuPrinter::getDetectedAmsCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_DETECTED_AMS; i++) {
    if (detectedAms[i].connected) count++;
  }
  return count;
}

const char* BambuPrinter::getAmsTrayMaterial(uint8_t amsId, uint8_t trayId) const {
  if (amsId >= MAX_DETECTED_AMS || trayId >= 4) return "";
  return detectedAms[amsId].trays[trayId];
}

const char* BambuPrinter::getAmsTrayColor(uint8_t amsId, uint8_t trayId) const {
  if (amsId >= MAX_DETECTED_AMS || trayId >= 4) return "";
  return detectedAms[amsId].trayColors[trayId];
}

void BambuPrinter::staticMqttCallback(char* topic, byte* payload, unsigned int length) {
  if (instance) instance->mqttCallback(topic, payload, length);
}
