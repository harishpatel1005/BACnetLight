/*
 * BACnetLight - Lightweight BACnet/IP & BACnet/MSTP Library for Arduino/ESP32
 *
 * A minimal, memory-efficient BACnet server implementation designed
 * for microcontrollers. Supports BACnet/IP (UDP) and BACnet/MSTP (RS485).
 *
 * Supported BACnet Services:
 *   - WhoIs / IAm (device discovery)
 *   - ReadProperty
 *   - ReadPropertyMultiple
 *   - WriteProperty
 *   - SubscribeCOV / COV Notification (Change of Value)
 *   - Error responses
 *
 * Supported Object Types:
 *   - Device
 *   - Analog Input (AI)   - read-only sensor values
 *   - Analog Output (AO)  - writable control outputs
 *   - Analog Value (AV)   - read/write floating point
 *   - Binary Input (BI)   - read-only digital states
 *   - Binary Output (BO)  - writable digital outputs
 *   - Binary Value (BV)   - read/write boolean states
 *
 * Transport:
 *   - BACnet/IP over UDP (Ethernet/WiFi)
 *   - BACnet/MSTP over RS485 serial
 *
 * License: MIT
 */

#ifndef BACNET_LIGHT_H
#define BACNET_LIGHT_H

#define BACNETLIGHT_VERSION "1.0.0"
#define BACNETLIGHT_VERSION_MAJOR 1
#define BACNETLIGHT_VERSION_MINOR 0
#define BACNETLIGHT_VERSION_PATCH 0

#include <Arduino.h>
#include <Udp.h>

// ============================================================
// BACnet Constants
// ============================================================

#define BACNET_IP_PORT 47808  // 0xBAC0

// Object Types (from BACnet spec)
#define BACNET_OBJ_ANALOG_INPUT    0
#define BACNET_OBJ_ANALOG_OUTPUT   1
#define BACNET_OBJ_ANALOG_VALUE    2
#define BACNET_OBJ_BINARY_INPUT    3
#define BACNET_OBJ_BINARY_OUTPUT   4
#define BACNET_OBJ_BINARY_VALUE    5
#define BACNET_OBJ_DEVICE          8
#define BACNET_OBJ_MULTI_STATE_INPUT  13
#define BACNET_OBJ_MULTI_STATE_VALUE  19

// Property Identifiers
#define BACNET_PROP_OBJECT_IDENTIFIER     75
#define BACNET_PROP_OBJECT_NAME           77
#define BACNET_PROP_OBJECT_TYPE           79
#define BACNET_PROP_PRESENT_VALUE         85
#define BACNET_PROP_DESCRIPTION           28
#define BACNET_PROP_STATUS_FLAGS          111
#define BACNET_PROP_EVENT_STATE           36
#define BACNET_PROP_OUT_OF_SERVICE        81
#define BACNET_PROP_UNITS                 117
#define BACNET_PROP_PRIORITY_ARRAY        87
#define BACNET_PROP_RELINQUISH_DEFAULT    104
#define BACNET_PROP_SYSTEM_STATUS         112
#define BACNET_PROP_VENDOR_NAME           121
#define BACNET_PROP_VENDOR_IDENTIFIER     120
#define BACNET_PROP_MODEL_NAME            70
#define BACNET_PROP_FIRMWARE_REVISION     44
#define BACNET_PROP_APP_SOFTWARE_VERSION  12
#define BACNET_PROP_PROTOCOL_VERSION      98
#define BACNET_PROP_PROTOCOL_REVISION     139
#define BACNET_PROP_OBJECT_LIST           76
#define BACNET_PROP_MAX_APDU_LENGTH       62
#define BACNET_PROP_SEGMENTATION_SUPPORTED 107
#define BACNET_PROP_APDU_TIMEOUT          11
#define BACNET_PROP_NUM_APDU_RETRIES      73
#define BACNET_PROP_DATABASE_REVISION     155
#define BACNET_PROP_COV_INCREMENT         22
#define BACNET_PROP_POLARITY              84

// Engineering Units (common)
#define BACNET_UNITS_NO_UNITS              95
#define BACNET_UNITS_DEGREES_CELSIUS       62
#define BACNET_UNITS_DEGREES_FAHRENHEIT    64
#define BACNET_UNITS_PERCENT               98
#define BACNET_UNITS_PPM                   96
#define BACNET_UNITS_PASCALS               53
#define BACNET_UNITS_KILOPASCALS           54
#define BACNET_UNITS_WATTS                 47
#define BACNET_UNITS_KILOWATTS             48
#define BACNET_UNITS_AMPERES               3
#define BACNET_UNITS_VOLTS                 5
#define BACNET_UNITS_LITERS_PER_SECOND     85
#define BACNET_UNITS_CUBIC_METERS_PER_HOUR 135
#define BACNET_UNITS_RPM                   104
#define BACNET_UNITS_HERTZ                 27
#define BACNET_UNITS_MILLIAMPERES          2

// PDU Types
#define BACNET_PDU_CONFIRMED      0x00
#define BACNET_PDU_UNCONFIRMED    0x10
#define BACNET_PDU_SIMPLE_ACK     0x20
#define BACNET_PDU_COMPLEX_ACK    0x30
#define BACNET_PDU_ERROR          0x50

// Confirmed Services
#define BACNET_SERVICE_READ_PROPERTY           0x0C
#define BACNET_SERVICE_READ_PROPERTY_MULTIPLE  0x0E
#define BACNET_SERVICE_WRITE_PROPERTY          0x0F
#define BACNET_SERVICE_SUBSCRIBE_COV           0x05
#define BACNET_SERVICE_CONFIRMED_COV_NOTIF     0x01  // Confirmed-COV-Notification

// Unconfirmed Services
#define BACNET_SERVICE_I_AM                    0x00
#define BACNET_SERVICE_WHO_IS                  0x08
#define BACNET_SERVICE_UNCONFIRMED_COV_NOTIF   0x02

// Application Tags
#define BACNET_TAG_NULL            0
#define BACNET_TAG_BOOLEAN         1
#define BACNET_TAG_UNSIGNED        2
#define BACNET_TAG_SIGNED          3
#define BACNET_TAG_REAL            4
#define BACNET_TAG_DOUBLE          5
#define BACNET_TAG_CHAR_STRING     7
#define BACNET_TAG_BIT_STRING      8
#define BACNET_TAG_ENUMERATED      9
#define BACNET_TAG_OBJECT_ID       12

// ============================================================
// Configuration Limits
// ============================================================

#ifndef BACNET_MAX_OBJECTS
#define BACNET_MAX_OBJECTS 32
#endif

#ifndef BACNET_MAX_COV_SUBSCRIPTIONS
#define BACNET_MAX_COV_SUBSCRIPTIONS 16
#endif

#ifndef BACNET_MAX_NAME_LEN
#define BACNET_MAX_NAME_LEN 32
#endif

#ifndef BACNET_MAX_DESC_LEN
#define BACNET_MAX_DESC_LEN 64
#endif

#ifndef BACNET_BUF_SIZE
#define BACNET_BUF_SIZE 512
#endif

#ifndef BACNET_COV_TIMEOUT_MS
#define BACNET_COV_TIMEOUT_MS  5000   // ms before retrying an unacknowledged confirmed COV
#endif

#ifndef BACNET_COV_MAX_RETRIES
#define BACNET_COV_MAX_RETRIES 2      // cancel subscription after this many timeouts
#endif

// BACnet priority levels (16 levels, 1=highest)
#define BACNET_NUM_PRIORITIES 16
#define BACNET_NO_PRIORITY    0xFF

// ============================================================
// BACnet Object Structure
// ============================================================

struct BACnetObject {
    uint16_t type;
    uint32_t instance;
    char name[BACNET_MAX_NAME_LEN];
    char description[BACNET_MAX_DESC_LEN];
    float presentValue;
    uint16_t units;
    bool outOfService;
    bool writable;

    // COV tracking
    float covIncrement;       // Minimum change to trigger COV (analog objects)
    float lastCovValue;       // Last value when COV was sent

    // Priority array (for AO/BO - commandable objects)
    bool hasPriorityArray;
    float priorityArray[BACNET_NUM_PRIORITIES];  // NAN = unused slot
    float relinquishDefault;
};

// ============================================================
// COV Subscription Structure
// ============================================================

struct COVSubscription {
    bool active;
    uint8_t subscriberProcessId;
    uint16_t objectType;
    uint32_t objectInstance;
    bool issueConfirmedNotifications;
    uint32_t lifetime;            // Seconds remaining; set from the subscriber's requested lifetime (> 0)
    unsigned long startTime;      // millis() when created
    IPAddress subscriberIP;
    uint16_t subscriberPort;
};

// ============================================================
// Pending Confirmed Request (for confirmed COV lifecycle)
// ============================================================

struct PendingConfirmedRequest {
    bool          active;
    uint8_t       invokeId;
    uint8_t       subIndex;     // index into _covSubs[]
    uint8_t       objIndex;     // index into _objects[]
    unsigned long sentTime;     // millis() at last transmission
    uint8_t       retryCount;
};

// ============================================================
// MSTP Frame Types
// ============================================================

#define MSTP_FRAME_TOKEN        0x00
#define MSTP_FRAME_POLL_FOR_MASTER  0x01
#define MSTP_FRAME_REPLY_TO_PFM    0x02
#define MSTP_FRAME_TEST_REQUEST    0x03
#define MSTP_FRAME_TEST_RESPONSE   0x04
#define MSTP_FRAME_BACNET_DATA_EXPECTING_REPLY  0x05
#define MSTP_FRAME_BACNET_DATA_NOT_EXPECTING    0x06
#define MSTP_FRAME_REPLY_POSTPONED 0x07

// MSTP States
enum MSTPState {
    MSTP_IDLE,
    MSTP_WAIT_FOR_REPLY,
    MSTP_ANSWER_DATA_REQUEST,
    MSTP_USE_TOKEN,
    MSTP_PASS_TOKEN,
    MSTP_NO_TOKEN,
    MSTP_POLL_FOR_MASTER
};

// ============================================================
// Callback Types
// ============================================================

typedef bool (*BACnetWriteCallback)(BACnetObject *object, float value, uint8_t priority);
typedef void (*BACnetCOVCallback)(BACnetObject *object);

// ============================================================
// BACnetLight Class - BACnet/IP
// ============================================================

class BACnetLight {
public:
    BACnetLight();

    // --- Initialization ---
    void setUDP(UDP &transport);
    bool begin(uint32_t deviceInstance, const char *deviceName, IPAddress targetIP);
    bool begin(uint32_t deviceInstance, const char *deviceName, IPAddress targetIP, UDP &transport);
    void loop();

    // --- Object Creation ---
    BACnetObject* addAnalogInput(uint32_t instance, const char *name,
                                  float initialValue = 0.0f,
                                  uint16_t units = BACNET_UNITS_NO_UNITS,
                                  const char *description = "");

    BACnetObject* addAnalogOutput(uint32_t instance, const char *name,
                                   float relinquishDefault = 0.0f,
                                   uint16_t units = BACNET_UNITS_NO_UNITS,
                                   const char *description = "");

    BACnetObject* addAnalogValue(uint32_t instance, const char *name,
                                  float initialValue = 0.0f,
                                  uint16_t units = BACNET_UNITS_NO_UNITS,
                                  bool writable = false,
                                  const char *description = "");

    BACnetObject* addBinaryInput(uint32_t instance, const char *name,
                                  bool initialValue = false,
                                  const char *description = "");

    BACnetObject* addBinaryOutput(uint32_t instance, const char *name,
                                   bool relinquishDefault = false,
                                   const char *description = "");

    BACnetObject* addBinaryValue(uint32_t instance, const char *name,
                                  bool initialValue = false,
                                  bool writable = false,
                                  const char *description = "");

    // --- Value Access ---
    bool setValue(uint16_t type, uint32_t instance, float value);
    float getValue(uint16_t type, uint32_t instance);
    BACnetObject* getObject(uint16_t type, uint32_t instance);

    // --- Priority Array (for AO/BO commandable objects) ---
    bool commandObject(uint16_t type, uint32_t instance, float value, uint8_t priority);
    bool relinquish(uint16_t type, uint32_t instance, uint8_t priority);

    // --- COV Configuration ---
    void setCOVIncrement(uint16_t type, uint32_t instance, float increment);

    // --- Callbacks ---
    void onWrite(BACnetWriteCallback callback);
    void onCOV(BACnetCOVCallback callback);

    // --- Device Info ---
    void setDeviceInfo(const char *vendorName, uint16_t vendorId = 0,
                       const char *modelName = "",
                       const char *firmware = "1.0.0",
                       const char *software = "1.0.0");

    void sendIAm();
    uint8_t getObjectCount();

protected:
    // Device properties
    uint32_t _deviceInstance;
    char _deviceName[BACNET_MAX_NAME_LEN];
    char _vendorName[BACNET_MAX_NAME_LEN];
    uint16_t _vendorId;
    char _modelName[BACNET_MAX_NAME_LEN];
    char _firmwareRev[16];
    char _softwareVer[16];

    // Objects
    BACnetObject _objects[BACNET_MAX_OBJECTS];
    uint8_t _objectCount;

    // COV subscriptions
    COVSubscription _covSubs[BACNET_MAX_COV_SUBSCRIPTIONS];

    // In-flight confirmed COV notifications
    PendingConfirmedRequest _pendingCOV[BACNET_MAX_COV_SUBSCRIPTIONS];

    // Networking (IP)
    UDP *_udp;
    IPAddress _targetIP;
    bool _ipEnabled;

    // Buffers
    uint8_t _rxBuf[BACNET_BUF_SIZE];
    uint8_t _txBuf[BACNET_BUF_SIZE];
    int _txLen;

    // Callbacks
    BACnetWriteCallback _writeCallback;
    BACnetCOVCallback _covCallback;

    // Confirmed service invoke ID counter
    uint8_t _invokeId;

    // Set true by MSTP subclass while processing an MSTP request,
    // so sendIPResponse skips the UDP send and just fills _txBuf.
    bool _processingMSTP;

    // --- Encoding Helpers ---
    int encodeContextTag(uint8_t *buf, uint8_t tagNum, uint32_t value, int lenBytes);
    int encodeContextUnsigned(uint8_t *buf, uint8_t tagNum, uint32_t value);
    int encodeOpeningTag(uint8_t *buf, uint8_t tagNum);
    int encodeClosingTag(uint8_t *buf, uint8_t tagNum);
    int encodeAppUnsigned(uint8_t *buf, uint32_t value);
    int encodeAppSigned(uint8_t *buf, int32_t value);
    int encodeAppEnumerated(uint8_t *buf, uint32_t value);
    int encodeAppBoolean(uint8_t *buf, bool value);
    int encodeAppReal(uint8_t *buf, float value);
    int encodeAppObjectId(uint8_t *buf, uint16_t objectType, uint32_t instance);
    int encodeAppCharString(uint8_t *buf, const char *str);
    int encodeAppBitString(uint8_t *buf, uint8_t bits, uint8_t numBits);
    int encodeAppNull(uint8_t *buf);

    // --- Property Encoding ---
    int encodeDeviceProperty(uint8_t *buf, uint32_t property);
    int encodeObjectProperty(uint8_t *buf, BACnetObject *obj, uint32_t property);
    int encodePropertyValue(uint8_t *buf, uint16_t objectType, uint32_t instance, uint32_t property);
    int encodePriorityArray(uint8_t *buf, BACnetObject *obj);
    int encodeCOVPropertyList(uint8_t *buf, BACnetObject *obj);

    // --- Packet Handlers ---
    void handleIPPacket(int packetSize);
    void handleAPDU(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort);
    void handleWhoIs(uint8_t *apdu, int apduLen);
    void handleReadProperty(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort);
    void handleReadPropertyMultiple(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort);
    void handleWriteProperty(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort);
    void handleSubscribeCOV(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort);

    // --- COV ---
    void checkCOV();
    void sendCOVNotification(COVSubscription *sub, BACnetObject *obj);
    void sendConfirmedCOVApdu(uint8_t invokeId, COVSubscription *sub, BACnetObject *obj);
    void expireCOVSubscriptions();
    void retryPendingCOV();
    void handleCOVSimpleAck(uint8_t invokeId);
    void handleCOVError(uint8_t invokeId);

    // --- Decode Helpers ---
    float decodeAppValue(uint8_t *buf, int *pos, int len);
    bool decodeObjectId(uint8_t *buf, int *pos, int len, uint16_t *type, uint32_t *instance);
    uint32_t decodeUnsigned(uint8_t *buf, int *pos, int len);

    // --- Priority Array ---
    float resolvePriority(BACnetObject *obj);

    // --- Object Helper ---
    BACnetObject* addObject(uint16_t type, uint32_t instance, const char *name,
                            float initialValue, uint16_t units, bool writable,
                            bool commandable, float relinquishDefault,
                            const char *description);

    // --- IP Response Helper ---
    // Virtual so BACnetMSTP can override to route responses over MSTP instead of UDP.
    virtual void sendIPResponse(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort);
};

// ============================================================
// BACnetMSTP Class - BACnet/MSTP over RS485
// ============================================================

class BACnetMSTP : public BACnetLight {
public:
    BACnetMSTP();

    /**
     * Initialize BACnet/MSTP transport (MSTP-only, no IP stack required).
     *
     * @param deviceInstance  BACnet device instance number (0-4194302)
     * @param deviceName      Human-readable device name string
     * @param serial          Hardware serial port (e.g., Serial2), already configured with serial.begin()
     * @param dePin           RS485 DE/RE pin (direction control)
     * @param macAddress      MSTP MAC address (0-127)
     * @param baud            Expected MSTP baud rate; must match the rate passed to serial.begin()
     * @param maxMaster       Highest MAC address to poll (default 127)
     * @return                true if initialized
     */
    bool beginMSTP(uint32_t deviceInstance, const char *deviceName,
                   HardwareSerial &serial, uint8_t dePin, uint8_t macAddress,
                   uint32_t baud = 38400, uint8_t maxMaster = 127);

    /**
     * Initialize both BACnet/IP and BACnet/MSTP.
     * The caller must configure the serial port with serial.begin() first.
     */
    bool beginDual(uint32_t deviceInstance, const char *deviceName,
                   IPAddress targetIP,
                   HardwareSerial &serial, uint8_t dePin, uint8_t macAddress,
                   uint32_t baud = 38400);
    bool beginDual(uint32_t deviceInstance, const char *deviceName,
                   IPAddress targetIP, UDP &transport,
                   HardwareSerial &serial, uint8_t dePin, uint8_t macAddress,
                   uint32_t baud = 38400);

    /**
     * Process both IP and MSTP packets. Call in loop().
     */
    void loop();

private:
    // MSTP config
    HardwareSerial *_mstpSerial;
    uint8_t _dePin;
    uint8_t _macAddress;
    uint8_t _maxMaster;
    uint8_t _nextStation;
    bool _mstpEnabled;

    // MSTP state machine
    MSTPState _mstpState;
    bool _hasToken;
    unsigned long _tokenTimer;
    unsigned long _silenceTimer;
    uint8_t _tokenCount;
    uint8_t _retryCount;
    uint8_t _framePending;

    // MSTP frame buffers
    uint8_t _mstpRxBuf[BACNET_BUF_SIZE];
    uint8_t _mstpTxBuf[BACNET_BUF_SIZE];
    int _mstpRxLen;

    // Pending proactive MSTP transmit (e.g. COV notifications sent when token is held)
    bool _mstpTxPending;
    uint8_t _mstpTxDest;
    int _mstpTxDataLen;

    // MSTP CRC
    static uint8_t calcHeaderCRC(uint8_t *buf, int len);
    static uint16_t calcDataCRC(uint8_t *buf, int len);

    // MSTP frame handling
    void mstpReceive();
    void mstpSendFrame(uint8_t frameType, uint8_t dest, uint8_t *data, int dataLen);
    void mstpSendToken(uint8_t dest);
    void mstpHandleFrame(uint8_t frameType, uint8_t src, uint8_t dest,
                         uint8_t *data, int dataLen);
    void mstpStateMachine();
    void mstpHandleDataFrame(uint8_t src, uint8_t *data, int dataLen, bool expectingReply);

    // RS485 direction control
    void setTxMode(bool tx);

    // Override to route responses over MSTP when appropriate
    void sendIPResponse(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort) override;
};

#endif // BACNET_LIGHT_H
