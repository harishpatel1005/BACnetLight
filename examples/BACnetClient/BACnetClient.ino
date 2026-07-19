/*
 * BACnetClient - BACnetLight as a BACnet/IP client / master
 *
 * Demonstrates the client role added in BACnetLight 2.0.0:
 *   1. Broadcast Who-Is to discover devices on the field network
 *   2. Read a property from a discovered device
 *   3. Write a property to a device
 *
 * Hardware: ESP32 + W5500 SPI Ethernet.
 *
 * Wiring (default): W5500 CS=GPIO5, SCK=18, MISO=19, MOSI=23.
 * Put the ESP and your BACnet devices on the same subnet; set BROADCAST_IP
 * to that subnet's broadcast address.
 */

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <BACnetLight.h>

// ---- Network config (edit for your field network) ----
byte      mac[]      = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress espIP(192, 168, 1, 50);          // this ESP on the field network
IPAddress BROADCAST_IP(192, 168, 1, 255);  // subnet broadcast for Who-Is
const uint8_t W5500_CS = 5;

// ---- The remote device/object we will read + write (edit to match a real device) ----
IPAddress TARGET_IP(192, 168, 1, 60);
const uint16_t TARGET_OBJ_TYPE = BACNET_OBJ_ANALOG_VALUE;
const uint32_t TARGET_INSTANCE = 0;

BACnetLight bacnet;
EthernetUDP bacnetUdp;

void setup() {
    Serial.begin(115200);
    delay(300);

    Ethernet.init(W5500_CS);
    Ethernet.begin(mac, espIP);
    delay(500);
    Serial.print("ESP IP: ");
    Serial.println(Ethernet.localIP());

    // deviceInstance here is our own id; targetIP is the broadcast address used for Who-Is.
    bacnet.begin(999, "ESP-Master", BROADCAST_IP, bacnetUdp);

    // ---- 1) Discover ----
    Serial.println("Sending Who-Is...");
    bacnet.sendWhoIs();
    unsigned long t = millis();
    while (millis() - t < 1500) bacnet.loop();   // collect I-Am replies

    uint8_t n = bacnet.getDiscoveredCount();
    Serial.print("Discovered devices: ");
    Serial.println(n);
    for (uint8_t i = 0; i < n; i++) {
        BACnetDevice d = bacnet.getDiscoveredDevice(i);
        Serial.print("  id=");    Serial.print(d.deviceId);
        Serial.print("  ip=");    Serial.print(d.ip);
        Serial.print("  vendor="); Serial.println(d.vendorId);
    }

    // ---- 2) Read present-value ----
    BACnetValue v;
    if (bacnet.readProperty(TARGET_IP, TARGET_OBJ_TYPE, TARGET_INSTANCE,
                            BACNET_PROP_PRESENT_VALUE, -1, v)) {
        Serial.print("present-value = ");
        switch (v.kind) {
            case BACNET_VAL_REAL:       Serial.println(v.realValue); break;
            case BACNET_VAL_UNSIGNED:   Serial.println(v.unsignedValue); break;
            case BACNET_VAL_ENUMERATED: Serial.println(v.unsignedValue); break;
            case BACNET_VAL_BOOLEAN:    Serial.println(v.boolValue); break;
            case BACNET_VAL_STRING:     Serial.println(v.stringValue); break;
            default:                    Serial.println("(other)"); break;
        }
    } else {
        Serial.print("read failed (err class ");
        Serial.print(v.errorClass); Serial.print(", code ");
        Serial.print(v.errorCode);  Serial.println(")");
    }

    // ---- 3) Read the object name ----
    BACnetValue name;
    if (bacnet.readProperty(TARGET_IP, TARGET_OBJ_TYPE, TARGET_INSTANCE,
                            BACNET_PROP_OBJECT_NAME, -1, name)
        && name.kind == BACNET_VAL_STRING) {
        Serial.print("object-name = ");
        Serial.println(name.stringValue);
    }

    // ---- 4) Write present-value (only works on a writable/commandable object) ----
    if (bacnet.writePropertyReal(TARGET_IP, TARGET_OBJ_TYPE, TARGET_INSTANCE,
                                 BACNET_PROP_PRESENT_VALUE, 42.0f)) {
        Serial.println("write accepted");
    } else {
        Serial.println("write failed / not writable");
    }
}

void loop() {
    // Keep servicing the socket (also lets this node answer Who-Is as a device).
    bacnet.loop();
}
