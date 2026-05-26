# BambuTagger AMS

Multi-spool NFC tag reader for Bambu Lab printers. Reads 4 Bambu Lab filament spool tags via RC522 and sends spool data to the printer over MQTT. Fully configurable via web interface with automatic AP fallback.

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
   - **ArduinoJson** by Benoit Blanchon (version 6.x)

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

- **Status** — live view of all 4 slots: UID, material, color hex, filament weight, batch number
- **WiFi Configuration** — SSID, password, device name
- **Printer Connection** — printer IP, port (8883 default), access code, serial number, MQTT toggle, update interval
- **Actions** — manual scan trigger, send tag data to printer

All settings persist in ESP32 NVS flash and survive reboots.

## OLED Display (128x64)

```
┌──────────────────────────────┐
│ Device Name           WiFi   │  ← status bar (white on black)
├──────────────────────────────┤
│ 1: PLA Basic   #FF0000  85% │  ← slot 1: material, color, fill %
│ 2: PETG Matte  #00FF00  42% │  ← slot 2
│ 3: ABS         #0000FF 100% │  ← slot 3
│ 4: empty                     │  ← slot 4
├──────────────────────────────┤
│ MQTT:OK  Printer connected   │  ← footer (MQTT status)
└──────────────────────────────┘
```

In AP mode the status bar shows the AP SSID and IP address on the main display area.

## Printer Communication

Uses MQTT (with optional TLS) to send AMS filament data to the Bambu Lab printer.

- **Port**: 8883 (default, TLS)
- **Topic**: `device/<serial>/request`
- **Command**: `ams_filament_setting` — sends material type, color, remaining grams, and tag UID

Enable/disable MQTT and configure the update interval via the web interface.

## Tag Format

Bambu Lab uses **NTAG216** NFC tags with a TLV-based data structure:

| Field | Description |
|-------|-------------|
| Header | Magic byte (`0x5A`) + version (`0x01`) |
| Material | Type ID mapped to name (PLA Basic, PETG, ABS, TPU, etc.) |
| Color | 3-byte RGB value displayed as hex |
| Weight | Remaining grams + total grams |
| Batch | Batch/lot number string |
| Manufacturer | Manufacturer name |

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
| MQTT Enabled | No |
| MQTT Update Interval | 5000 ms |

## License

MIT
