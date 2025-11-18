# ESP32-S3-POE-ETH-8DI-8RO - Multi-Network Relay Controller

A comprehensive Arduino firmware for the Waveshare ESP32-S3-POE-ETH-8DI-8RO board with support for Ethernet, WiFi, and Access Point modes, featuring automatic failover, web UI, MQTT integration, and RGB status LED.

![Waveshare ESP32-S3-POE-ETH-8DI-8RO](https://www.waveshare.com/w/upload/thumb/a/a6/ESP32-S3-ETH-8DI-8RO-1.jpg/500px-ESP32-S3-ETH-8DI-8RO-1.jpg)

## 📋 Table of Contents

- [Features](#features)
- [Hardware Overview](#hardware-overview)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Configuration](#configuration)
- [Network Modes](#network-modes)
- [Web Interface](#web-interface)
- [API Endpoints](#api-endpoints)
- [MQTT Integration](#mqtt-integration)
- [RGB Status LED](#rgb-status-led)
- [File Structure](#file-structure)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## ✨ Features

- **Multi-Network Support**:
  - Ethernet (W5500) with PoE capability
  - WiFi Station mode
  - WiFi Access Point fallback
  - Automatic priority-based failover (Ethernet → WiFi → AP)
  - ~800ms debounce for stable network transitions

- **8-Channel Relay Control**:
  - TCA9554 I2C I/O expander
  - Individual relay control via web UI and API
  - Batch control with mask operations
  - Visual feedback with color-coded buttons

- **8 Digital Inputs**:
  - Opto-isolated active-low inputs
  - Real-time status monitoring
  - Change detection with logging
  - Web UI radio button indicators

- **Web Interface**:
  - Responsive single-page application
  - Real-time status updates (500ms polling)
  - Color-coded relay buttons
  - Digital input indicators
  - Event log viewer
  - Network status display
  - MQTT connection indicator

- **MQTT Support**:
  - Automatic connection and reconnection
  - Individual relay state publishing (`relayboard/relay/<idx>/state`)
  - Relay control via commands (`relayboard/relay/<idx>/command`)
  - Digital input state publishing (`relayboard/input/<idx>/state`)
  - Bulk state publishing (all relays/inputs)
  - Authentication support

- **RGB Status LED** (WS2812):
  - Network mode indication:
    - 🔴 Red blink = Access Point mode
    - 🟢 Green blink = WiFi Station mode
    - 🔵 Blue blink = Ethernet mode
  - Relay state visualization (color-coded per channel)
  - Heartbeat pulse when idle (every 5 seconds)
  - All relays on = White
  - Mixed relays = Blended color

- **OTA Updates**:
  - ArduinoOTA support (WiFi/AP modes only)
  - Password-protected
  - Network-based firmware updates

- **Advanced Features**:
  - mDNS for easy discovery (`relayboard-xx.local`)
  - In-memory event logging (200 lines)
  - API for programmatic control
  - Serial output mirroring
  - DHCP support with Ethernet
  - Static IP configuration option

## 🔧 Hardware Overview

### Board Specifications

- **Microcontroller**: ESP32-S3-WROOM-1U-N16R8
  - Dual-core Xtensa LX7 @ 240MHz
  - 16MB Flash
  - 8MB PSRAM (Octal SPI)
  - 2.4GHz WiFi + Bluetooth LE

- **Networking**:
  - W5500 Ethernet controller (SPI)
  - PoE support (IEEE 802.3af)
  - Built-in WiFi

- **I/O**:
  - 8x relay outputs (10A @ 250V AC / 30V DC)
  - 8x opto-isolated digital inputs
  - TCA9554 I2C I/O expander (address 0x20)

- **Peripherals**:
  - WS2812 RGB LED (GPIO 38)
  - Buzzer (GPIO 46)
  - RS485 transceiver (GPIO 17/18)
  - SD card interface
  - USB-C for power/programming

- **Power**:
  - 7-36V DC via screw terminal
  - 5V USB-C
  - PoE (48V)

### Pin Mappings

All pin definitions are centralized in `BoardPins.h`:

```cpp
// Ethernet (W5500 SPI)
W5500_CS   = 16
W5500_INT  = 12
SPI_SCLK   = 15
SPI_MISO   = 14
SPI_MOSI   = 13

// I2C (TCA9554)
I2C_SDA    = 42
I2C_SCL    = 41
TCA9554_ADDR = 0x20

// Digital Inputs (opto-isolated, active-low)
DI_PINS[8] = { 4, 5, 6, 7, 8, 9, 10, 11 }

// Peripherals
RGB_LED    = 38
BUZZER     = 46
BOOT_BTN   = 0
RS485_TX   = 17
RS485_RX   = 18
```

## 📦 Prerequisites

### Software Requirements

- **Arduino IDE** 2.0 or newer (recommended)
  - Or PlatformIO (alternative)
- **ESP32 Board Support**: Espressif ESP32 v3.x
  - Install via Board Manager: Tools → Board Manager → "ESP32" by Espressif

### Required Libraries

Install via Arduino Library Manager (Tools → Manage Libraries):

1. **FastLED** (latest version)
   - For WS2812 RGB LED control
2. **TCA9554** (latest version)
   - For I2C I/O expander
3. **Ethernet** (included with ESP32 core)
   - For W5500 support
4. **PubSubClient** (latest version)
   - For MQTT communication

Optional libraries (already included with ESP32):
- WiFi
- ESPmDNS
- ArduinoOTA
- WebServer
- Wire
- SPI

### Board Settings

Configure in Arduino IDE (Tools menu):

```
Board: "ESP32S3 Dev Module"
USB CDC On Boot: "Enabled"
Flash Size: "16MB (128Mb)"
Partition Scheme: "Custom" (use included partitions.csv)
PSRAM: "OPI PSRAM"
Upload Speed: "921600"
```

**Important**: Use the provided `partitions.csv` file for proper OTA support:
- NVS: 20KB
- OTA Data: 8KB  
- App0: 3MB
- App1: 3MB (for OTA)
- SPIFFS: ~10MB

## 🚀 Installation

### 1. Download the Firmware

Clone or download this repository:
```bash
git clone https://github.com/abrinlee/ESP32-S3-POE-ETH-8DI-8RO-Python-Ethernet-WiFi-AP-Mode.git
```

### 2. Arduino IDE Setup

1. Extract files to your Arduino sketch folder (e.g., `~/Arduino/esp32_s3_8ch_relayboard_rgb/`)
2. Ensure the folder name matches the `.ino` file name
3. Verify the following files exist:
   ```
   esp32_s3_8ch_relayboard_rgb/
   ├── esp32_s3_8ch_relayboard_rgb.ino  (main sketch)
   ├── BoardPins.h                       (pin definitions)
   ├── RgbLed_WS2812.h                   (RGB LED class)
   ├── RgbLed_WS2812.cpp                 (RGB LED implementation)
   ├── StateHelpers.h                    (state helper functions)
   └── partitions.csv                    (partition table)
   ```

### 3. Configure Network Settings

Edit the main `.ino` file and update these constants near line 173:

```cpp
// WiFi Credentials
#define WIFI_SSID   "your_wifi_ssid"
#define WIFI_PASS   "your_wifi_password"

// Device Hostname (used for mDNS and AP name)
#define HOSTNAME    "relayboard"
```

### 4. Configure MQTT (Optional)

Edit MQTT settings near line 188:

```cpp
// MQTT Broker Configuration
#define MQTT_BROKER_IP IPAddress(192, 168, 0, 94)  // Your MQTT broker IP
#define MQTT_PORT 1883
#define MQTT_USER "mqtt_username"
#define MQTT_PASS "mqtt_password"
#define MQTT_CLIENT_ID "relayboard-client"
```

To disable MQTT, comment out the MQTT setup calls in `setup()` and `loop()`.

### 5. Upload Partition Table

**Critical Step**: Upload the custom partition table before uploading the sketch:

1. Open the sketch in Arduino IDE
2. Select: Tools → Partition Scheme → Custom
3. Place `partitions.csv` in the sketch folder
4. Tools → Partition → Upload Partition Table
5. Wait for completion

### 6. Compile and Upload

1. Select the correct COM port: Tools → Port → (your ESP32 port)
2. Click Upload (or press Ctrl+U)
3. Monitor serial output: Tools → Serial Monitor (115200 baud)

### 7. First Boot

On first boot, the board will:
1. Initialize all peripherals (TCA9554, SPI, I2C)
2. Attempt Ethernet connection (if cable connected)
3. Fall back to WiFi if configured
4. Fall back to Access Point mode if WiFi fails

Monitor the serial output (115200 baud) to see:
```
[BOOT] ESP32-S3 8-Channel Relay Board initializing...
[BOOT] Initializing peripherals...
[BOOT] Initializing MQTT...
[ETH] Bringing up Ethernet...
[ETH] Link status: LinkON (1)
[ETH] IP address: 192.168.1.100
[NET] Selected Ethernet mode
[WEB] Using Ethernet HTTP server on :80
[BOOT] Setup complete
```

## ⚙️ Configuration

### Network Priority

The firmware implements automatic failover with priority:

**Priority Order**:
1. **Ethernet** (highest priority)
2. **WiFi Station**
3. **Access Point** (fallback)

**Switching Logic**:
- Requires ~800ms of stable connection before switching
- Automatically detects link state and IP acquisition
- Gracefully handles network transitions
- Stops inactive servers to prevent conflicts

### Access Point Mode

If both Ethernet and WiFi fail, the board creates an AP:

```
SSID: relayboard-AP (or HOSTNAME-AP)
Password: (none - open network)
IP Address: 192.168.4.1
```

Access the web UI at: `http://192.168.4.1`

### Static IP Configuration

For Ethernet static IP, edit `bringupEthernet_()` function:

```cpp
// Replace DHCP code with:
Ethernet.begin(mac, IPAddress(192, 168, 1, 100),  // IP
                    IPAddress(192, 168, 1, 1),    // Gateway
                    IPAddress(255, 255, 255, 0)); // Subnet
```

For WiFi static IP, edit `wifiBegin()` function:

```cpp
WiFi.config(IPAddress(192, 168, 1, 100),  // IP
            IPAddress(192, 168, 1, 1),    // Gateway
            IPAddress(255, 255, 255, 0)); // Subnet
```

### mDNS Discovery

Access the board via hostname:
```
http://relayboard-xx.local/  (xx = last 2 bytes of MAC)
```

Note: mDNS may not work on all networks (especially with Ethernet).

## 🌐 Network Modes

### Mode Detection

The firmware automatically detects and switches between network modes:

```cpp
MODE_ETH  (3)  // Ethernet connected with valid IP
MODE_WIFI (2)  // WiFi station connected
MODE_AP   (1)  // Access Point mode
MODE_NONE (0)  // No network
```

### Ethernet Mode

**Features**:
- W5500 SPI Ethernet controller
- DHCP or static IP
- PoE support (hardware)
- Automatic link detection
- HTTP server on port 80

**LED Indicator**: Blue blink

**HTTP Server**: Custom `EthernetServer` implementation (not WebServer)

**Note**: OTA updates are disabled in Ethernet mode (use WiFi for OTA).

### WiFi Station Mode

**Features**:
- Connects to configured AP
- DHCP or static IP
- ArduinoOTA support
- mDNS discovery
- HTTP server on port 80

**LED Indicator**: Green blink

### Access Point Mode

**Features**:
- Creates open AP (no password)
- Fixed IP: 192.168.4.1
- ArduinoOTA support
- HTTP server on port 80

**LED Indicator**: Red blink

**Activation**: Automatic fallback when Ethernet and WiFi fail

## 🖥️ Web Interface

Access the web interface by navigating to the board's IP address in a browser.

### Features

- **Relay Control Panel**:
  - 8 color-coded relay buttons
  - Visual state indication (tinted when ON)
  - Click to toggle individual relays
  - All ON / All OFF quick controls

- **Digital Input Monitor**:
  - 8 input status indicators
  - Red dot = Active (pulled low)
  - Gray dot = Inactive
  - Real-time updates

- **Network Information**:
  - Current interface (Ethernet/WiFi/AP)
  - IP address
  - WiFi SSID (if applicable)
  - Signal strength (WiFi only)
  - MQTT connection status

- **Event Log**:
  - Last 50 events displayed
  - Relay state changes
  - Digital input transitions
  - Network events
  - Auto-refresh every 3 seconds
  - Clear log button

- **System Controls**:
  - Reboot button
  - Diagnostics endpoint link
  - Responsive design for mobile

### Color Coding

Each relay has a unique color for easy identification:

| Relay | Color |
|-------|-------|
| Relay 1 | 🔴 Red |
| Relay 2 | 🟠 Orange |
| Relay 3 | 🟡 Yellow |
| Relay 4 | 🟢 Chartreuse |
| Relay 5 | 🟢 Green |
| Relay 6 | 🔵 Cyan |
| Relay 7 | 🔵 Blue |
| Relay 8 | 🟣 Magenta |

## 📡 API Endpoints

All endpoints return JSON or plain text responses.

### GET /

Returns the main web UI (HTML/CSS/JS single page).

### GET /api/state

Returns current system state.

**Response**:
```json
{
  "mask": 5,              // Binary relay state (bit 0 = relay 1, etc.)
  "mqtt_connected": true,
  "di_mask": 3,           // Binary DI state (bit 0 = DI 1, etc.)
  "di": [1, 1, 0, 0, 0, 0, 0, 0],  // Individual DI states (1=active/low)
  "count": 8,             // Number of relays
  "net": {
    "iface": "Ethernet",
    "ip": "192.168.1.100",
    "ssid": "",           // WiFi SSID (empty for Ethernet)
    "rssi": 0             // WiFi signal strength (0 for Ethernet)
  }
}
```

### GET /api/relay?idx=X&on=Y

Control a single relay.

**Parameters**:
- `idx` or `index`: Relay index (0-7)
- `on` or `state`: `1`/`true`/`on` = ON, `0`/`false`/`off` = OFF

**Example**:
```bash
curl "http://192.168.1.100/api/relay?idx=0&on=1"  # Turn relay 1 ON
curl "http://192.168.1.100/api/relay?idx=7&on=0"  # Turn relay 8 OFF
```

**Response**: Same as `/api/state`

### POST /api/relay

Control a single relay (POST method).

**Body** (form-urlencoded):
```
idx=0&on=1
```

**Response**: Same as `/api/state`

### POST /api/mask

Set all relays at once using a bitmask.

**Body** (form-urlencoded):
```
value=85  (binary: 01010101 = relays 1,3,5,7 ON)
```

**Example**:
```bash
curl -X POST -d "value=255" http://192.168.1.100/api/mask  # All ON
curl -X POST -d "value=0" http://192.168.1.100/api/mask    # All OFF
curl -X POST -d "value=170" http://192.168.1.100/api/mask  # Even relays
```

**Response**: Same as `/api/state`

### GET /api/log

Returns the event log (last 200 events, returns last 50).

**Response**:
```json
{
  "lines": [
    {
      "id": 1,
      "ms": 1234,
      "txt": "Relay 1 ON"
    },
    ...
  ]
}
```

### POST /api/log/clear

Clears the event log.

**Response**: `"cleared"` (text/plain)

### GET /api/diag

Returns diagnostic information.

**Response**:
```json
{
  "ota_ready": true,
  "ota_port": 3232,
  "heap": 245678
}
```

### GET /reboot

Reboots the device.

**Response**: `"rebooting"` (text/plain)

## 📨 MQTT Integration

The firmware includes comprehensive MQTT support for integration with home automation systems.

### MQTT Configuration

Edit these defines in the main `.ino` file (around line 188):

```cpp
#define MQTT_BROKER_IP IPAddress(192, 168, 0, 94)
#define MQTT_PORT 1883
#define MQTT_USER "username"
#define MQTT_PASS "password"
#define MQTT_CLIENT_ID "relayboard-client"
#define MQTT_BASE_TOPIC "relayboard"
```

### Topic Structure

**Relay State Publishing** (device → broker):
```
relayboard/relay/1/state  → "ON" or "OFF"
relayboard/relay/2/state  → "ON" or "OFF"
...
relayboard/relay/8/state  → "ON" or "OFF"
relayboard/relays/state   → "01010101" (8-bit binary mask)
```

**Relay Command Subscription** (broker → device):
```
relayboard/relay/1/set  ← "ON" or "OFF"
relayboard/relay/2/set  ← "ON" or "OFF"
...
relayboard/relay/8/set  ← "ON" or "OFF"
relayboard/relays/set   ← "01010101" (8-bit binary mask)
```

**Digital Input State Publishing** (device → broker):
```
relayboard/input/1/state  → "ON" or "OFF"  (ON = active/low)
relayboard/input/2/state  → "ON" or "OFF"
...
relayboard/input/8/state  → "ON" or "OFF"
relayboard/inputs/state   → "01010101" (8-bit binary mask)
```

### MQTT Features

- **Automatic Connection**: Connects on boot and reconnects on disconnect
- **Retained Messages**: All state messages are retained for last known state
- **QoS 0**: Used for all messages (at most once delivery)
- **State Publishing**:
  - Individual relay state on change
  - Bulk relay state every 5 seconds
  - Individual input state on change
  - Bulk input state on change
- **Command Processing**:
  - Individual relay control via commands
  - Bulk relay control via mask command
- **Connection Indicator**: Visible in web UI

### Home Assistant Integration

Example `configuration.yaml` entries:

```yaml
mqtt:
  switch:
    - name: "Relay 1"
      state_topic: "relayboard/relay/1/state"
      command_topic: "relayboard/relay/1/set"
      payload_on: "ON"
      payload_off: "OFF"
      
  binary_sensor:
    - name: "Digital Input 1"
      state_topic: "relayboard/input/1/state"
      payload_on: "ON"
      payload_off: "OFF"
      device_class: occupancy
```

### Node-RED Integration

Use MQTT nodes to subscribe to state topics and publish to command topics:

```json
[
  {
    "id": "mqtt-in",
    "type": "mqtt in",
    "topic": "relayboard/relay/+/state",
    "broker": "mqtt-broker"
  },
  {
    "id": "mqtt-out",
    "type": "mqtt out",
    "topic": "relayboard/relay/1/set",
    "broker": "mqtt-broker"
  }
]
```

## 💡 RGB Status LED

The onboard WS2812 RGB LED provides visual feedback for system status.

### Network Mode Indicators

- **🔴 Red blink** (150ms): Access Point mode
- **🟢 Green blink** (150ms): WiFi Station mode  
- **🔵 Blue blink** (150ms): Ethernet mode
- **⚫ Off**: No network connection

### Relay State Visualization

When any relay is active:

- **Single relay ON**: Shows corresponding color from palette
- **Multiple relays ON**: Shows blended/average color
- **All relays ON**: White
- **No relays ON**: Returns to network mode indicator

### Heartbeat

When no relays are active (idle mode):
- Brief pulse every 5 seconds using the network mode color
- Can be disabled: `rgb.setHeartbeatEnabled(false);`

### LED Configuration

Customize in `RgbLed_WS2812.h`:

```cpp
#define LED_COLOR_ORDER RGB  // Change to GRB if colors are wrong

// Palette (modify if desired):
static constexpr uint8_t PALETTE[8][3] = {
  {255,   0,   0}, // 0: Red
  {255, 128,   0}, // 1: Orange
  {255, 255,   0}, // 2: Yellow
  {128, 255,   0}, // 3: Chartreuse
  {  0, 255,   0}, // 4: Green
  {  0, 255, 255}, // 5: Cyan
  {  0,   0, 255}, // 6: Blue
  {255,   0, 255}  // 7: Magenta
};
```

## 📁 File Structure

```
esp32_s3_8ch_relayboard_rgb/
├── esp32_s3_8ch_relayboard_rgb.ino  # Main sketch (1665 lines)
│   ├── Network management (Ethernet/WiFi/AP)
│   ├── Web server implementation
│   ├── MQTT client implementation
│   ├── HTTP handlers
│   ├── Digital input polling
│   ├── Event logging system
│   └── Main setup() and loop()
│
├── BoardPins.h                       # Pin definitions (140 lines)
│   ├── W5500 Ethernet pins
│   ├── I2C pins (TCA9554)
│   ├── Digital input pins array
│   ├── Peripheral pins (RGB, buzzer, etc.)
│   └── Convenience helper functions
│
├── RgbLed_WS2812.h                   # RGB LED class definition (130 lines)
│   ├── Color palette for 8 relays
│   ├── Network mode indicators
│   ├── Heartbeat configuration
│   └── Public API methods
│
├── RgbLed_WS2812.cpp                 # RGB LED implementation (130 lines)
│   ├── FastLED integration
│   ├── Color blending for multiple relays
│   ├── Network mode visualization
│   ├── Heartbeat pulse logic
│   └── State management
│
├── StateHelpers.h                    # State helper functions (70 lines)
│   ├── Digital input mask functions
│   ├── Active-low conversion helpers
│   ├── Relay state accessors
│   └── Bit manipulation utilities
│
└── partitions.csv                    # Custom partition table
    ├── NVS (20KB)
    ├── OTA Data (8KB)
    ├── App0 (3MB) - Primary firmware
    ├── App1 (3MB) - OTA update slot
    └── SPIFFS (10MB) - File storage
```

### Key Components

1. **Main Sketch** (`esp32_s3_8ch_relayboard_rgb.ino`):
   - Network priority and failover logic
   - Dual HTTP server support (WebServer for WiFi/AP, EthernetServer for Ethernet)
   - MQTT client with auto-reconnect
   - Web UI served from PROGMEM
   - JSON API implementation
   - Event logging system with ring buffer
   - Digital input change detection

2. **BoardPins.h**:
   - Single source of truth for pin mappings
   - Namespace-based organization to avoid globals
   - Backward compatibility aliases
   - Helper functions for I2C, SPI, and DI initialization

3. **RgbLed_WS2812 Class**:
   - Encapsulates all LED logic
   - FastLED-based for precise timing
   - Configurable color order (RGB/GRB)
   - Idle detection for heartbeat
   - Network mode and relay state visualization

4. **StateHelpers.h**:
   - Header-only utility functions
   - Active-low DI conversion (hardware is active-low)
   - Relay state accessors
   - Reusable for other projects (RS485, MQTT gateways)

5. **Custom Partition Table**:
   - Allocates 6MB for OTA (2x 3MB app slots)
   - Large SPIFFS partition for future expansion
   - Proper alignment for ESP32-S3

## 🔍 Troubleshooting

### Compilation Issues

**Error: "Ethernet.h: No such file or directory"**
- Solution: Update ESP32 board support to v3.x
- Verify: Tools → Board Manager → "ESP32" by Espressif

**Error: "TCA9554.h: No such file or directory"**
- Solution: Install TCA9554 library via Library Manager
- Search: "TCA9554" by Rob Tillaart

**Error: "FastLED.h: No such file or directory"**
- Solution: Install FastLED library via Library Manager
- Get latest version from Library Manager

**Error: "PubSubClient.h: No such file or directory"**
- Solution: Install PubSubClient library via Library Manager
- Search: "PubSubClient" by Nick O'Leary

**Error: Partition table errors**
- Solution: Ensure `partitions.csv` is in the sketch folder
- Verify: Tools → Partition Scheme → "Custom"
- Upload partition table before sketch

### Upload Issues

**Cannot connect to serial port**:
1. Press and hold BOOT button
2. Click Upload
3. Release BOOT when "Connecting..." appears
4. Or press RST button after upload starts

**Upload fails mid-way**:
- Reduce upload speed: Tools → Upload Speed → "460800"
- Check USB cable quality
- Try different USB port

### Network Issues

**Ethernet not connecting**:
1. Verify cable is properly connected
2. Check serial output for W5500 initialization
3. Verify PoE power supply (if using PoE)
4. Check LED on RJ45 jack (should blink)
5. Try manual IP instead of DHCP
6. Verify SPI connections

**WiFi not connecting**:
1. Verify SSID and password in configuration
2. Check WiFi signal strength
3. Ensure 2.4GHz network (ESP32 doesn't support 5GHz)
4. Check for special characters in SSID/password
5. Monitor serial output for connection attempts

**Stuck in AP mode**:
1. Verify WiFi credentials are correct
2. Check WiFi network is in range
3. Try connecting Ethernet cable
4. Serial monitor shows connection status

**Cannot access via mDNS** (`.local`):
- mDNS may not work on all networks
- Use IP address directly
- Check firewall settings
- mDNS less reliable with Ethernet

### Relay Issues

**Relays not switching**:
1. Verify TCA9554 is detected (check I2C scan)
2. Check I2C connections (GPIO 41, 42)
3. Verify TCA9554 address (default 0x20)
4. Check power supply to relay board
5. Serial output shows relay commands

**Relays click but don't switch load**:
- Check load wiring
- Verify load voltage/current within ratings
- Ensure relay contacts are not damaged
- Test with different load

**Relays in wrong state at boot**:
- Code initializes all relays OFF at boot
- Check hardware power-on glitches
- Verify TCA9554 initialization

### MQTT Issues

**Cannot connect to MQTT broker**:
1. Verify broker IP address and port
2. Check username/password if authentication enabled
3. Ensure broker is running (`sudo systemctl status mosquitto`)
4. Check firewall rules (allow port 1883)
5. Monitor serial output for connection attempts
6. Check web UI for MQTT connection status

**MQTT messages not received**:
1. Verify subscriptions are correct
2. Check QoS settings
3. Monitor broker logs
4. Use MQTT client (mosquitto_sub) to test
5. Verify topic structure matches code

**MQTT disconnects frequently**:
1. Check network stability
2. Increase keepalive interval in PubSubClient
3. Monitor broker resource usage
4. Check for broker connection limits

### Digital Input Issues

**Inputs not detecting changes**:
1. Verify input wiring (opto-isolated, active-low)
2. Check external power supply for inputs
3. Monitor serial output for state changes
4. Inputs are active-LOW (pulled to ground when active)
5. Try different input channel

**Input states inverted**:
- Hardware is active-low by design
- Code uses `StateHelpers.h` for conversion
- `diActive()` returns 1 when input is pulled LOW

### RGB LED Issues

**LED not lighting**:
1. Verify FastLED library is installed
2. Check GPIO 38 connection
3. Verify power supply
4. Try different LED_COLOR_ORDER (RGB vs GRB)

**Wrong colors**:
- Change `LED_COLOR_ORDER` in `RgbLed_WS2812.h`:
  ```cpp
  #define LED_COLOR_ORDER GRB  // Try GRB instead of RGB
  ```

**LED stays one color**:
- Check relay states (LED shows relay colors when active)
- Verify heartbeat is enabled
- Check network mode detection

### Web Interface Issues

**Web page not loading**:
1. Verify correct IP address
2. Check network connectivity
3. Try different browser
4. Clear browser cache
5. Check serial output for HTTP requests
6. Verify server is running (check mode detection logs)

**Buttons not working**:
1. Check JavaScript console for errors
2. Verify API endpoints are responding
3. Monitor serial output for POST requests
4. Check Content-Type headers

**Status not updating**:
- JavaScript polls `/api/state` every 500ms
- Check browser network tab for failed requests
- Verify JSON responses are valid

### General Debugging

**Enable verbose logging**:
```cpp
// Add to setup():
esp_log_level_set("*", ESP_LOG_DEBUG);
```

**Monitor serial output** (115200 baud):
- Shows network state changes
- Displays relay commands
- Reports digital input changes
- Logs HTTP requests
- MQTT connection status

**Check diagnostics**:
- Browse to `/api/diag` for system info
- Check heap memory
- Verify OTA status

**I2C scanner**:
```cpp
// Add to setup() for troubleshooting:
for (byte addr = 1; addr < 127; addr++) {
  Wire.beginTransmission(addr);
  if (Wire.endTransmission() == 0) {
    Serial.printf("Found I2C device at 0x%02X\n", addr);
  }
}
```

**Expected I2C addresses**:
- TCA9554: 0x20 (relay I/O expander)
- PCF85063: 0x51 (RTC, if populated)

## 📄 License

**Author**: Antony E. Brinlee  
**License**: GNU General Public License v2.0  
**Release**: Rev 1  
**Date**: October 2024

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

## 🙏 Acknowledgments

- **Waveshare** for the ESP32-S3-POE-ETH-8DI-8RO hardware
- **Espressif** for the ESP32-S3 platform and Arduino core
- **FastLED** library for precise WS2812 control
- **PubSubClient** library for MQTT support
- Community contributors and testers

## 📚 Additional Resources

- **Waveshare Wiki**: [ESP32-S3-ETH-8DI-8RO](https://www.waveshare.com/wiki/ESP32-S3-ETH-8DI-8RO)
- **ESP32 Arduino Core**: [GitHub](https://github.com/espressif/arduino-esp32)
- **FastLED Documentation**: [fastled.io](http://fastled.io/)
- **MQTT Protocol**: [mqtt.org](https://mqtt.org/)
- **Home Assistant MQTT**: [Integration Docs](https://www.home-assistant.io/integrations/mqtt/)

## 🔄 Future Enhancements

Potential features for future releases:

- [ ] SD card logging
- [ ] Web-based configuration editor
- [ ] Modbus RTU/TCP support via RS485
- [ ] CANbus support (for -C variant)
- [ ] Real-time clock integration
- [ ] Scheduled relay operations
- [ ] Email/SMS alerts
- [ ] Advanced MQTT discovery for Home Assistant
- [ ] RESTful API with authentication
- [ ] Web-based firmware updates
- [ ] Configuration backup/restore

---

**Questions or Issues?** Open an issue on GitHub or check the troubleshooting section above.

**Contributions Welcome!** Pull requests are appreciated. Please test thoroughly before submitting.
