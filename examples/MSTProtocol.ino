/*
 * BACnetLight - BACnet/MSTP (RS485) Example
 * 
 * Runs BACnet over RS485 serial bus using MSTP token-passing protocol.
 * Can also run dual-port: BACnet/IP + BACnet/MSTP simultaneously.
 * 
 * Hardware:
 *   ESP32 + MAX485 RS485 transceiver
 *   Optional: W5500 for dual IP+MSTP mode
 * 
 * Wiring (MAX485 to ESP32):
 *   RO  → GPIO 16 (RX2)
 *   DI  → GPIO 17 (TX2)
 *   DE+RE (tied) → GPIO 4
 *   A/B → RS485 bus
 */

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <BACnetLight.h>

// --- RS485 Pins ---
#define RS485_RX   16
#define RS485_TX   17
#define RS485_DE   4
#define MSTP_MAC   10      // Our MSTP address (0-127)
#define MSTP_BAUD  38400

// --- Ethernet (for dual mode) ---
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 200);
IPAddress bacnetTarget(192, 168, 1, 100);

// Use BACnetMSTP class instead of BACnetLight
BACnetMSTP bacnet;
EthernetUDP bacnetUdp;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("BACnet/MSTP Example");
    Serial.println("====================");

    // Option A: MSTP only (no Ethernet needed)
    // Serial2.begin(MSTP_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    // bacnet.beginMSTP(3000, "MSTP-Device", Serial2, RS485_DE, MSTP_MAC, MSTP_BAUD);

    // Option B: Dual mode - IP + MSTP
    Ethernet.init(5);
    Ethernet.begin(mac, ip);
    delay(2000);
    Serial.print("IP: ");
    Serial.println(Ethernet.localIP());

    Serial2.begin(MSTP_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    bacnet.beginDual(3000, "DualPort-Gateway", bacnetTarget, bacnetUdp,
                     Serial2, RS485_DE, MSTP_MAC, MSTP_BAUD);

    // Add objects (visible on both IP and MSTP)
    bacnet.addAnalogInput(0, "Zone-Temp", 22.0, BACNET_UNITS_DEGREES_CELSIUS,
                          "Zone temperature");
    bacnet.addAnalogOutput(0, "VAV-Damper", 50.0, BACNET_UNITS_PERCENT,
                           "VAV box damper");
    bacnet.addBinaryOutput(0, "Fan-Command", false, "AHU fan start/stop");

    Serial.printf("MSTP MAC: %d, Baud: %d\n", MSTP_MAC, MSTP_BAUD);
    Serial.println("Device accessible via IP and RS485!");
    Serial.println("====================");
}

void loop() {
    bacnet.loop();  // Handles both IP and MSTP

    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate >= 2000) {
        lastUpdate = millis();

        float temp = 21.0 + random(0, 30) / 10.0;
        bacnet.setValue(BACNET_OBJ_ANALOG_INPUT, 0, temp);

        float damper = bacnet.getValue(BACNET_OBJ_ANALOG_OUTPUT, 0);
        float fan = bacnet.getValue(BACNET_OBJ_BINARY_OUTPUT, 0);

        Serial.printf("Temp=%.1f  Damper=%.0f%%  Fan=%s\n",
                      temp, damper, fan > 0.5 ? "ON" : "OFF");
    }

    Ethernet.maintain();
}
