# ESP32-S3-POE-ETH-8DI-8RO Fallback Modes Ethernet,WiFi,AP-Mode,MQTT
This is based on the Waveshare ESP32-S3-POE-ETH-8RO-8DI module shown below.


**Author:** Antony E. Brinlee  
**Release:** Rev 2  
**License:** GNU 2.0

---

## 1) Purpose &amp; Scope

This page is the single, copy‑pastable runbook for engineers to:

- Install the required toolchains and libraries
- Build and flash the firmware
- Run the device for lab/line **test automation** (HTTP/MQTT ready patterns)
- Diagnose and fix common issues fast

The code in this release targets the **Waveshare ESP32‑S3‑PoE‑ETH‑8DI‑8RO** (a.k.a. “8‑channel relay board” with 8 digital inputs). It uses:

- **ESP32‑S3** (dual‑core, USB‑CDC flashing)
- **I²C TCA9554** 8‑bit expander for relays/DIs
- **WS2812 RGB LED** (status)
- **Wi‑Fi** primary; **W5500 Ethernet** optional (SPI)
- A small embedded **web UI** and simple **HTTP endpoints** suitable for automation scripts

> **Repository layout (Rev 1 zip):**
> 
> - `esp32_s3_8ch_relayboard_rgb.ino` – main sketch
> - `BoardPins.h` – **single source of truth** for pins &amp; tiny helpers
> - `RgbLed_WS2812.h/.cpp` – status LED wrapper
> - `StateHelpers.h` – state/bitfield helpers

> If you add files later, update this page’s **Dependencies** and **Build steps**.

---

## 2) Prerequisites

### 2.1 OS / Tools

- **Windows 11**, Linux, or macOS
- **Arduino IDE 2.x** (preferred for Rev 1)  
    *PlatformIO is fine, but the steps below are written for Arduino.*
- **USB‑C cable** for the ESP32‑S3’s native USB
- (Optional) **DHCP reservation** on your router for a stable IP when on Ethernet

### 2.2 ESP32 Core &amp; Board Setup

1. Open Arduino IDE → **File → Preferences** → *Additional Boards Manager URLs*:
    
    
    - `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. **Tools → Board Manager** → search **ESP32** → install **Espressif ESP32** core (**3.x** recommended).
3. **Tools → Board** → pick **ESP32S3 Dev Module**.
4. Recommended **Tools** settings (match our lab images):
    
    
    - USB CDC On Boot: **Enabled**
    - USB Mode: **Hardware CDC**
    - Flash Size: **16MB** (if your board variant has it)
    - PSRAM: **OPI PSRAM** (if present)
    - CPU Freq: **240 MHz**
    - Partition Scheme: **Default** or **Custom** (if the sketch includes one)

> If you don’t see a serial port after flashing, re‑select **/dev/ttyACM0** (Linux) or the **USB COM** port (Windows), or press the on‑board **RST** button once.

### 2.3 Required Arduino Libraries

Install via **Library Manager** unless noted:

- **Rob Tillaart – TCA9554** (I²C GPIO expander)
- **FastLED** *or* a minimal WS2812 driver (Rev 1 ships `RgbLed_WS2812.*`, no external lib needed. If you prefer FastLED, keep the versions consistent across the team.)
- **ArduinoOTA** (bundled with ESP32 core)
- **WiFi**, **Wire**, **SPI** (bundled with ESP32 core)
- **ETH** / W5500 support – if you enable Ethernet in code, stick to ESP32 core 3.x where W5500 support is stable (we use a light‑touch bring‑up; avoid third‑party network stacks unless necessary).
- *(Optional)* **ArduinoJson** if you add JSON endpoints later.
- Ethernet\_Generic

> **Pin &amp; feature decisions live in `BoardPins.h`.** Treat it as the authoritative map.

---

## 3) Getting the Code

1. Obtain the **Rev 1 zip** and extract it into your Arduino sketch folder (e.g., `~/Arduino/ESP32_8CH_Relay/`).
2. Ensure the primary file is named **`esp32_s3_8ch_relayboard_rgb/esp32_s3_8ch_relayboard_rgb.ino`** (folder and .ino must match for Arduino).
3. Confirm the following files exist at the sketch root:
    
    
    - `esp32_s3_8ch_relayboard_rgb.ino`
    - `BoardPins.h`
    - `RgbLed_WS2812.h`, `RgbLed_WS2812.cpp`
    - `StateHelpers.h`

---

## 4) Configuration

Open **`BoardPins.h`** and review:

- **I²C pins**: `BoardPins::I2C_SDA`, `BoardPins::I2C_SCL`
- **TCA9554 address** (commonly `0x20` – `0x27` depending on A0‑A2)
- **Relay mapping &amp; polarity**: this board’s relays are **active‑low**. Our helper functions abstract this; still, verify the intended default state (all‑off at boot).
- **DI inputs**: confirm pull‑ups vs external biasing.
- **WS2812 status LED pin** (often **GPIO 38** on Waveshare boards) &amp; LED count (usually 1)
- **W5500 SPI** (if used): `CS`, `MOSI`, `MISO`, `SCLK`, and optional `RST`.
- **mDNS / Hostname**: choose a unique name (e.g., `relayboard-xx`).

**Wi‑Fi credentials**: If stored in code, set `WIFI_SSID` / `WIFI_PASS`. For production, prefer a small `secrets.h` (not checked in) or provisioning.

---

## 5) Build &amp; Flash

### 5.1 USB Flash (first time)

1. Connect USB‑C.
2. Select the serial port under **Tools → Port**.
3. **Sketch → Upload**. First flash can take ~30–60s.
4. Open **Serial Monitor** at **115200 8N1** to watch boot logs.

### 5.2 OTA Updates (after first USB flash)

1. With the board on the same LAN, Arduino IDE shows a **Network Port** like `relayboard-xx at 192.168.x.x`.
2. Select that port and **Upload**.  
    *Tip:* If OTA fails intermittently, briefly re‑flash over USB to reset the OTA credentials and try again.

### 5.3 Ethernet Bring‑Up (Optional)

- Plug Ethernet into the Waveshare board (PoE or standard).
- On boot you should see `ETH` logs and a DHCP lease. If you need a fixed IP, create a **DHCP reservation** in your router.
- If both Wi‑Fi and Ethernet are active, the code favors the interface that acquires first (Rev 1). You can change priority in the `bringupEthernet_()` / Wi‑Fi init sections.

---

## 6) Running &amp; Using the Device

### 6.1 Web UI

- Browse to `http://<device-ip>/` or `http://relayboard-xx.local/` (mDNS).
- You should see relay buttons (tinted for **ON**, clear for **OFF**), DI indicators, and status.
- On first boot, all relays should be **OFF** (active‑low hardware, handled in init). If they click on at power, see **Troubleshooting ▶ Power‑on glitch**.

### 6.2 Automation‑Friendly HTTP Patterns

Rev 1 exposes simple routes (patterned after `server.on(...)` in the .ino):

- `GET /api/info` → firmware/build info, interface (Wi‑Fi/Eth), IP, uptime
- `GET /api/relays` → returns an 8‑bit mask (or simple JSON) of relay states
- `POST /api/relays?set=<mask>` → sets all relays via bitmask
- `POST /api/relay/<n>?on=0|1` → sets a single relay `0..7`
- `GET /api/di` → returns 8‑bit DI snapshot (1 = high level on the MCU pin)

> **Note:** If your copy of Rev 1 diverges, search the sketch for `server.on(` and adjust your scripts accordingly. We keep routes minimal and deterministic for test rigs.

### 6.3 MQTT

MQTT is now integrated in for Rev 2

- `relayboard/<host>/di` – DI bitmask
- `relayboard/<host>/relay` – relay bitmask
- `relayboard/<host>/status` – online/uptime/IP

Add subscriptions for `relayboard/<host>/cmd` if you need remote control via broker.

---

## 7) Example: Quick Automation from a PC

### 7.1 Curl

```bash
# Read relay mask
curl http://relayboard-xx.local/api/relays

# Turn relay 3 ON (bit 3)
curl -X POST "http://relayboard-xx.local/api/relay/3?on=1"

# Set all relays (bitmask, e.g., 0x05 => relays 0 and 2 ON)
curl -X POST "http://<ip>/api/relays?set=0x05"


```

### 7.2 Python (requests)

```python
import requests as r
base = "http://relayboard-xx.local"
print(r.get(base+"/api/info").json())
mask = int(r.get(base+"/api/di").text, 0)
print(f"DI mask: 0x{mask:02X}")
# Toggle relay 0
r.post(base+"/api/relay/0", params={"on": 1})


```

---

## 8) Troubleshooting

### 8.1 Build/Compile

**Error:** `unterminated raw string` around `HTML_INDEX[] PROGMEM = R"rawliteral(`  
**Fix:** Close the raw literal with **`)rawliteral";`**. Example:

```cpp
const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!doctype html>
<html> ...
</html>
)rawliteral";


```

**Error:** `#include nested depth 200 exceeds maximum`  
**Cause:** Accidentally pasted .ino contents into `BoardPins.h` or circular includes.  
**Fix:** Keep `BoardPins.h` lean (pins + tiny helpers). No `#include <Arduino.h>` more than once if not needed, and no `#include` of the sketch back into headers.

**Error:** TCA9554 not connected (runtime log)  
**Checklist:**

- Confirm I²C wiring to the expander (SDA/SCL per `BoardPins.h`)
- Ensure **pull‑ups** (often 4.7k–10k) to 3.3V – many boards already have them
- Verify the **address** (A0–A2 straps) matches the code (e.g., 0x20)
- Use `i2cdetect` (external adapter) or add a quick I²C scan in `setup()` for debug

**Error:** `ETH` bring‑up fails  
**Checklist:**

- W5500 **CS/MOSI/MISO/SCLK** match `BoardPins.h` and share SPI with care
- Provide a brief **RST** pulse (code includes optional `W5500_RST_PIN` block)
- Ensure **ESP32 core 3.x**; older cores differ in ETH init APIs
- Try Wi‑Fi first to validate the rest of the stack

**OTA not appearing in Arduino’s Port list**

- Make sure board and PC are on the **same subnet**
- Reboot the board once; in IDE, toggle **Network Discovery**
- As a last resort, USB‑flash once to refresh OTA credentials

### 8.2 Power‑On Glitch (Relays click at boot)

- The hardware is **active‑low**; we stage outputs safely:
    
    
    1. Write **all‑OFF** to the **output register** while pins are still **inputs**
    2. Switch pins to **outputs** (now they drive low or high deterministically)
- Confirm your init follows that order (Rev 1 does in the TCA setup helper).

### 8.3 mDNS / Hostname not resolving

- Use the raw IP once, then fix router’s **DHCP reservation** for stability
- Some corporate networks block `.local` – use the IP in automation scripts

### 8.4 WS2812 status LED dark or erratic

- Verify the **GPIO** and **LED count** in `RgbLed_WS2812.*`
- Avoid long blocking delays; refresh the LED in the main loop state machine

### 8.5 Serial goes away after Ethernet starts

- Normal with network‑first designs. Keep a short grace‑period before switching logs to network, or simply rely on **USB‑CDC** Serial (recommended) for lab use.

---

## 9) Code Conventions (Rev 1)

- **Pins &amp; constants** live in `BoardPins.h` (namespaced; tiny inline helpers allowed)
- **No business logic** in headers; avoid circular includes
- **Active‑low relays** abstracted behind helpers `getRelayMask()`, `setRelayBit()`, etc.
- **Status LED** shows: Boot (white), Wi‑Fi/Eth connect (green), Error (red), Action (blue pulse)
- **Web UI** served from `HTML_INDEX[]` raw literal in PROGMEM

---

## 10) Extending for Production Test

- Add **/api/sequence** that accepts a simple JSON script (steps, delays, assertions) if you want test recipes uploaded from the line PC.
- Add **MQTT publishing** for DI/relay masks every N ms; consume from your factory orchestrator.
- Consider a **result log** endpoint: `/api/log` (append mode) to stream outcome lines to a NAS.

---

## 11) Versioning &amp; Attribution

- Tag releases as **Rev N** in the header banner.
- Keep this BookStack page version‑locked to the matching code zip.
- **License:** *Creative Commons Attribution 4.0 International (CC BY 4.0)*.  
    Required attribution: **“Antony E. Brinlee — ESP32‑S3 8‑DI/8‑Relay Test Automation (Rev 1)”**.

---

## 12) Appendix

### 12.1 Known‑Good Toolchain Snapshot

- Arduino IDE 2.3.x
- ESP32 core 3.0.x
- TCA9554 0.4.x
- FastLED 3.6.x *(if used)*

### 12.2 Quick I²C Scan Snippet

```cpp
#include <Wire.h>
void scanI2C() {
  byte cnt=0; Serial.println("I2C scan...");
  for (byte addr=1; addr<127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission()==0) { Serial.printf("Found 0x%02X\n", addr); cnt++; }
  }
  Serial.printf("Done. %u device(s).\n", cnt);
}


```

### 12.3 Minimal WS2812 Blink (1 LED)

```cpp
#include "RgbLed_WS2812.h"
RgbLed ws(38, 1); // pin, count
void setup(){ ws.begin(); ws.set(0, 16,16,16); ws.show(); }
void loop(){ ws.set(0, 0,32,0); ws.show(); delay(250); ws.set(0, 32,0,0); ws.show(); delay(250);}


```

## Programming

# ESP32-S3 Relay Board – Arduino IDE Settings (Waveshare 8-Relay)

<div class="_tableContainer_1rjym_1" id="bkmrk-setting-value-why-bo"><div class="group _tableWrapper_1rjym_13 flex w-fit flex-col-reverse" tabindex="-1"><table class="w-fit min-w-(--thread-content-width)" data-end="1722" data-start="247"><thead data-end="272" data-start="247"><tr data-end="272" data-start="247"><th data-col-size="sm" data-end="257" data-start="247">Setting</th><th data-col-size="md" data-end="265" data-start="257">Value</th><th data-col-size="md" data-end="272" data-start="265">Why</th></tr></thead><tbody data-end="1722" data-start="287"><tr data-end="364" data-start="287"><td data-col-size="sm" data-end="299" data-start="287">**Board**</td><td data-col-size="md" data-end="322" data-start="299">`ESP32S3 Dev Module`</td><td data-col-size="md" data-end="364" data-start="322">Matches the module on the relay board.</td></tr><tr data-end="433" data-start="365"><td data-col-size="sm" data-end="387" data-start="365">**USB CDC On Boot**</td><td data-col-size="md" data-end="399" data-start="387">`Enabled`</td><td data-col-size="md" data-end="433" data-start="399">Serial over USB from power-up.</td></tr><tr data-end="507" data-start="434"><td data-col-size="sm" data-end="454" data-start="434">**CPU Frequency**</td><td data-col-size="md" data-end="473" data-start="454">`240 MHz (WiFi)`</td><td data-col-size="md" data-end="507" data-start="473">Standard max clock with Wi-Fi.</td></tr><tr data-end="584" data-start="508"><td data-col-size="sm" data-end="531" data-start="508">**Core Debug Level**</td><td data-col-size="md" data-end="571" data-start="531">`None` *(or `Error` while debugging)*</td><td data-col-size="md" data-end="584" data-start="571">Optional.</td></tr><tr data-end="651" data-start="585"><td data-col-size="sm" data-end="607" data-start="585">**USB DFU On Boot**</td><td data-col-size="md" data-end="620" data-start="607">`Disabled`</td><td data-col-size="md" data-end="651" data-start="620">We use UART/CDC bootloader.</td></tr><tr data-end="768" data-start="652"><td data-col-size="sm" data-end="695" data-start="652">**Erase All Flash Before Sketch Upload**</td><td data-col-size="md" data-end="736" data-start="695">`Disabled` *(enable only when needed)*</td><td data-col-size="md" data-end="768" data-start="736">Full wipe only for recovery.</td></tr><tr data-end="818" data-start="769"><td data-col-size="sm" data-end="789" data-start="769">**Events Run On**</td><td data-col-size="md" data-end="800" data-start="789">`Core 1`</td><td data-col-size="md" data-end="818" data-start="800">Default; fine.</td></tr><tr data-end="897" data-start="819"><td data-col-size="sm" data-end="836" data-start="819">**Flash Mode**</td><td data-col-size="md" data-end="851" data-start="836">`QIO 80 MHz`</td><td data-col-size="md" data-end="897" data-start="851">Correct for the 16-MB flash on this board.</td></tr><tr data-end="1001" data-start="898"><td data-col-size="sm" data-end="915" data-start="898">**Flash Size**</td><td data-col-size="md" data-end="932" data-start="915">`16MB (128Mb)`</td><td data-col-size="md" data-end="1001" data-start="932">**Important**—prevents “exceeds flash chip size 0x400000” errors.</td></tr><tr data-end="1078" data-start="1002"><td data-col-size="sm" data-end="1021" data-start="1002">**JTAG Adapter**</td><td data-col-size="md" data-end="1034" data-start="1021">`Disabled`</td><td data-col-size="md" data-end="1078" data-start="1034">Enable only if you use an external JTAG.</td></tr><tr data-end="1124" data-start="1079"><td data-col-size="sm" data-end="1101" data-start="1079">**Arduino Runs On**</td><td data-col-size="md" data-end="1112" data-start="1101">`Core 1`</td><td data-col-size="md" data-end="1124" data-start="1112">Default.</td></tr><tr data-end="1184" data-start="1125"><td data-col-size="sm" data-end="1156" data-start="1125">**USB Firmware MSC On Boot**</td><td data-col-size="md" data-end="1169" data-start="1156">`Disabled`</td><td data-col-size="md" data-end="1184" data-start="1169">Not needed.</td></tr><tr data-end="1258" data-start="1185"><td data-col-size="sm" data-end="1208" data-start="1185">**Partition Scheme**</td><td data-col-size="md" data-end="1219" data-start="1208">`Custom`</td><td data-col-size="md" data-end="1258" data-start="1219">Points to the partitions.csv below.</td></tr><tr data-end="1343" data-start="1259"><td data-col-size="sm" data-end="1271" data-start="1259">**PSRAM**</td><td data-col-size="md" data-end="1289" data-start="1271">**`QPI PSRAM`**</td><td data-col-size="md" data-end="1343" data-start="1289">The S3 module on this board uses **QPI**, not OPI.</td></tr><tr data-end="1419" data-start="1344"><td data-col-size="sm" data-end="1362" data-start="1344">**Upload Mode**</td><td data-col-size="md" data-end="1386" data-start="1362">`UART / Hardware CDC`</td><td data-col-size="md" data-end="1419" data-start="1386">For standard USB programming.</td></tr><tr data-end="1520" data-start="1420"><td data-col-size="sm" data-end="1439" data-start="1420">**Upload Speed**</td><td data-col-size="md" data-end="1497" data-start="1439">`115200` *(safe)* or `921600` *(faster, stable cables)*</td><td data-col-size="md" data-end="1520" data-start="1497">Start conservative.</td></tr><tr data-end="1620" data-start="1521"><td data-col-size="sm" data-end="1536" data-start="1521">**USB Mode**</td><td data-col-size="md" data-end="1589" data-start="1536">`Hardware CDC and JTAG` *(or just `Hardware CDC`)*</td><td data-col-size="md" data-end="1620" data-start="1589">Either works; CDC required.</td></tr><tr data-end="1665" data-start="1621"><td data-col-size="sm" data-end="1639" data-start="1621">**Zigbee Mode**</td><td data-col-size="md" data-end="1652" data-start="1639">`Disabled`</td><td data-col-size="md" data-end="1665" data-start="1652">Not used.</td></tr><tr data-end="1722" data-start="1666"><td data-col-size="sm" data-end="1683" data-start="1666">**Programmer**</td><td data-col-size="md" data-end="1695" data-start="1683">`Default`</td><td data-col-size="md" data-end="1722" data-start="1695">N/A for USB bootloader.</td></tr></tbody></table>

</div></div>---

## Custom Partition Table (place as **partitions.csv**)

<div class="contain-inline-size rounded-2xl relative bg-token-sidebar-surface-primary" id="bkmrk-nvs%2C-data%2C-nvs%2C-0x90"><div class="sticky top-9"><div class="absolute end-0 bottom-0 flex h-9 items-center pe-2"><div class="bg-token-bg-elevated-secondary text-token-text-secondary flex items-center gap-4 rounded-sm px-2 font-sans text-xs">  
</div></div></div><div class="overflow-y-auto p-4" dir="ltr">`nvs,      data, nvs,     0x9000,   0x5000otadata,  data, ota,     0xE000,   0x2000app0,     app,  ota_0,   0x10000,  0x300000app1,     app,  ota_1,   0x310000, 0x300000spiffs,   data, spiffs,  0x610000, 0x9F0000`</div></div>**Layout summary (16 MB flash):**

- **OTA slots:** 2 × 3.0 MB (app0/app1)
- **SPIFFS:** ~9.94 MB (from 0x610000 to end)
- **NVS + otadata:** standard offsets

> In Arduino IDE: **Tools → Partition Scheme → Custom**, and make sure the sketch is using this `partitions.csv` in the project root (or reference it via your boards.txt if you manage boards definitions).

---

## Entering Bootloader (Manual)

1. **Hold `BOOT`**
2. **Tap `RESET (EN)`** while holding `BOOT`
3. **Release `BOOT`** after ~1 s
4. Click **Upload** in Arduino

If uploads hang at:

<div class="contain-inline-size rounded-2xl relative bg-token-sidebar-surface-primary" id="bkmrk-connecting........__"><div class="sticky top-9"><div class="absolute end-0 bottom-0 flex h-9 items-center pe-2"><div class="bg-token-bg-elevated-secondary text-token-text-secondary flex items-center gap-4 rounded-sm px-2 font-sans text-xs">  
</div></div></div><div class="overflow-y-auto p-4" dir="ltr">`Connecting........<span class="hljs-strong">____</span><span class="hljs-emphasis">_.....<span class="hljs-strong">____</span></span>_.....`</div></div>repeat the BOOT/RESET sequence and try again.

---

## Common Recovery / Build Errors

- **`exceeds flash chip size 0x400000`**  
    → Set **Flash Size = 16MB (128Mb)** and use the **Custom** partition above.
- **Random reboots / heap issues** on larger sketches  
    → Ensure **PSRAM = QPI PSRAM** (not Disabled/OPI).
- **Corrupted filesystem or odd boot logs**  
    → Temporarily set **Tools → Erase All Flash Before Sketch Upload → Enabled**, upload once, then set back to Disabled.
- **Hard-brick symptoms** (can’t enter bootloader)  
    → Disconnect external circuits on **GPIO0/EN/TX/RX**, power via USB only, then retry BOOT/RESET sequence.

---

## Optional: Full Flash Erase (last resort)

<div class="contain-inline-size rounded-2xl relative bg-token-sidebar-surface-primary" id="bkmrk-esptool.py---chip-es"><div class="sticky top-9"><div class="absolute end-0 bottom-0 flex h-9 items-center pe-2"><div class="bg-token-bg-elevated-secondary text-token-text-secondary flex items-center gap-4 rounded-sm px-2 font-sans text-xs">  
</div></div></div><div class="overflow-y-auto p-4" dir="ltr">`esptool.py --chip esp32s3 --port COMx erase_flash`</div></div>Then re-upload with the settings above.

</body></html>
