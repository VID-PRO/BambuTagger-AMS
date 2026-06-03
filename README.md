# <img alt="logo" src="Logo/bambutagger.png" height="36" /> BambuTagger-AMS

Multi-spool NFC tag reader for Bambu Lab printers. Reads 4 tags via RC522, syncs AMS tray data over MQTT, and sends filament data to the printer/BMCU. Web interface + OLED + WS2812 LEDs + BME280.

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/G8M220JASY)

<p align="center">
<img src="Pics/printer.png" />
<img src="Pics/status.png" />
<img src="Pics/pcb.png" />
</p>

---

## Features

| Category | Details |
|----------|---------|
| **RFID readers** | 4× RC522 on shared SPI, MIFARE Classic 1K + NTAG |
| **Tag formats** | Bambu Lab, TigerTag, SpoolEase, OpenSpool, OpenTag3D — auto-detect |
| **Key derivation** | HKDF-SHA256 with Bambu Lab salt — no hardcoded keys |
| **Web interface** | 3-tab SPA: Status (merged slots + swatches), Printer Config, WiFi Config |
| **OLED** | 128×64 SSD1306: splash, AMS tray data, big BME280 temp/humidity, OTA progress bar |
| **LEDs** | 4× WS2812: per-slot color from printer AMS tray data (live MQTT) |
| **BME280** | Temperature + humidity on I2C shared bus, displayed on OLED + web |
| **MQTT bridge** | Subscribe printer status, publish `ams_filament_setting` to BMCU |
| **WiFi** | STA with AP fallback + captive portal on `192.168.4.1` |
| **OTA updates** | One-click from GitHub Releases, OLED progress bar, web overlay |
| **CI/CD** | GitHub Actions: build on push, merged + OTA binaries on release tags |

---

## Hardware

### Bill of Materials

| Component | Notes | Buy |
|-----------|-------|-----|
| **ESP32** Dev Module | Standard ESP32-WROOM | https://de.aliexpress.com/item/1005006589341221.html |
| **4× RC522** | SPI, shared bus | https://de.aliexpress.com/item/1005006233005745.html |
| **4× WS2812** | Daisy-chained, single data pin | https://de.aliexpress.com/item/32560280169.html |
| **128×64 OLED** | SSD1306, I2C | https://de.aliexpress.com/item/1005007551771400.html |
| **BME280** | Temp/humidity, I2C (shared bus with OLED) | https://de.aliexpress.com/item/1005006824236173.html |
| **PCB** | DIY PCB from JLPCB | https://oshwlab.com/bambutagger/project_hdkkdlsn |

### Pin Assignments

| Component | ESP32 Pin |
|-----------|-----------|
| **SPI MOSI** | GPIO 23 |
| **SPI MISO** | GPIO 19 |
| **SPI SCK** | GPIO 18 |
| RC522 #1 SS | GPIO 13 |
| RC522 #2 SS | GPIO 12 |
| RC522 #3 SS | GPIO 14 |
| RC522 #4 SS | GPIO 27 |
| RC522 #1 RST | GPIO 26 |
| RC522 #2 RST | GPIO 25 |
| RC522 #3 RST | GPIO 33 |
| RC522 #4 RST | GPIO 32 |
| **WS2812 data** | GPIO 15 |
| **OLED SDA** | GPIO 21 |
| **OLED SCL** | GPIO 22 |

### Slot Mapping

| RC522 # | SS | Slot | WS2812 Pixel |
|---------|----|------|--------------|
| 1 | 13 | 1 | 0 |
| 2 | 12 | 2 | 1 |
| 3 | 14 | 3 | 2 |
| 4 | 27 | 4 | 3 |

---

## Software

### Required Libraries (Arduino Library Manager)

| Library | Notes |
|---------|-------|
| `MFRC522-spi-i2c-uart-async` | Multi-reader SPI sharing (not standard MFRC522) |
| `Adafruit NeoPixel` | WS2812 LEDs |
| `Adafruit GFX Library` | Graphics |
| `Adafruit SSD1306` | OLED |
| `Adafruit BME280 Library` | Temp/humidity sensor |
| `PubSubClient` | MQTT |
| `ArduinoJson` | v6.x or v7.x |
| `mbedTLS` | Bundled with ESP32 core, HKDF-SHA256 |

### Board Settings (Arduino IDE)

| Setting | Value |
|---------|-------|
| Board | **ESP32 Dev Module** |
| Partition Scheme | **Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)** |
| Upload Speed | 921600 |
| Monitor Speed | 115200 |

---

## Tag Formats

| Format | Chip | Method | Display |
|--------|------|--------|---------|
| **Bambu Lab** | MIFARE Classic 1K | HKDF keys, fixed blocks | `Tag: Bambu - PLA · C12E1FFF · 1000g/1000g` |
| **TigerTag** | NTAG | Binary v2.1 protocol | `Tag: TigerTag - ASA-AF · F078B4FF · 1000g/1000g` |
| **SpoolEase** | NTAG | NDEF URI + URL params | `Tag: SpoolEase - PLA · 000000FF · 1000g/1036g` |
| **OpenSpool** | NTAG | NDEF JSON | `Tag: OpenSpool - ASA-AF · F078B4FF · 1000g/1000g` |
| **OpenTag3D** | NTAG | MIME binary (application/opentag3d) | `Tag: OpenTag3D - ASA-AF · F078B4FF · 1000g/1000g` |

### Bambu Lab Blocks

| Block | Content |
|-------|---------|
| 0 | UID (4 bytes) |
| 1 | Variant ID + Material index (e.g. GFA00) |
| 4 | Detailed type (e.g. PLA Basic) |
| 5 | RGBA color + spool weight (LE) |
| 6 | Nozzle temps (LE) |

### Filament Prefix → Type

| Prefix | Type |
|--------|------|
| GFA–GFE, GFL | PLA |
| GFG | PETG |
| GFH, GFI | ABS |
| GFJ | ASA |
| GFK | TPU |

---

## Web Interface

| Tab | Features |
|-----|----------|
| **Status** | Merged slots (AMS + scanned tag), color swatches (36×36px), status bar (WiFi/MQTT/Printer/BME280), Printer AMS Cards (all units, FW, serial, temp/humidity), Sync / Scan / Send / OTA buttons |
| **Printer Config** | Printer IP, port, access code, serial, AMS Unit selector, MQTT TLS toggle, AMS detection status |
| **WiFi Config** | SSID, password, device name |

### Sticky Footer

`© 2026 by VID-PRO` — linked to [www.vid-pro.de](https://www.vid-pro.de)

---

## OLED Display (128×64)

```
┌──────────────────────────────────┐
│ Device Name                WiFi │
├──────────────────────────────────┤
│ 1: PLA   #C0C0C0FF              │  ← AMS tray data, 1px gap between rows
│ 2: empty                        │
│ 3: empty                        │
│ 4: empty                        │
├──────────────────────────────────┤
│ 22C 45%                   PTR:OK│  ← big BME280 (size 2) + PTR status
└──────────────────────────────────┘
```

- Footer alternates: BME280 temp/humidity when present, MQTT status as fallback
- OTA progress: header/footer preserved, 200ms progress bar updates

---

## MQTT Protocol

| Direction | Topic | Commands |
|-----------|-------|----------|
| Subscribe | `device/<serial>/report` | `push_status`, `get_version`, `pushall` responses |
| Publish | `device/<serial>/request` | `ams_filament_setting`, `get_version`, `pushall`, `ams_user_setting` (BME280) |

### `ams_filament_setting` Payload

```json
{
  "print": {
    "sequence_id": "0",
    "command": "ams_filament_setting",
    "ams_id": 0,
    "tray_id": 0,
    "tray_info_idx": "GFA00",
    "tray_color": "C12E1FFF",
    "nozzle_temp_min": 190,
    "nozzle_temp_max": 230,
    "tray_type": "PLA"
  }
}
```

- `tray_type`: derived from filament prefix (GFA→PLA…) or material name for non-Bambu tags
- `tray_color`: RRGGBBFF format

---

## OTA Updates

| Feature | Detail |
|---------|--------|
| Button | "Update Firmware" on Status page (shows version info) |
| Endpoints | `POST /api/ota`, `GET /api/ota-check`, `GET /api/version` |
| Download | Latest `.ino.bin` from GitHub Releases |
| Retries | 3 attempts, fresh HTTP client per attempt |
| Progress | OLED: "Checking…" → "Downloading…" → "Flashing… 45%", web overlay with spinner + bar |
| Reboot | Auto after `Update.end(true)`, web UI auto-reloads |

---

## CI / CD

Workflow at `GHActions/release.yml`:
- **On push/PR**: compiles sketch, uploads artifacts
- **On release tag**: merged flash binary + OTA binary, attached to GitHub Release
- Pinned core: `esp32:esp32@3.0.7`, Arduino cache

---

## Configuration Defaults

| Setting | Default |
|---------|---------|
| WiFi SSID | (empty) |
| WiFi Password | (empty) |
| Device Name | BambuTagger-AMS |
| Printer IP | 192.168.1.100 |
| Printer Port | 8883 |
| Access Code | (empty) |
| Printer Serial | (empty) |
| AMS Unit | 0 (A) |
| MQTT Enabled | Yes |
| MQTT TLS | No |
| MQTT Update Interval | 3000 ms |
| RFID Poll Interval | 100 ms |
| Firmware Version | 1.0.8 |

## License

MIT
