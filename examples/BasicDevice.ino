/*
 * BACnetLight - Basic Device Example
 * 
 * Simplest possible BACnet device: one Analog Value.
 * Hardware: ESP32 + W5500 Ethernet
 */

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <BACnetLight.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 200);
IPAddress bacnetTarget(192, 168, 1, 255);  // Subnet broadcast for BACnet discovery

BACnetLight bacnet;
EthernetUDP bacnetUdp;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Ethernet.init(5);
    Ethernet.begin(mac, ip);
    delay(2000);
    Serial.print("IP: ");
    Serial.println(Ethernet.localIP());

    if (!bacnet.begin(1234, "My-First-BACnet", bacnetTarget, bacnetUdp)) {
        Serial.println("ERROR: BACnet init failed!");
        while (1) delay(1000);
    }
    bacnet.addAnalogValue(0, "Temperature", 22.5, BACNET_UNITS_DEGREES_CELSIUS);

    Serial.println("BACnet ready!");
}

void loop() {
    bacnet.loop();

    static unsigned long last = 0;
    if (millis() - last >= 5000) {
        last = millis();
        float temp = 20.0 + random(0, 50) / 10.0;
        bacnet.setValue(BACNET_OBJ_ANALOG_VALUE, 0, temp);
        Serial.printf("Temp: %.1f C\n", temp);
    }
}
