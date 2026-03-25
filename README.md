# BACnetLight

**A lightweight BACnet/IP & BACnet/MSTP library for ESP32**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Arduino](https://img.shields.io/badge/Arduino-Compatible-teal.svg)](https://www.arduino.cc/)
[![ESP32](https://img.shields.io/badge/ESP32-Supported-green.svg)](https://www.espressif.com/)
[![Version](https://img.shields.io/badge/version-1.0.0-orange.svg)](https://github.com/harishpatel1005/BACnetLight)

BACnetLight turns an ESP32 into a fully functional BACnet device -- discoverable, readable, writable, and capable of pushing live updates via COV -- with minimal code. It supports both **BACnet/IP** (Ethernet/WiFi) and **BACnet/MSTP** (RS485), covering the vast majority of real-world building automation use cases.

This repository is being prepared as the **first public release** of BACnetLight.

## At a Glance

- Lightweight BACnet library designed specifically for ESP32
- Supports BACnet/IP, BACnet/MSTP, and dual-port deployments
- Includes object modeling, WriteProperty, ReadPropertyMultiple, COV, and priority arrays
- Ships with example sketches for common device and gateway patterns
- Example sketches have been compiled successfully for this first public release

## Contents

- [Why BACnetLight?](#why-bacnetlight)
- [Quick Start](#quick-start)
- [Requirements](#requirements)
- [Installation](#installation)
- [API Reference](#api-reference)
- [Examples](#examples)
- [Configuration](#configuration)
- [Testing with YABE](#testing-with-yabe)
- [Hardware Compatibility](#hardware-compatibility)
- [Architecture](#architecture)
- [Security Considerations](#security-considerations)
- [Limitations](#limitations)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

## Why BACnetLight?

The standard open-source BACnet stack (200+ files, 80-120KB RAM) was designed for Linux desktops. **It doesn't fit on a microcontroller.** BACnetLight gives you production-grade BACnet functionality that actually runs on an ESP32:

| Feature | bacnet-stack | BACnetLight |
|---------|-------------|-------------|
| Source files | 200+ | 3 files (~1200 lines) |
| RAM usage | ~80-120 KB | ~8-12 KB |
| Setup time | Hours of porting | `bacnet.begin()` |
| Arduino compatible | No | Yes |
| BACnet/IP | Yes | Yes |
| BACnet/MSTP (RS485) | Yes | Yes |
| WhoIs / IAm | Yes | Yes |
| ReadProperty | Yes | Yes |
| ReadPropertyMultiple | Yes | Yes |
| WriteProperty | Yes | Yes |
| COV Subscriptions (confirmed & unconfirmed) | Yes | Yes |
| Priority Arrays (AO/BO) | Yes | Yes |
| YABE compatible | Yes | Yes |

## Quick Start

```cpp
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <BACnetLight.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 200);

BACnetLight bacnet;
EthernetUDP bacnetUdp;

void setup() {
    Ethernet.init(5);
    Ethernet.begin(mac, ip);

    bacnet.begin(1234, "My-Device", IPAddress(192,168,1,255), bacnetUdp);
    bacnet.addAnalogValue(0, "Temperature", 22.5, BACNET_UNITS_DEGREES_CELSIUS);
}

void loop() {
    bacnet.loop();
    bacnet.setValue(BACNET_OBJ_ANALOG_VALUE, 0, readSensor());
}
```

If you want a complete sketch with serial logging and startup flow, start with [`examples/BasicDevice.ino`](examples/BasicDevice.ino).

## Requirements

### Software

- Arduino IDE 2.x or another Arduino-compatible build environment
- ESP32 board support package
- Arduino `Ethernet` library for BACnet/IP over Ethernet
- A UDP transport such as `EthernetUDP` or `WiFiUDP`

### Hardware

- **BACnet/IP:** ESP32 plus Ethernet hardware such as W5500, or ESP32 WiFi when using `WiFiUDP`
- **BACnet/MSTP:** ESP32 plus an RS485 transceiver such as MAX485 or MAX3485

## Features

### BACnet Services
| Service | Description |
|---------|-------------|
| **WhoIs / IAm** | Automatic device discovery on the network |
| **ReadProperty** | Clients read any property of any object |
| **ReadPropertyMultiple** | Read multiple properties in one request (efficient) |
| **WriteProperty** | Clients write to writable/commandable objects |
| **SubscribeCOV** | Clients subscribe; device pushes updates on value changes |
| **Error Responses** | Proper BACnet error codes for all error conditions |

### Object Types
| Type | Code | Writable | Priority Array | Use Case |
|------|------|----------|---------------|----------|
| Analog Input (AI) | `BACNET_OBJ_ANALOG_INPUT` | No | No | Sensor readings |
| Analog Output (AO) | `BACNET_OBJ_ANALOG_OUTPUT` | Yes | **Yes (16-level)** | Control outputs (dampers, valves) |
| Analog Value (AV) | `BACNET_OBJ_ANALOG_VALUE` | Optional | No | Setpoints, calculated values |
| Binary Input (BI) | `BACNET_OBJ_BINARY_INPUT` | No | No | Digital sensors (switches, contacts) |
| Binary Output (BO) | `BACNET_OBJ_BINARY_OUTPUT` | Yes | **Yes (16-level)** | Relay control (fans, pumps) |
| Binary Value (BV) | `BACNET_OBJ_BINARY_VALUE` | Optional | No | Status flags, modes |

### Transport Layers
| Transport | Class | Hardware |
|-----------|-------|----------|
| **BACnet/IP** | `BACnetLight` | Any Ethernet (W5500, ENC28J60) or WiFi (WiFiUDP) |
| **BACnet/MSTP** | `BACnetMSTP` | RS485 transceiver (MAX485, MAX3485) |
| **Dual (IP + MSTP)** | `BACnetMSTP` | Ethernet + RS485 simultaneously |

## Installation

### Manual
1. Download/clone this repository
2. Copy `BACnetLight/` to `Documents/Arduino/libraries/`
3. Restart Arduino IDE

### Arduino Library Manager

Use Library Manager after BACnetLight has been accepted there:

1. **Sketch -> Include Library -> Manage Libraries**
2. Search **"BACnetLight"**
3. Click **Install**

## API Reference

### Initialization

```cpp
BACnetLight bacnet;     // BACnet/IP only
BACnetMSTP bacnet;      // BACnet/MSTP (also supports IP)
WiFiUDP wifiUdp;
EthernetUDP ethUdp;

// Start BACnet/IP over WiFi
bacnet.begin(1234, "Device-Name", IPAddress(192,168,1,255), wifiUdp);

// Start BACnet/IP over Ethernet
bacnet.begin(1234, "Device-Name", IPAddress(192,168,1,255), ethUdp);

// Start BACnet/MSTP only (no IP stack needed)
Serial2.begin(38400, SERIAL_8N1, RX_PIN, TX_PIN);
// beginMSTP() expects the serial port to already be configured
bacnet.beginMSTP(1234, "Device-Name", Serial2, dePin, macAddress, 38400, 127);

// Start both simultaneously (dual-port)
Serial2.begin(38400, SERIAL_8N1, RX_PIN, TX_PIN);
bacnet.beginDual(1234, "Dual-Device", targetIP, ethUdp, Serial2, dePin, macAddr, 38400);

// Set device metadata
bacnet.setDeviceInfo("Vendor", 0, "Model", "1.0.0", "1.0.0");
```

### Typical Setup Flow

1. Configure your network transport or serial port.
2. Start BACnet with `begin()`, `beginMSTP()`, or `beginDual()`.
3. Add one or more BACnet objects.
4. Call `loop()` continuously from the Arduino main loop.
5. Update values with `setValue()` or react to external writes with `onWrite()`.

### Creating Objects

```cpp
// Read-only sensors
bacnet.addAnalogInput(0, "Temperature", 22.0, BACNET_UNITS_DEGREES_CELSIUS, "Room temp");
bacnet.addBinaryInput(0, "Door-Contact", false, "Magnetic reed switch");

// Writable values
bacnet.addAnalogValue(0, "Setpoint", 23.0, BACNET_UNITS_DEGREES_CELSIUS, true, "Temp setpoint");
bacnet.addBinaryValue(0, "Mode", false, true, "Auto/Manual mode");

// Commandable outputs (with 16-level priority array)
bacnet.addAnalogOutput(0, "Damper", 0.0, BACNET_UNITS_PERCENT, "VAV damper 0-100%");
bacnet.addBinaryOutput(0, "Fan", false, "Supply fan relay");
```

### Reading & Writing Values

```cpp
// Update from your sensor/Modbus/etc.
bacnet.setValue(BACNET_OBJ_ANALOG_INPUT, 0, 23.5);

// Read current value
float temp = bacnet.getValue(BACNET_OBJ_ANALOG_INPUT, 0);

// Get direct object pointer
BACnetObject *obj = bacnet.getObject(BACNET_OBJ_ANALOG_INPUT, 0);

// Get total object count
uint8_t count = bacnet.getObjectCount();

// Command with priority (for AO/BO, priorities 1-16 where 1 is highest)
bacnet.commandObject(BACNET_OBJ_ANALOG_OUTPUT, 0, 75.0, 8);  // Priority 8
bacnet.relinquish(BACNET_OBJ_ANALOG_OUTPUT, 0, 8);            // Release priority 8
```

### COV (Change of Value)

BACnetLight supports both **unconfirmed** and **confirmed** COV notifications. The client chooses which it wants when it sends a SubscribeCOV request. For confirmed subscriptions the library tracks each in-flight notification, retries on timeout, and cancels the subscription after repeated failures.

```cpp
// Set how much change triggers a notification
bacnet.setCOVIncrement(BACNET_OBJ_ANALOG_INPUT, 0, 0.5);  // 0.5 unit change
// Binary objects trigger on any state change automatically

// Optional: get notified locally when COV fires
bacnet.onCOV([](BACnetObject *obj) {
    Serial.printf("COV: %s = %.2f\n", obj->name, obj->presentValue);
});
```

Confirmed COV retry behaviour is controlled by two compile-time defines (see Configuration).

### Write Callback

```cpp
bacnet.onWrite([](BACnetObject *obj, float value, uint8_t priority) {
    Serial.printf("Write: %s = %.2f @ priority %d\n", obj->name, value, priority);
    if (value > 100.0) return false;  // Reject
    return true;                       // Accept
});
```

### Engineering Units

```cpp
BACNET_UNITS_DEGREES_CELSIUS       // 62
BACNET_UNITS_DEGREES_FAHRENHEIT    // 64
BACNET_UNITS_PERCENT               // 98
BACNET_UNITS_PASCALS               // 53
BACNET_UNITS_KILOPASCALS           // 54
BACNET_UNITS_WATTS                 // 47
BACNET_UNITS_KILOWATTS             // 48
BACNET_UNITS_AMPERES               // 3
BACNET_UNITS_MILLIAMPERES          // 2
BACNET_UNITS_VOLTS                 // 5
BACNET_UNITS_HERTZ                 // 27
BACNET_UNITS_RPM                   // 104
BACNET_UNITS_LITERS_PER_SECOND     // 85
BACNET_UNITS_CUBIC_METERS_PER_HOUR // 135
BACNET_UNITS_PPM                   // 96
BACNET_UNITS_NO_UNITS              // 95
```

## Examples

| Example | Description |
|---------|-------------|
| **BasicDevice** | Simplest BACnet device -- one AV, 20 lines |
| **TemperatureSensor** | HVAC controller with AI, AO, AV, BI, BO, BV + write validation |
| **COVExample** | COV subscriptions with different increments per object |
| **MSTProtocol** | BACnet/MSTP over RS485, dual-port IP+MSTP |
| **ModbusBACnetGateway** | Modbus RTU -> BACnet/IP gateway template (simulated data, ready for real RS485) |

Recommended order for first-time users:

1. `BasicDevice`
2. `TemperatureSensor`
3. `COVExample`
4. `MSTProtocol`
5. `ModbusBACnetGateway`

## Configuration

Override these before `#include <BACnetLight.h>`:

```cpp
#define BACNET_MAX_OBJECTS           32    // Max objects per device
#define BACNET_MAX_COV_SUBSCRIPTIONS 16    // Max simultaneous COV subscribers
#define BACNET_MAX_NAME_LEN          32    // Object name length
#define BACNET_MAX_DESC_LEN          64    // Description length
#define BACNET_BUF_SIZE              512   // Packet buffer size
#define BACNET_COV_TIMEOUT_MS        5000  // ms before retrying a confirmed COV notification
#define BACNET_COV_MAX_RETRIES       2     // Subscription cancelled after this many timeouts
```

## Notes

- BACnet object identifiers must be unique per device. Adding the same object type/instance twice now fails and returns `nullptr`.
- For BACnet/MSTP, call `Serial.begin(...)` on the chosen hardware port before `beginMSTP()` or `beginDual()`. The `baud` argument must match that serial configuration.

## Testing with YABE

1. Install [YABE](https://sourceforge.net/projects/yetanotherbacnetexplorer/)
2. **Functions -> Communication Channel**
3. Set BACnet/IP port to `0xBAC0`, select your network adapter
4. Click **Start** -- your device appears in the tree
5. Click objects to read properties
6. Right-click -> **Subscribe** to test COV
7. Right-click writable objects -> **Write** to test WriteProperty

## Hardware Compatibility

**Tested:** ESP32 + W5500 (SPI Ethernet), ESP32 + MAX485 (RS485)

**Should work:** ESP32 + ENC28J60, ESP32 WiFi (WiFiUDP)

## Architecture

```
BACnetLight (base class - BACnet/IP)
+-- Object storage (AI, AO, AV, BI, BO, BV)
+-- Priority array engine (for AO/BO)
+-- COV subscription manager
|   +-- Unconfirmed notifications (fire-and-forget)
|   \-- Confirmed notifications (pending table, ACK/Error handling, retry)
+-- BACnet encoding/decoding engine
+-- Service handlers (WhoIs, RP, RPM, WP, SubscribeCOV)
\-- UDP transport (EthernetUDP / WiFiUDP)

BACnetMSTP (extends BACnetLight - adds RS485)
+-- MSTP token-passing state machine
+-- RS485 frame encoding/decoding with CRC
+-- Frame receive + transmit with direction control
\-- Poll-for-master discovery
```

## Repository Layout

- `BACnetLight.h` - public API, constants, object structures, and class declarations
- `BACnetLight.cpp` - BACnet/IP implementation, object handling, service handlers, and COV logic
- `BACnetMSTP.cpp` - BACnet/MSTP transport and RS485 state machine
- `examples/` - ready-to-build example sketches
- `library.properties` - Arduino library metadata
- `keywords.txt` - Arduino IDE syntax highlighting hints

## Security Considerations

BACnet/IP transmits plain-text UDP with no authentication or encryption. BACnet/MSTP uses unencrypted RS485. This is standard for the BACnet protocol, but you should:

- Deploy BACnet devices on an isolated building automation network (VLAN)
- Do not expose BACnet UDP port (47808) to the internet
- Use firewalls to restrict which hosts can reach the device

## Limitations

- No segmentation (responses limited to ~480 bytes; Segmentation_Supported reports `no-segmentation`)
- No alarm/event reporting
- No scheduling objects
- No trend logging
- No BACnet Secure Connect (BACnet/SC)
- No routing (routed NPDU messages with DNET/SNET are parsed but not forwarded)
- MSTP: simplified token-passing (functional but not BTL-certified)
- Object/device names are truncated to 31 characters (`BACNET_MAX_NAME_LEN - 1`), descriptions to 63 characters (`BACNET_MAX_DESC_LEN - 1`)

## Roadmap

- [ ] Multi-State Value/Input objects
- [ ] Native ESP32 Ethernet (no W5500)
- [ ] Alarm & event reporting
- [ ] BACnet router (IP <-> MSTP bridging)

## Contributing

Contributions are welcome.

- Use issues for bugs, feature requests, and API discussions
- Keep examples focused and easy to compile
- Update README snippets when public API or setup behavior changes
- Prefer small pull requests with one clear purpose

## License

[MIT](LICENSE) -- Free for personal and commercial use.
