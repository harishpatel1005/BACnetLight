/*
 * BACnetLight - Modbus RTU to BACnet/IP Gateway Template
 *
 * Demonstrates how to bridge Modbus holding registers to BACnet
 * objects over Ethernet with COV support. Ships with simulated
 * register data so you can test without RS485 hardware.
 *
 * To connect real Modbus RTU devices:
 * 1. Install the ModbusMaster library (Arduino Library Manager)
 * 2. Uncomment the ModbusMaster sections below
 * 3. Wire a MAX485 transceiver to your ESP32
 *
 * Hardware: ESP32 + W5500 (HSPI) + MAX485 (optional for real Modbus)
 */

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <BACnetLight.h>
// #include <ModbusMaster.h>  // Uncomment for real Modbus

// --- W5500 Pins (HSPI) ---
#define ETH_CS   18
#define ETH_RST  32
#define ETH_SCK  14
#define ETH_MISO 33
#define ETH_MOSI 13

// --- MAX485 Pins (uncomment for real Modbus) ---
// #define RS485_RX   16
// #define RS485_TX   17
// #define RS485_DE   4
// #define MODBUS_SLAVE_ID  1
// #define MODBUS_BAUD      9600

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 200);
IPAddress bacnetTarget(192, 168, 1, 255);  // Subnet broadcast for BACnet discovery

BACnetLight bacnet;
EthernetUDP bacnetUdp;
// ModbusMaster modbus;  // Uncomment for real Modbus

// Simulated registers
uint16_t holdingRegs[3];
unsigned long lastCounterUpdate = 0;
unsigned long lastToggleUpdate = 0;

void w5500_init() {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(ETH_CS, LOW);
    SPI.transfer(0x00); SPI.transfer(0x00);
    SPI.transfer(0x04); SPI.transfer(0x80);
    digitalWrite(ETH_CS, HIGH);
    SPI.endTransaction();
    delay(200);
}

// Uncomment for real Modbus:
// void preTransmission()  { digitalWrite(RS485_DE, HIGH); }
// void postTransmission() { digitalWrite(RS485_DE, LOW); }

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("========================================");
    Serial.println("  Modbus-to-BACnet Gateway v1.0");
    Serial.println("  Using BACnetLight Library");
    Serial.println("========================================");

    // Init W5500
    pinMode(ETH_CS, OUTPUT); digitalWrite(ETH_CS, HIGH);
    pinMode(ETH_RST, OUTPUT);
    digitalWrite(ETH_RST, LOW); delay(500);
    digitalWrite(ETH_RST, HIGH); delay(500);
    SPI.begin(ETH_SCK, ETH_MISO, ETH_MOSI, ETH_CS);
    w5500_init();
    Ethernet.init(ETH_CS);
    Ethernet.begin(mac, ip);
    delay(2000);
    Serial.print("IP: ");
    Serial.println(Ethernet.localIP());

    // Init Modbus (uncomment for real RS485)
    // pinMode(RS485_DE, OUTPUT); digitalWrite(RS485_DE, LOW);
    // Serial2.begin(MODBUS_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    // modbus.begin(MODBUS_SLAVE_ID, Serial2);
    // modbus.preTransmission(preTransmission);
    // modbus.postTransmission(postTransmission);

    // Init BACnet
    if (!bacnet.begin(1234, "Modbus-BACnet-GW", bacnetTarget, bacnetUdp)) {
        Serial.println("ERROR: BACnet init failed!");
        while (1) delay(1000);
    }
    bacnet.setDeviceInfo("DIY-ESP32", 0, "MB-BN-GW-v1", "1.0.0", "1.0.0");

    // Map Modbus registers -> BACnet objects
    bacnet.addAnalogValue(0, "HR0-Static", 250.0,
                          BACNET_UNITS_NO_UNITS, false,
                          "Static register = 250");

    bacnet.addAnalogValue(1, "HR1-Counter", 0.0,
                          BACNET_UNITS_NO_UNITS, false,
                          "Counter increments every 2s");

    bacnet.addBinaryValue(0, "HR2-Toggle", false, false,
                          "Toggles every 3s");

    // Set COV increments
    bacnet.setCOVIncrement(BACNET_OBJ_ANALOG_VALUE, 0, 1.0);
    bacnet.setCOVIncrement(BACNET_OBJ_ANALOG_VALUE, 1, 1.0);

    // Init simulated registers
    holdingRegs[0] = 250;
    holdingRegs[1] = 0;
    holdingRegs[2] = 0;

    Serial.println("Gateway running!");
    Serial.println("========================================");
}

void loop() {
    bacnet.loop();

    unsigned long now = millis();

    // === SIMULATED DATA ===
    if (now - lastCounterUpdate >= 2000) {
        lastCounterUpdate = now;
        holdingRegs[1]++;
        if (holdingRegs[1] > 9999) holdingRegs[1] = 0;
    }
    if (now - lastToggleUpdate >= 3000) {
        lastToggleUpdate = now;
        holdingRegs[2] = !holdingRegs[2];
    }

    // === REAL MODBUS (uncomment when RS485 connected) ===
    // static unsigned long lastModbus = 0;
    // if (now - lastModbus >= 1000) {
    //     lastModbus = now;
    //     uint8_t result = modbus.readHoldingRegisters(0, 3);
    //     if (result == modbus.ku8MBSuccess) {
    //         holdingRegs[0] = modbus.getResponseBuffer(0);
    //         holdingRegs[1] = modbus.getResponseBuffer(1);
    //         holdingRegs[2] = modbus.getResponseBuffer(2);
    //     } else {
    //         Serial.printf("Modbus error: 0x%02X\n", result);
    //     }
    // }

    // Push to BACnet
    bacnet.setValue(BACNET_OBJ_ANALOG_VALUE, 0, (float)holdingRegs[0]);
    bacnet.setValue(BACNET_OBJ_ANALOG_VALUE, 1, (float)holdingRegs[1]);
    bacnet.setValue(BACNET_OBJ_BINARY_VALUE, 0, holdingRegs[2] ? 1.0 : 0.0);

    // Status
    static unsigned long lastPrint = 0;
    if (now - lastPrint >= 5000) {
        lastPrint = now;
        Serial.printf("HR0=%d  HR1=%d  HR2=%d\n",
                      holdingRegs[0], holdingRegs[1], holdingRegs[2]);
    }

    Ethernet.maintain();
}
