#include "rfid_manager.h"
#include "tag_parser.h"

void RfidManager::begin() {
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  SPI.setClockDivider(SPI_CLOCK_DIV16);

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

  char uidStr[16];
  snprintf(uidStr, sizeof(uidStr), "%02X%02X%02X%02X%02X%02X%02X",
           reader->uid.uidByte[0], reader->uid.uidByte[1],
           reader->uid.uidByte[2], reader->uid.uidByte[3],
           reader->uid.uidByte[4], reader->uid.uidByte[5],
           reader->uid.uidByte[6]);

  bool success = authenticateAndRead(slot, info);
  strncpy(info.uid, uidStr, sizeof(info.uid) - 1);

  if (!success) {
    info.present = true;
    strcpy(info.materialType, "Unknown tag");
    info.tagReadSuccess = false;
  }

  reader->PICC_HaltA();
  reader->PCD_StopCrypto1();

  return true;
}

bool RfidManager::authenticateAndRead(uint8_t slot, SpoolInfo &info) {
  MFRC522* reader = mfrc522[slot];

  uint16_t totalPages = NTAG_MAX_PAGES;

  byte versionBuf[4];
  byte size = 4;
  if (reader->MIFARE_Read(NTAG_USER_START_PAGE - 1, versionBuf, &size) == MFRC522::STATUS_OK && size >= 4) {
    uint8_t containerSize = versionBuf[2];
    if (containerSize > 0) {
      totalPages = ((uint16_t)containerSize * 8) / NTAG_PAGE_SIZE;
    }
  }

  uint16_t bufferSize = (totalPages - NTAG_USER_START_PAGE) * NTAG_PAGE_SIZE;
  if (bufferSize > 4096) bufferSize = 4096;
  uint8_t* dataBuffer = new uint8_t[bufferSize];
  if (!dataBuffer) return false;

  uint16_t bytesRead = 0;

  for (uint8_t page = NTAG_USER_START_PAGE; page < totalPages; page += 4) {
    byte readBuf[18];
    byte readSize = sizeof(readBuf);
    MFRC522::StatusCode status = reader->MIFARE_Read(page, readBuf, &readSize);
    if (status != MFRC522::STATUS_OK) break;

    byte bytesThisBlock = readSize;
    if (bytesThisBlock > 16) bytesThisBlock = 16;

    for (byte b = 0; b < bytesThisBlock && bytesRead < bufferSize; b++) {
      dataBuffer[bytesRead++] = readBuf[b];
    }

    if (bytesThisBlock < 16) break;
  }

  bool result = TagParser::parse(dataBuffer, bytesRead, info.uid, info);
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
  deselectAll();
  if (slot < NUM_SLOTS) {
    digitalWrite(SS_PINS[NUM_SLOTS - 1], HIGH);
    delayMicroseconds(10);
    digitalWrite(SS_PINS[slot], LOW);
  }
}

void RfidManager::deselectAll() {
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    digitalWrite(SS_PINS[i], HIGH);
  }
}
