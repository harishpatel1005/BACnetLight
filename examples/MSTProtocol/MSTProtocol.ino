/*
 * BACnetLight - BACnet/MSTP (RS485) Example
 *
 * A BACnet device on an RS485 MS/TP bus. No Ethernet required.
 *
 * Hardware:
 *   ESP32 + RS485 transceiver (MAX485 / MAX3485, or an auto-direction module)
 *
 * Wiring (transceiver -> ESP32):
 *   RO            -> GPIO 16 (RX2)
 *   DI            -> GPIO 17 (TX2)
 *   DE + RE (tied)-> GPIO 4   (auto-direction modules ignore this; any free pin)
 *   A / B         -> RS485 bus (share a common ground with the other devices)
 *
 * Test with YABE: Functions -> Add MSTP port, set the same baud and Max_Master,
 * then discover and read this device.
 *
 * For a combined BACnet/IP + BACnet/MSTP device, see the note at the bottom.
 */

#include <BACnetLight.h>

// --- RS485 / MS/TP settings ---
#define RS485_RX        16
#define RS485_TX        17
#define RS485_DE        4        // DE/RE direction pin (auto-direction modules ignore it)
#define MSTP_MAC        10       // This device's MS/TP MAC (0-127), unique on the bus
#define MSTP_BAUD       38400    // Must match every device on the bus
#define MSTP_MAX_MASTER 127      // Highest MAC the bus polls

BACnetMSTP bacnet;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("BACnetLight - BACnet/MSTP example");

    // The RS485 UART MUST be configured before beginMSTP().
    Serial2.begin(MSTP_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);

    bacnet.setDeviceInfo("BACnetLight", 0, "MSTP-Device", "1.0.2", "1.0.2");
    bacnet.beginMSTP(3000, "MSTP-Device", Serial2,
                     RS485_DE, MSTP_MAC, MSTP_BAUD, MSTP_MAX_MASTER);

    // Objects (visible to any MS/TP client)
    bacnet.addAnalogInput (0, "Zone-Temp",   22.0, BACNET_UNITS_DEGREES_CELSIUS, "Zone temperature");
    bacnet.addAnalogOutput(0, "VAV-Damper",  0.0,  BACNET_UNITS_PERCENT,         "VAV box damper 0-100%");
    bacnet.addBinaryOutput(0, "Fan-Command", false, "AHU fan start/stop");

    Serial.printf("MS/TP up: MAC=%d  Device=3000  baud=%d  objects=%d\n",
                  MSTP_MAC, MSTP_BAUD, bacnet.getObjectCount());
}

void loop() {
    bacnet.loop();   // services MS/TP + BACnet

    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate >= 2000) {
        lastUpdate = millis();

        // Simulate a live sensor reading
        float temp = 21.0 + random(0, 30) / 10.0;
        bacnet.setValue(BACNET_OBJ_ANALOG_INPUT, 0, temp);

        // Read back what a BMS commanded
        float damper = bacnet.getValue(BACNET_OBJ_ANALOG_OUTPUT, 0);
        float fan    = bacnet.getValue(BACNET_OBJ_BINARY_OUTPUT, 0);
        Serial.printf("Temp=%.1f  Damper=%.0f%%  Fan=%s\n",
                      temp, damper, fan > 0.5 ? "ON" : "OFF");
    }
}

/*
 * --- Dual-port (BACnet/IP + BACnet/MSTP) on one device ---
 * Add at the top of the sketch:
 *
 *   #include <SPI.h>
 *   #include <Ethernet.h>
 *   #include <EthernetUdp.h>
 *   EthernetUDP bacnetUdp;
 *   byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
 *   IPAddress ip(192, 168, 1, 200), target(192, 168, 1, 255);
 *
 * In setup(), replace beginMSTP() with:
 *
 *   Ethernet.init(5);
 *   Ethernet.begin(mac, ip);
 *   Serial2.begin(MSTP_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
 *   bacnet.beginDual(3000, "DualPort-Gateway", target, bacnetUdp,
 *                    Serial2, RS485_DE, MSTP_MAC, MSTP_BAUD);
 *
 * And call Ethernet.maintain(); at the end of loop().
 */
