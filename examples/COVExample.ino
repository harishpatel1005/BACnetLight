/*
 * BACnetLight - COV (Change of Value) Example
 * 
 * Demonstrates COV subscriptions:
 * - BACnet clients subscribe to objects
 * - ESP32 automatically pushes updates when values change
 * - No constant polling needed = less network traffic
 * 
 * How to test with YABE:
 * 1. Discover this device
 * 2. Right-click an object → Subscribe
 * 3. Watch the Subscriptions panel update automatically
 * 
 * Hardware: ESP32 + W5500 Ethernet
 */

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <BACnetLight.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 200);
IPAddress bacnetTarget(192, 168, 1, 100);

BACnetLight bacnet;
EthernetUDP bacnetUdp;

// Called whenever a COV notification is sent
void onCOVNotification(BACnetObject *obj) {
    Serial.printf("COV fired: %s = %.2f\n", obj->name, obj->presentValue);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("BACnet COV Example");
    Serial.println("==================");

    Ethernet.init(5);
    Ethernet.begin(mac, ip);
    delay(2000);
    Serial.print("IP: ");
    Serial.println(Ethernet.localIP());

    bacnet.begin(2000, "COV-Demo-Device", bacnetTarget, bacnetUdp);

    // Analog sensor - COV triggers on 1.0 degree change
    bacnet.addAnalogValue(0, "Temperature", 22.0, BACNET_UNITS_DEGREES_CELSIUS,
                          false, "Temp with 1.0 COV increment");
    bacnet.setCOVIncrement(BACNET_OBJ_ANALOG_VALUE, 0, 1.0);

    // Another analog - COV triggers on 0.5 change
    bacnet.addAnalogValue(1, "Pressure", 101.3, BACNET_UNITS_KILOPASCALS,
                          false, "Pressure with 0.5 COV increment");
    bacnet.setCOVIncrement(BACNET_OBJ_ANALOG_VALUE, 1, 0.5);

    // Binary - COV triggers on any state change
    bacnet.addBinaryValue(0, "Door-Contact", false, false,
                          "COV on every toggle");

    bacnet.onCOV(onCOVNotification);

    Serial.println("Subscribe to objects in YABE to see COV in action!");
    Serial.println("==================");
}

float simTemp = 22.0;
float simPressure = 101.3;
bool doorOpen = false;

void loop() {
    bacnet.loop();

    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate >= 3000) {
        lastUpdate = millis();

        // Slowly drift temperature (COV fires when change >= 1.0)
        simTemp += random(-5, 6) / 10.0;
        if (simTemp < 18.0) simTemp = 18.0;
        if (simTemp > 30.0) simTemp = 30.0;

        // Small pressure changes (COV fires when change >= 0.5)
        simPressure += random(-3, 4) / 10.0;

        // Random door toggles (COV fires on every change)
        if (random(0, 10) > 7) doorOpen = !doorOpen;

        bacnet.setValue(BACNET_OBJ_ANALOG_VALUE, 0, simTemp);
        bacnet.setValue(BACNET_OBJ_ANALOG_VALUE, 1, simPressure);
        bacnet.setValue(BACNET_OBJ_BINARY_VALUE, 0, doorOpen ? 1.0 : 0.0);

        Serial.printf("Temp=%.1f  Press=%.1f  Door=%s\n",
                      simTemp, simPressure, doorOpen ? "OPEN" : "CLOSED");
    }

    Ethernet.maintain();
}
