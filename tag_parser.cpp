#include "tag_parser.h"

void TagParser::clear(SpoolInfo &info) {
  info.present = false;
  info.uid[0] = '\0';
  info.materialType[0] = '\0';
  info.detailedType[0] = '\0';
  info.color[0] = '\0';
  info.colorHex[0] = '\0';
  info.remainingGrams = 0;
  info.totalGrams = 0;
  info.nozzleTempMin = 0;
  info.nozzleTempMax = 0;
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

  // Try TigerTag binary format first (NTAG pages 4+)
  if (parseTigerTag(data, length, uid, info)) return true;

  if (data[0] == BAMBU_TAG_MAGIC && data[1] == BAMBU_TAG_VERSION) {
    return parseBambuTLV(data + BAMBU_HEADER_SIZE, length - BAMBU_HEADER_SIZE, info);
  }

  return parseRawNTAG(data, length, uid, info);
}

static uint32_t readU32BE(const uint8_t* d, int off) {
  return ((uint32_t)d[off] << 24) | ((uint32_t)d[off+1] << 16) | ((uint32_t)d[off+2] << 8) | d[off+3];
}
static uint16_t readU16BE(const uint8_t* d, int off) {
  return ((uint16_t)d[off] << 8) | d[off+1];
}

bool TagParser::parseTigerTag(uint8_t* data, uint16_t length, const char* uid, SpoolInfo &info) {
  if (length < 48) return false; // need at least header fields

  uint32_t magic = readU32BE(data, 0);
  if (magic != 0x5BF59264 && magic != 0xBC0FCB97 && magic != 0x6C41A2E1) return false;

  // ID TigerTag → detect type
  const char* ttLabel = "TigerTag";
  if (magic == 0xBC0FCB97) ttLabel = "TigerTag+";
  else if (magic == 0x6C41A2E1) ttLabel = "TigerTag Init";

  // Color 1 RGBA at offset +16
  snprintf(info.colorHex, sizeof(info.colorHex), "%02X%02X%02X%02X",
           data[16], data[17], data[18], data[19]);

  // Material ID at offset +8 (u16 BE) — map known IDs to names
  uint16_t matId = readU16BE(data, 8);
  const char* matName = nullptr;
  switch (matId) {
    case 38219: matName = "PLA"; break;
    case 24629: matName = "PLA HS"; break;
    case 46591: matName = "PLA+"; break;
    case 10602: matName = "PLA Silk"; break;
    case 8345:  matName = "PLA+Silk"; break;
    case 48310: matName = "PLA-CF"; break;
    case 9456:  matName = "PLA Marble"; break;
    case 48001: matName = "PLA Wood"; break;
    case 38256: matName = "PETG"; break;
    case 57469: matName = "PETG HF"; break;
    case 7649:  matName = "PETG HS"; break;
    case 55418: matName = "PETG-CF"; break;
    case 34944: matName = "PETG-GF"; break;
    case 20562: matName = "ABS"; break;
    case 49074: matName = "ABS-GF"; break;
    case 425:   matName = "ABS-CF"; break;
    case 43518: matName = "TPU"; break;
    case 48047: matName = "TPU HS"; break;
    case 12844: matName = "ASA"; break;
    case 12878: matName = "CoPE"; break;
    case 30458: matName = "PC"; break;
    case 59328: matName = "PA"; break;
    case 39944: matName = "PA-CF"; break;
    case 30594: matName = "PA-GF"; break;
    case 30884: matName = "PP"; break;
    case 50497: matName = "PP-CF"; break;
    case 42962: matName = "PP-GF"; break;
    case 9483:  matName = "PVA"; break;
    case 34049: matName = "BVOH"; break;
    case 26029: matName = "HIPS"; break;
    case 3368:  matName = "PC-ABS"; break;
    case 15041: matName = "PCTG"; break;
    case 11053: matName = "PET-CF"; break;
    case 9691:  matName = "EVA"; break;
    default: break;
  }
  if (matName)
    snprintf(info.materialType, sizeof(info.materialType), "%s", matName);
  else
    snprintf(info.materialType, sizeof(info.materialType), "%d", matId);

  // Brand ID at offset +14 (u16 BE) — store in manufacturer
  uint16_t brandId = readU16BE(data, 14);
  snprintf(info.manufacturer, sizeof(info.manufacturer), "%d", brandId);

  // Detailed type = tag type label
  snprintf(info.detailedType, sizeof(info.detailedType), "%s", ttLabel);

  // Measure at offset +20 (u24 BE)
  info.totalGrams = ((uint32_t)data[20] << 16) | ((uint32_t)data[21] << 8) | data[22];

  // Measure Available at offset +76 (u24 BE)
  if (length >= 80) {
    info.remainingGrams = ((uint32_t)data[76] << 16) | ((uint32_t)data[77] << 8) | data[78];
  } else {
    info.remainingGrams = info.totalGrams;
  }

  // Nozzle temps at offset +24, +26 (u16 BE)
  info.nozzleTempMin = readU16BE(data, 24);
  info.nozzleTempMax = readU16BE(data, 26);

  // Custom message at offset +48 (28 bytes UTF-8) — stored in manufacturer append
  if (length >= 76) {
    char msg[29];
    memcpy(msg, data + 48, 28);
    msg[28] = '\0';
    for (int i = 27; i >= 0; i--) { if (msg[i] == ' ' || msg[i] == '\0') msg[i] = '\0'; else break; }
    if (msg[0]) { strncat(info.manufacturer, " ", sizeof(info.manufacturer) - strlen(info.manufacturer) - 1); strncat(info.manufacturer, msg, sizeof(info.manufacturer) - strlen(info.manufacturer) - 1); }
  }

  info.tagReadSuccess = true;
  return true;
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
      int ndefEnd = pos + ndefLen;  // absolute end of NDEF message

      if (ndefEnd > length) break;

      if (pos + 2 <= length) {
        uint8_t ndefFlags = data[pos];
        uint8_t typeLen = data[pos + 1];
        uint8_t payloadLen = data[pos + 2];
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
        if (typeLen == 1 && pos + typeLen <= length &&
            data[pos] == 'U') {
          pos += typeLen;
            if (payloadLen >= 1) {
            uint8_t uriCode = data[pos];
            pos++;
            int uriLen = payloadLen - 1;
            if (uriLen < 0) uriLen = 0;
            int remain = length - pos;
            // Serial.printf("NDEF URI raw: ...\n");
            if (uriLen > remain) uriLen = remain;
            static const char* PREFIXES[] = {"","http://www.","https://www.","http://","https://"};
            const char* prefix = (uriCode < 5) ? PREFIXES[uriCode] : "";
            strcpy(info.materialType, "SpoolEase");
            int preLen = strlen(prefix);
            if (preLen > 0 && preLen < (int)(sizeof(info.detailedType) - 1))
              memcpy(info.detailedType, prefix, preLen);
            int outPos = preLen;
            // Copy URI bytes, clamped to NDEF message boundary
            for (int i = 0; i < uriLen && pos + i < ndefEnd && outPos < (int)(sizeof(info.detailedType) - 1); i++) {
              info.detailedType[outPos++] = data[pos + i];
            }
            info.detailedType[outPos] = '\0';

            // Detect tag type from URL domain
            const char* tagLabel = "SpoolTag";
            bool isSpoolEase = (strstr(info.detailedType, "tag.spoolease.io") != nullptr);
            if (isSpoolEase) tagLabel = "SpoolEase";
            else if (strstr(info.detailedType, "tigertag")) tagLabel = "TigerTag";
            else if (strstr(info.detailedType, "openspooltag")) tagLabel = "OpenSpoolTag";

            // Only parse URL params for SpoolEase format
            if (isSpoolEase) {
            // Parse SpoolEase URL parameters: M=type CC=color SC=material etc.
            char seType[16] = "";
            char seMat[16] = "";
            int seWE = 0, seWF = 0;
            Serial.printf("SpoolEase URL: %s\n", info.detailedType);
            for (const char* p = info.detailedType; *p; p++) {
              if (*p == '&' || *p == '?' || p == info.detailedType) {
                if (p != info.detailedType) p++; // skip & or ?
                if (strncmp(p, "M=", 2) == 0) { p += 2;
                  int n = 0; while (p[n] && p[n] != '&') n++;
                  if (n > 0 && n < (int)sizeof(seType)) { memcpy(seType, p, n); seType[n] = '\0'; }
                  p += n - 1;
                } else if (strncmp(p, "CC=", 3) == 0) { p += 3;
                  int n = 0; while (p[n] && p[n] != '&') n++;
                  if (n > 0 && n <= (int)(sizeof(info.colorHex) - 1))
                    { memcpy(info.colorHex, p, n); info.colorHex[n] = '\0'; }
                  p += n - 1;
                } else if (strncmp(p, "SC=", 3) == 0) { p += 3;
                  int n = 0; while (p[n] && p[n] != '&') n++;
                  if (n > 0 && n < (int)(sizeof(info.materialType) - 1))
                    { memcpy(info.materialType, p, n); info.materialType[n] = '\0'; }
                  p += n - 1;
                } else if (strncmp(p, "B=", 2) == 0) { p += 2;
                  int n = 0; while (p[n] && p[n] != '&') n++;
                  if (n > 0 && n < (int)(sizeof(info.manufacturer) - 1))
                    { memcpy(info.manufacturer, p, n); info.manufacturer[n] = '\0'; }
                  p += n - 1;
                } else if (strncmp(p, "WE=", 3) == 0) { p += 3;
                  seWE = atoi(p);
                  while (*p && *p != '&') p++; p--;
                } else if (strncmp(p, "WL=", 3) == 0) { p += 3;
                  info.remainingGrams = atoi(p);
                  while (*p && *p != '&') p++; p--;
                } else if (strncmp(p, "WF=", 3) == 0) { p += 3;
                  seWF = atoi(p);
                  while (*p && *p != '&') p++; p--;
                } else if (strncmp(p, "NN=", 3) == 0) { p += 3;
                  info.nozzleTempMin = atoi(p);
                  while (*p && *p != '&') p++;
                  p--;
                } else if (strncmp(p, "NX=", 3) == 0) { p += 3;
                  info.nozzleTempMax = atoi(p);
                  while (*p && *p != '&') p++;
                  p--;
                }
              }
            }
            info.totalGrams = seWF - seWE;
            if (info.totalGrams < 0) info.totalGrams = info.remainingGrams;
            Serial.printf("SpoolEase parsed: M=%s SC=%s color=%s w=%d/%d\n",
                          seType, info.materialType, info.colorHex,
                          info.remainingGrams, info.totalGrams);
            snprintf(info.detailedType, sizeof(info.detailedType), "%s", seType[0] ? seType : tagLabel);
            // Show SpoolEase as tag type, M= as material
            strncpy(info.detailedType, "SpoolEase", sizeof(info.detailedType) - 1);
            if (seType[0]) strncpy(info.materialType, seType, sizeof(info.materialType) - 1);
            } else {
              strcpy(info.materialType, tagLabel);
              snprintf(info.detailedType, sizeof(info.detailedType), "%s", tagLabel);
            }
            info.tagReadSuccess = true;
            return true;
          }
        }
      }
      // NDEF found but record type not recognized — still a valid tag
      strcpy(info.materialType, "SpoolTag");
      info.tagReadSuccess = true;
      return true;
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
