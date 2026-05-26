# <img alt="logo" src="Logo/bambutagger.png" height="36" />  BambuTagger-AMS

Multi-spool NFC tag reader for Bambu Lab printers. Reads 4 Bambu Lab filament spool tags via RC522, displays live printer AMS tray data over MQTT, and sends RFID tag data back to the printer. Fully configurable via web interface with automatic AP fallback.

<img alt="atatus" src="Pics/status.png" width="800" />    
<img alt="printer" src="Pics/printer.png" width="800" />
    
## Features

- **4x RC522** on shared SPI bus polling MIFARE Classic 1K spool tags
- **Live printer AMS sync** — reads tray data (material, color, type) from the printer over MQTT
- **Web interface** — 3-tab SPA: Status (printer slots + tag slots), Printer Config, WiFi Config
- **OLED display** — shows live AMS tray data or RFID tag data with MQTT/PTR status
- **4x WS2812 LEDs** — per-slot color display from tag `colorHex`
- **MQTT bridge** — subscribes to printer status, publishes `ams_filament_setting` commands
- **Auto AP fallback** — captive portal on `192.168.4.1` when WiFi is unavailable

## Hardware

- **ESP32** Dev Module
- **4x RC522** RFID/NFC readers (SPI, shared bus)
- **4x WS2812** addressable LEDs (daisy-chained, single data pin)
- **128x64 OLED** SSD1306 I2C display

### Wiring

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

All RC522 share the same SPI bus (MOSI, MISO, SCK). Each has its own SS and RST pin.

WS2812 LEDs connect in daisy-chain: `ESP32 GPIO 15 → LED #1 DIN → LED #1 DOUT → LED #2 DIN → ... → LED #4`.

### Slot Mapping

| RC522 # | SS Pin | Slot | WS2812 Pixel |
|---------|--------|------|--------------|
| 1 | 13 | 1 | 0 |
| 2 | 12 | 2 | 1 |
| 3 | 14 | 3 | 2 |
| 4 | 27 | 4 | 3 |

Each WS2812 LED displays the actual filament color read from the tag in its corresponding slot.

## Software Setup (Arduino IDE)

1. Install **ESP32 board package**:  
   File → Preferences → Additional Board Manager URLs:  
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`  
   Then Tools → Board → Boards Manager → search "ESP32" → install.

2. Install required libraries via **Tools → Manage Libraries**:
   - **MFRC522** by Miguel Balboa (v1.4+)
   - **Adafruit NeoPixel** by Adafruit (v1.12+)
   - **Adafruit GFX Library** by Adafruit (v1.11+)
   - **Adafruit SSD1306** by Adafruit (v2.5+)
   - **PubSubClient** by Nick O'Leary (v2.8+)
   - **ArduinoJson** by Benoit Blanchon (v6.x or v7.x)

3. Open `BambuTagger-AMS.ino`, select **ESP32 Dev Module** as board, and upload.

## WiFi & AP Mode

On first boot (or if saved WiFi credentials are invalid), the device automatically opens an access point:

| Scenario | Behavior |
|----------|----------|
| No WiFi configured | Opens AP immediately |
| WiFi connection fails | Opens AP after 15 seconds |
| AP active, credentials exist | Retries STA connection every 30 seconds |
| STA connects while AP active | Closes AP, switches to normal mode |

- **SSID**: Device name (default: `BambuTagger-AMS`)
- **Security**: Open (no password)
- **IP**: `192.168.4.1`
- **Captive portal**: DNS redirects all domains to the config page

Connect to the AP with any phone or laptop, visit `http://192.168.4.1`, enter WiFi credentials, and save. The device connects and AP mode closes automatically.

## Web Interface

Available at `http://<esp32-ip>` on your network, or `http://192.168.4.1` in AP mode.

### Status Tab
- **Printer AMS Slots** — live tray data from the configured AMS unit over MQTT (type, material, color, color swatch)
- **RFID Tag Slots** — scanned tag data (UID, material, color, filament weight) with color swatches
- **Status bar** — WiFi, MQTT, and Printer connection status indicators
- **Printer AMS Cards** — full AMS unit info (all detected units with tray grids, FW version, serial number)
- **Actions** — Scan All Slots, Send to Printer, Sync From Printer

### Printer Config Tab
- Printer IP, Port (default 8883), Access Code, Serial Number
- **AMS Unit selector** (A/B/C/D) — selects which AMS unit to display in Slot Status
- Printer AMS detection status with green/red indicators
- MQTT enable/disable toggle and update interval

### WiFi Config Tab
- SSID, Password, Device Name
- Settings persist in NVS flash and survive reboots
- Auto-reboot after saving configuration

## OLED Display (128x64)

```
┌──────────────────────────────────┐
│ Device Name                WiFi │  ← status bar (white on black)
├──────────────────────────────────┤
│ 1: PLA   #C0C0C0FF              │  ← AMS tray data (live from printer)
│ 2: empty                        │
│ 3: empty                        │
│ 4: empty                        │
├──────────────────────────────────┤
│ MQTT:OK                   PTR:OK│  ← footer (MQTT + Printer status)
└──────────────────────────────────┘
```

- **MQTT connected + AMS detected**: shows printer tray data (type + color hex) for the configured AMS unit
- **No MQTT / AMS not detected**: falls back to RFID tag reader data
- **Status bar**: device name left, WiFi status right-aligned
- **Footer**: MQTT status left, Printer status right-aligned

## Printer Communication

Connects to the Bambu Lab printer over MQTT (unsecure, port 8883) to:

### Subscribe
- **Topic**: `device/<serial>/report`
- **Data received**: `push_status` (periodic, ~3KB status messages) and `get_version` responses
- **Tray data**: parsed from `print.ams.ams[]` — tray type (PLA/PETG/ABS/etc.), material code, color (RGBA hex)
- **AMS detection**: parsed from `info.module[]` in `get_version` — module names like `n3f/0`, `n3f/1`

### Publish
- **Topic**: `device/<serial>/request`
- **`get_version`** — discovers AMS units connected to the printer
- **`pushall`** — requests full printer status including tray data
- **`ams_filament_setting`** — sends material type, color, remaining grams, and tag UID for each slot

### AMS Unit Selection
Use the AMS Unit dropdown in Printer Config to select which physical AMS unit (A/B/C/D) to display in the Status tab and OLED. The web interface shows all detected AMS units with their tray grids.

## Tag Format

Bambu Lab uses **MIFARE Classic 1K** NFC tags with a TLV-based data structure:

| Field | Description |
|-------|-------------|
| Header | Magic byte (`0x5A`) + version (`0x01`) + 6 reserved bytes |
| Material | Type ID → name (PLA Basic, PETG, ABS, TPU, etc.) |
| Color | 3-byte RGB value displayed as hex |
| Weight | Remaining grams (2 bytes) + total grams (2 bytes) |
| Batch | Batch/lot number string |
| Manufacturer | Manufacturer name |

Reading uses MIFARE Classic 1K protocol:
- Sector-based authentication with key A (`0xFF FF FF FF FF FF`)
- 16 sectors, 3 data blocks each (skipping block 0 UID and sector trailers)
- Up to 752 bytes of user data fed to the TLV parser

A fallback parser handles generic NDEF text records for non-Bambu tags.

## LED Indicators

| State | LED Color |
|-------|-----------|
| Empty slot | Dim blue glow |
| Tag detected, reading | Yellow |
| Tag read, no color field | Green |
| Tag read, color field present | **Actual filament color** |
| AP mode / no WiFi | Dim blue (all slots) |
| WiFi disconnected | Orange (all slots) |

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
| AMS Unit | A (0) |
| MQTT Enabled | No |
| MQTT Update Interval | 5000 ms |
| MQTT Topic Prefix | device |

## License

MIT
