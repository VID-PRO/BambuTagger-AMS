#include "tag_parser.h"

void TagParser::clear(SpoolInfo &info) {
  info.present = false;
  info.uid[0] = '\0';
  info.materialType[0] = '\0';
  info.color[0] = '\0';
  info.colorHex[0] = '\0';
  info.remainingGrams = 0;
  info.totalGrams = 0;
  info.batchNumber[0] = '\0';
  info.manufacturer[0] = '\0';
  info.tagReadSuccess = false;
}

bool TagParser::parse(uint8_t* data, uint16_t length, const char* uid, SpoolInfo &info) {
  info.present = true;
  info.tagReadSuccess = false;
  strncpy(info.uid, uid, sizeof(info.uid) - 1);

  if (length < BAMBU_HEADER_SIZE) {
    strcpy(info.materialType, "Data too short");
    return false;
  }

  if (data[0] == BAMBU_TAG_MAGIC && data[1] == BAMBU_TAG_VERSION) {
    return parseBambuTLV(data + BAMBU_HEADER_SIZE, length - BAMBU_HEADER_SIZE, info);
  }

  return parseRawNTAG(data, length, uid, info);
}

bool TagParser::parseBambuTLV(uint8_t* data, uint16_t length, SpoolInfo &info) {
  uint16_t pos = 0;

  while (pos + 2 <= length) {
    uint8_t type = data[pos];
    uint8_t len = data[pos + 1];
    pos += 2;

    if (pos + len > length) break;

    switch (type) {
      case BAMBU_TLV_TYPE_MATERIAL:
        if (len >= 1) {
          strncpy(info.materialType, materialName(data[pos]), sizeof(info.materialType) - 1);
        }
        break;

      case BAMBU_TLV_TYPE_COLOR:
        if (len >= 4) {
          bytesToHex(&data[pos], 3, info.colorHex);
          snprintf(info.color, sizeof(info.color), "#%s", info.colorHex);
        } else if (len >= 3) {
          bytesToHex(&data[pos], 3, info.colorHex);
          snprintf(info.color, sizeof(info.color), "#%s", info.colorHex);
        }
        break;

      case BAMBU_TLV_TYPE_WEIGHT:
        if (len >= 4) {
          info.remainingGrams = (data[pos] << 8) | data[pos + 1];
          info.totalGrams = (data[pos + 2] << 8) | data[pos + 3];
        } else if (len >= 2) {
          info.remainingGrams = (data[pos] << 8) | data[pos + 1];
        }
        break;

      case BAMBU_TLV_TYPE_BATCH:
        if (len > 0) {
          uint8_t copyLen = len < (sizeof(info.batchNumber) - 1) ? len : (sizeof(info.batchNumber) - 1);
          memcpy(info.batchNumber, &data[pos], copyLen);
          info.batchNumber[copyLen] = '\0';
        }
        break;

      case BAMBU_TLV_TYPE_MFG:
        if (len > 0) {
          uint8_t copyLen = len < (sizeof(info.manufacturer) - 1) ? len : (sizeof(info.manufacturer) - 1);
          memcpy(info.manufacturer, &data[pos], copyLen);
          info.manufacturer[copyLen] = '\0';
        }
        break;

      default:
        break;
    }
    pos += len;
  }

  info.tagReadSuccess = (info.materialType[0] != '\0');
  return info.tagReadSuccess;
}

bool TagParser::parseRawNTAG(uint8_t* data, uint16_t length, const char* uid, SpoolInfo &info) {
  uint16_t pos = 0;

  while (pos + 2 <= length) {
    uint8_t tlvType = data[pos];
    pos++;

    if (tlvType == 0x00 || tlvType == 0xFE) continue;
    if (tlvType == 0x03) {
      if (pos >= length) break;
      uint8_t ndefLen = data[pos];
      pos++;

      if (pos + ndefLen > length) break;

      if (pos + 2 <= length) {
        uint8_t ndefFlags = data[pos];
        uint8_t typeLen = data[pos + 1];
        uint8_t payloadLen = (pos + 2 < length) ? data[pos + 2] : 0;
        pos += 3;

        if (typeLen == 2 && pos + typeLen <= length &&
            data[pos] == 'T' && data[pos + 1] == 'a') {
          pos += typeLen + 1;

          if (payloadLen > 0 && pos + payloadLen <= length) {
            uint8_t copyLen = payloadLen < (sizeof(info.materialType) - 1) ? payloadLen : (sizeof(info.materialType) - 1);
            memcpy(info.materialType, &data[pos], copyLen);
            info.materialType[copyLen] = '\0';
            info.tagReadSuccess = true;
            return true;
          }
        }
      }
      break;
    }

    if (tlvType == 0x01 || tlvType == 0x02) {
      if (pos >= length) break;
      pos += data[pos] + 1;
    } else if (tlvType == 0xFD) {
      break;
    } else {
      if (pos >= length) break;
      pos += data[pos] + 1;
      break;
    }
  }

  if (!info.tagReadSuccess) {
    strcpy(info.materialType, "Unknown format");
    uint8_t copyLen = length < 8 ? length : 8;
    bytesToHex(data, copyLen, info.colorHex);
  }

  return info.tagReadSuccess;
}

const char* TagParser::materialName(uint8_t id) {
  for (size_t i = 0; i < sizeof(MATERIAL_TABLE) / sizeof(MATERIAL_TABLE[0]); i++) {
    if (MATERIAL_TABLE[i].id == id) return MATERIAL_TABLE[i].name;
  }
  return "Unknown Material";
}

void TagParser::bytesToHex(uint8_t* bytes, uint8_t len, char* out) {
  for (uint8_t i = 0; i < len && i < 7; i++) {
    snprintf(out + (i * 2), 3, "%02X", bytes[i]);
  }
}

void TagParser::dumpHex(uint8_t* data, uint16_t length, char* out, uint16_t outLen) {
  uint16_t pos = 0;
  for (uint16_t i = 0; i < length && pos < outLen - 4; i++) {
    pos += snprintf(out + pos, outLen - pos, "%02X ", data[i]);
    if ((i + 1) % 16 == 0 && pos < outLen - 2) {
      out[pos++] = '\n';
    }
  }
  out[pos] = '\0';
}
