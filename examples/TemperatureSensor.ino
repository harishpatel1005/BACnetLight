/*
 * BACnetLight - HVAC Sensor + Controller Example
 * 
 * Demonstrates all object types including AO/BO with priority arrays:
 *   AI:0 - Room Temperature (read-only sensor)
 *   AI:1 - Humidity (read-only sensor)
 *   AV:0 - Temperature Setpoint (writable)
 *   AO:0 - Damper Position (commandable with priority array)
 *   BI:0 - Occupancy Sensor (read-only)
 *   BO:0 - Fan Relay (commandable with priority array)
 *   BV:0 - Alarm Active (read-only status)
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

// Write callback with priority support
bool onBACnetWrite(BACnetObject *obj, float value, uint8_t priority) {
    Serial.printf("WRITE: %s = %.2f (priority %d)\n", obj->name, value, priority);

    // Reject setpoints outside valid range
    if (obj->type == BACNET_OBJ_ANALOG_VALUE && (value < 15.0 || value > 30.0)) {
        Serial.println("  Rejected: setpoint out of range (15-30)");
        return false;
    }

    // Reject damper position outside 0-100%
    if (obj->type == BACNET_OBJ_ANALOG_OUTPUT && (value < 0.0 || value > 100.0)) {
        Serial.println("  Rejected: damper out of range (0-100)");
        return false;
    }

    return true;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("HVAC Controller - BACnetLight");
    Serial.println("=============================");

    Ethernet.init(5);
    Ethernet.begin(mac, ip);
    delay(2000);
    Serial.print("IP: ");
    Serial.println(Ethernet.localIP());

    bacnet.begin(5678, "HVAC-Controller-01", bacnetTarget, bacnetUdp);
    bacnet.setDeviceInfo("MyCompany", 0, "HVAC-Ctrl-v2", "2.0.0", "2.0.0");

    // Analog Inputs (read-only sensors)
    bacnet.addAnalogInput(0, "Room-Temp", 22.0, BACNET_UNITS_DEGREES_CELSIUS,
                          "Room temperature sensor");
    bacnet.addAnalogInput(1, "Humidity", 45.0, BACNET_UNITS_PERCENT,
                          "Room humidity sensor");

    // Analog Value (writable setpoint)
    bacnet.addAnalogValue(0, "Temp-Setpoint", 23.0, BACNET_UNITS_DEGREES_CELSIUS,
                          true, "Temperature setpoint");

    // Analog Output (commandable with 16-level priority array)
    bacnet.addAnalogOutput(0, "Damper-Position", 0.0, BACNET_UNITS_PERCENT,
                           "Supply air damper 0-100%");

    // Binary Input (read-only)
    bacnet.addBinaryInput(0, "Occupancy", false, "PIR occupancy sensor");

    // Binary Output (commandable with priority array)
    bacnet.addBinaryOutput(0, "Fan-Relay", false, "Supply fan relay");

    // Binary Value (read-only status)
    bacnet.addBinaryValue(0, "Alarm-Active", false, false, "High temp alarm");

    // Set COV increments
    bacnet.setCOVIncrement(BACNET_OBJ_ANALOG_INPUT, 0, 0.5);  // Notify on 0.5°C change
    bacnet.setCOVIncrement(BACNET_OBJ_ANALOG_INPUT, 1, 2.0);  // Notify on 2% change

    bacnet.onWrite(onBACnetWrite);

    Serial.printf("Objects: %d\n", bacnet.getObjectCount());
    Serial.println("Ready!");
}

void loop() {
    bacnet.loop();

    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate >= 2000) {
        lastUpdate = millis();

        // Simulate sensors
        float temp = 21.0 + random(0, 40) / 10.0;
        float hum = 40.0 + random(0, 20) / 10.0;
        bool occupied = (random(0, 10) > 3);

        bacnet.setValue(BACNET_OBJ_ANALOG_INPUT, 0, temp);
        bacnet.setValue(BACNET_OBJ_ANALOG_INPUT, 1, hum);
        bacnet.setValue(BACNET_OBJ_BINARY_INPUT, 0, occupied ? 1.0 : 0.0);

        // Simple control logic
        float setpoint = bacnet.getValue(BACNET_OBJ_ANALOG_VALUE, 0);
        bool alarm = (temp > setpoint + 5.0);
        bacnet.setValue(BACNET_OBJ_BINARY_VALUE, 0, alarm ? 1.0 : 0.0);

        // Read what the BMS commanded for damper and fan
        float damper = bacnet.getValue(BACNET_OBJ_ANALOG_OUTPUT, 0);
        float fan = bacnet.getValue(BACNET_OBJ_BINARY_OUTPUT, 0);

        Serial.printf("T=%.1f H=%.0f%% SP=%.1f Dmp=%.0f%% Fan=%s Occ=%s Alm=%s\n",
                      temp, hum, setpoint, damper,
                      fan > 0.5 ? "ON" : "OFF",
                      occupied ? "Y" : "N",
                      alarm ? "Y" : "N");
    }

    Ethernet.maintain();
}
