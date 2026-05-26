#include "rfid_manager.h"
#include "tag_parser.h"

void RfidManager::begin() {
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  SPI.setClockDivider(SPI_CLOCK_DIV16);

  Serial.println(F("RFID init:"));
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    pinMode(SS_PINS[i], OUTPUT);
    digitalWrite(SS_PINS[i], HIGH);
    pinMode(RST_PINS[i], OUTPUT);
    digitalWrite(RST_PINS[i], HIGH);

    mfrc522[i] = new MFRC522(SS_PINS[i], RST_PINS[i]);
    selectReader(i);
    mfrc522[i]->PCD_Init();
    mfrc522[i]->PCD_SetAntennaGain(MFRC522::RxGain_max);
    deselectAll();

    byte ver = mfrc522[i]->PCD_ReadRegister(MFRC522::VersionReg);
    Serial.printf("  Slot %d SS=%d RST=%d ver=0x%02X %s\n", i,
                  SS_PINS[i], RST_PINS[i], ver,
                  (ver == 0x92 || ver == 0x91) ? "OK" : "FAIL");

    TagParser::clear(spoolData[i]);
    lastPoll[i] = 0;
  }

  currentSlot = 0;
  initialized = true;
}

void RfidManager::loop() {
  if (!initialized) return;

  unsigned long now = millis();
  if (now - lastPoll[currentSlot] < RFID_POLL_INTERVAL_MS) return;

  lastPoll[currentSlot] = now;

  static unsigned long lastHeartbeat = 0;
  if (now - lastHeartbeat > 10000) {
    lastHeartbeat = now;
    Serial.printf("RFID: polling slot %d (SS=%d)\n", currentSlot, SS_PINS[currentSlot]);
  }

  selectReader(currentSlot);
  SpoolInfo newInfo;
  TagParser::clear(newInfo);

  bool tagFound = readNtag(currentSlot, newInfo);

  if (tagFound) {
    bool wasPresent = spoolData[currentSlot].present;
    bool sameTag = (strcmp(newInfo.uid, spoolData[currentSlot].uid) == 0);

    if (!wasPresent || !sameTag) {
      spoolData[currentSlot] = newInfo;
      spoolData[currentSlot].lastSeen = now;
      spoolData[currentSlot].present = true;
    } else {
      spoolData[currentSlot].lastSeen = now;
    }
  } else {
    if (spoolData[currentSlot].present) {
      if (now - spoolData[currentSlot].lastSeen > RFID_DEBOUNCE_MS) {
        TagParser::clear(spoolData[currentSlot]);
      }
    }
  }

  deselectAll();
  currentSlot = (currentSlot + 1) % NUM_SLOTS;
}

bool RfidManager::readNtag(uint8_t slot, SpoolInfo &info) {
  MFRC522* reader = mfrc522[slot];

  if (!reader->PICC_IsNewCardPresent()) return false;
  if (!reader->PICC_ReadCardSerial()) return false;

  MFRC522::PICC_Type piccType = reader->PICC_GetType(reader->uid.sak);
  Serial.printf("Slot %d: tag type=%s (SAK=0x%02X)\n",
                slot, reader->PICC_GetTypeName(piccType), reader->uid.sak);

  char uidStr[16];
  snprintf(uidStr, sizeof(uidStr), "%02X%02X%02X%02X%02X%02X%02X",
           reader->uid.uidByte[0], reader->uid.uidByte[1],
           reader->uid.uidByte[2], reader->uid.uidByte[3],
           reader->uid.uidByte[4], reader->uid.uidByte[5],
           reader->uid.uidByte[6]);

  bool success = false;
  if (piccType == MFRC522::PICC_TYPE_MIFARE_1K || piccType == MFRC522::PICC_TYPE_MIFARE_4K) {
    success = authenticateAndRead(slot, info);
  } else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL || piccType == MFRC522::PICC_TYPE_MIFARE_UL) {
    // NTAG/Ultralight fallback — try reading without auth
    success = readNtagPages(slot, info);
  } else {
    Serial.printf("Slot %d: unsupported tag type\n", slot);
  }

  strncpy(info.uid, uidStr, sizeof(info.uid) - 1);

  if (!success) {
    info.present = true;
    strcpy(info.materialType, "Unknown tag");
    info.tagReadSuccess = false;
  }

  reader->PICC_HaltA();

  return true;
}

bool RfidManager::authenticateAndRead(uint8_t slot, SpoolInfo &info) {
  MFRC522* reader = mfrc522[slot];
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  const uint8_t bufferSize = 1024;
  uint8_t* dataBuffer = new uint8_t[bufferSize];
  if (!dataBuffer) return false;
  uint16_t bytesRead = 0;

  for (uint8_t sector = 0; sector < 16 && bytesRead < bufferSize; sector++) {
    uint8_t blockAddr = sector * 4;

    MFRC522::StatusCode authStatus = reader->PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &key, &(reader->uid));
    if (authStatus != MFRC522::STATUS_OK) {
      if (bytesRead > 0) break;
      continue;
    }

    uint8_t startBlock = (sector == 0) ? 1 : blockAddr;
    uint8_t endBlock = blockAddr + 3;

    for (uint8_t block = startBlock; block < endBlock && bytesRead < bufferSize; block++) {
      byte readBuf[18];
      byte readSize = sizeof(readBuf);
      MFRC522::StatusCode status = reader->MIFARE_Read(block, readBuf, &readSize);
      if (status != MFRC522::STATUS_OK) break;

      for (byte b = 0; b < 16 && bytesRead < bufferSize; b++) {
        dataBuffer[bytesRead++] = readBuf[b];
      }
    }
  }

  reader->PCD_StopCrypto1();

  Serial.printf("Slot %d: MIFARE auth done, bytesRead=%d\n", slot, bytesRead);

  bool result = TagParser::parse(dataBuffer, bytesRead, info.uid, info);
  Serial.printf("Slot %d: parse result=%s\n", slot, result ? "OK" : "FAIL");

  if (!result) {
    info.present = true;
    info.tagReadSuccess = false;
  }

  delete[] dataBuffer;
  return result;
}

bool RfidManager::readNtagPages(uint8_t slot, SpoolInfo &info) {
  MFRC522* reader = mfrc522[slot];

  uint16_t bufferSize = 900;
  uint8_t* dataBuffer = new uint8_t[bufferSize];
  if (!dataBuffer) return false;

  uint16_t bytesRead = 0;

  for (uint8_t page = 4; page < 230 && bytesRead < bufferSize; page++) {
    byte readBuf[18];
    byte readSize = sizeof(readBuf);
    MFRC522::StatusCode status = reader->MIFARE_Read(page, readBuf, &readSize);
    if (status != MFRC522::STATUS_OK) break;

    byte bytesThisBlock = (readSize > 16) ? 16 : readSize;

    for (byte b = 0; b < bytesThisBlock && bytesRead < bufferSize; b++) {
      dataBuffer[bytesRead++] = readBuf[b];
    }
  }

  Serial.printf("Slot %d: NTAG read, bytesRead=%d\n", slot, bytesRead);

  bool result = TagParser::parse(dataBuffer, bytesRead, info.uid, info);
  Serial.printf("Slot %d: parse result=%s\n", slot, result ? "OK" : "FAIL");

  delete[] dataBuffer;
  return result;
}

bool RfidManager::isTagPresent(uint8_t slot) {
  if (slot >= NUM_SLOTS) return false;
  return spoolData[slot].present;
}

bool RfidManager::getSpoolInfo(uint8_t slot, SpoolInfo &info) {
  if (slot >= NUM_SLOTS) return false;
  info = spoolData[slot];
  return spoolData[slot].present;
}

void RfidManager::forceRescan(uint8_t slot) {
  if (slot >= NUM_SLOTS) return;
  lastPoll[slot] = 0;
}

void RfidManager::selectReader(uint8_t slot) {
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    digitalWrite(SS_PINS[i], HIGH);
  }
  delayMicroseconds(50);
  if (slot < NUM_SLOTS) {
    digitalWrite(SS_PINS[slot], LOW);
  }
}

void RfidManager::deselectAll() {
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    digitalWrite(SS_PINS[i], HIGH);
  }
}
