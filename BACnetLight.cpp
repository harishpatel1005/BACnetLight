/*
 * BACnetLight - Implementation
 * BACnet/IP with COV, RPM, WriteProperty, Priority Array
 * License: MIT
 */

#include "BACnetLight.h"

// ============================================================
// Constructor
// ============================================================

BACnetLight::BACnetLight() {
    _objectCount = 0;
    _vendorId = 0;
    _writeCallback = nullptr;
    _covCallback = nullptr;
    _udp = nullptr;
    _txLen = 0;
    _ipEnabled = false;
    _processingMSTP = false;
    _invokeId = 0;

    memset(_deviceName, 0, sizeof(_deviceName));
    memset(_vendorName, 0, sizeof(_vendorName));
    memset(_modelName, 0, sizeof(_modelName));
    strncpy(_firmwareRev, "1.0.0", sizeof(_firmwareRev));
    strncpy(_softwareVer, "1.0.0", sizeof(_softwareVer));
    strncpy(_vendorName, "BACnetLight", sizeof(_vendorName) - 1);
    strncpy(_modelName, "Arduino-Device", sizeof(_modelName) - 1);

    // Clear COV subscriptions and pending confirmed requests
    for (int i = 0; i < BACNET_MAX_COV_SUBSCRIPTIONS; i++) {
        _covSubs[i].active = false;
        _pendingCOV[i].active = false;
    }
}

// ============================================================
// Initialization
// ============================================================

void BACnetLight::setUDP(UDP &transport) {
    _udp = &transport;
}

bool BACnetLight::begin(uint32_t deviceInstance, const char *deviceName, IPAddress targetIP) {
    if (_udp == nullptr) {
        return false;
    }

    _deviceInstance = deviceInstance;
    _targetIP = targetIP;
    strncpy(_deviceName, deviceName, BACNET_MAX_NAME_LEN - 1);
    _deviceName[BACNET_MAX_NAME_LEN - 1] = '\0';

    if (!_udp->begin(BACNET_IP_PORT)) {
        return false;
    }

    _ipEnabled = true;
    delay(500);
    sendIAm();
    return true;
}

bool BACnetLight::begin(uint32_t deviceInstance, const char *deviceName, IPAddress targetIP, UDP &transport) {
    setUDP(transport);
    return begin(deviceInstance, deviceName, targetIP);
}

void BACnetLight::loop() {
    // Process IP packets
    if (_ipEnabled && _udp != nullptr) {
        int packetSize = _udp->parsePacket();
        if (packetSize > 0) {
            handleIPPacket(packetSize);
        }
    }

    // Check COV notifications
    checkCOV();
    expireCOVSubscriptions();
    retryPendingCOV();
}

// ============================================================
// Object Creation
// ============================================================

BACnetObject* BACnetLight::addAnalogInput(uint32_t instance, const char *name,
                                           float initialValue, uint16_t units,
                                           const char *description) {
    return addObject(BACNET_OBJ_ANALOG_INPUT, instance, name, initialValue,
                     units, false, false, 0.0f, description);
}

BACnetObject* BACnetLight::addAnalogOutput(uint32_t instance, const char *name,
                                            float relinquishDefault, uint16_t units,
                                            const char *description) {
    return addObject(BACNET_OBJ_ANALOG_OUTPUT, instance, name, relinquishDefault,
                     units, true, true, relinquishDefault, description);
}

BACnetObject* BACnetLight::addAnalogValue(uint32_t instance, const char *name,
                                           float initialValue, uint16_t units,
                                           bool writable, const char *description) {
    return addObject(BACNET_OBJ_ANALOG_VALUE, instance, name, initialValue,
                     units, writable, false, initialValue, description);
}

BACnetObject* BACnetLight::addBinaryInput(uint32_t instance, const char *name,
                                           bool initialValue, const char *description) {
    return addObject(BACNET_OBJ_BINARY_INPUT, instance, name, initialValue ? 1.0f : 0.0f,
                     BACNET_UNITS_NO_UNITS, false, false, 0.0f, description);
}

BACnetObject* BACnetLight::addBinaryOutput(uint32_t instance, const char *name,
                                            bool relinquishDefault, const char *description) {
    return addObject(BACNET_OBJ_BINARY_OUTPUT, instance, name, relinquishDefault ? 1.0f : 0.0f,
                     BACNET_UNITS_NO_UNITS, true, true, relinquishDefault ? 1.0f : 0.0f, description);
}

BACnetObject* BACnetLight::addBinaryValue(uint32_t instance, const char *name,
                                           bool initialValue, bool writable,
                                           const char *description) {
    return addObject(BACNET_OBJ_BINARY_VALUE, instance, name, initialValue ? 1.0f : 0.0f,
                     BACNET_UNITS_NO_UNITS, writable, false, 0.0f, description);
}

BACnetObject* BACnetLight::addObject(uint16_t type, uint32_t instance, const char *name,
                                      float initialValue, uint16_t units, bool writable,
                                      bool commandable, float relinquishDefault,
                                      const char *description) {
    if (_objectCount >= BACNET_MAX_OBJECTS) return nullptr;

    BACnetObject *obj = &_objects[_objectCount];
    obj->type = type;
    obj->instance = instance;
    strncpy(obj->name, name, BACNET_MAX_NAME_LEN - 1);
    obj->name[BACNET_MAX_NAME_LEN - 1] = '\0';
    strncpy(obj->description, description, BACNET_MAX_DESC_LEN - 1);
    obj->description[BACNET_MAX_DESC_LEN - 1] = '\0';
    obj->presentValue = initialValue;
    obj->units = units;
    obj->outOfService = false;
    obj->writable = writable;

    // COV defaults
    obj->covIncrement = 0.1f;  // Default: trigger on 0.1 change for analog
    obj->lastCovValue = initialValue;

    // Priority array (for commandable objects: AO, BO)
    obj->hasPriorityArray = commandable;
    obj->relinquishDefault = relinquishDefault;
    for (int i = 0; i < BACNET_NUM_PRIORITIES; i++) {
        obj->priorityArray[i] = NAN;
    }

    _objectCount++;
    return obj;
}

// ============================================================
// Value Access
// ============================================================

bool BACnetLight::setValue(uint16_t type, uint32_t instance, float value) {
    BACnetObject *obj = getObject(type, instance);
    if (!obj) return false;

    if (obj->hasPriorityArray) {
        // Route through priority array at lowest priority to keep
        // Present_Value, Priority_Array, and Relinquish_Default consistent
        obj->priorityArray[BACNET_NUM_PRIORITIES - 1] = value;
        obj->presentValue = resolvePriority(obj);
    } else {
        obj->presentValue = value;
    }
    return true;
}

float BACnetLight::getValue(uint16_t type, uint32_t instance) {
    BACnetObject *obj = getObject(type, instance);
    return obj ? obj->presentValue : NAN;
}

BACnetObject* BACnetLight::getObject(uint16_t type, uint32_t instance) {
    for (uint8_t i = 0; i < _objectCount; i++) {
        if (_objects[i].type == type && _objects[i].instance == instance) {
            return &_objects[i];
        }
    }
    return nullptr;
}

uint8_t BACnetLight::getObjectCount() { return _objectCount; }

// ============================================================
// Priority Array (AO/BO Command)
// ============================================================

bool BACnetLight::commandObject(uint16_t type, uint32_t instance, float value, uint8_t priority) {
    if (priority < 1 || priority > 16) return false;
    BACnetObject *obj = getObject(type, instance);
    if (!obj || !obj->hasPriorityArray) return false;

    obj->priorityArray[priority - 1] = value;
    obj->presentValue = resolvePriority(obj);
    return true;
}

bool BACnetLight::relinquish(uint16_t type, uint32_t instance, uint8_t priority) {
    if (priority < 1 || priority > 16) return false;
    BACnetObject *obj = getObject(type, instance);
    if (!obj || !obj->hasPriorityArray) return false;

    obj->priorityArray[priority - 1] = NAN;
    obj->presentValue = resolvePriority(obj);
    return true;
}

float BACnetLight::resolvePriority(BACnetObject *obj) {
    for (int i = 0; i < BACNET_NUM_PRIORITIES; i++) {
        if (!isnan(obj->priorityArray[i])) {
            return obj->priorityArray[i];
        }
    }
    return obj->relinquishDefault;
}

// ============================================================
// COV Configuration
// ============================================================

void BACnetLight::setCOVIncrement(uint16_t type, uint32_t instance, float increment) {
    BACnetObject *obj = getObject(type, instance);
    if (obj) obj->covIncrement = increment;
}

void BACnetLight::onWrite(BACnetWriteCallback callback) { _writeCallback = callback; }
void BACnetLight::onCOV(BACnetCOVCallback callback) { _covCallback = callback; }

void BACnetLight::setDeviceInfo(const char *vendorName, uint16_t vendorId,
                                 const char *modelName, const char *firmware,
                                 const char *software) {
    strncpy(_vendorName, vendorName, BACNET_MAX_NAME_LEN - 1);
    _vendorName[BACNET_MAX_NAME_LEN - 1] = '\0';
    _vendorId = vendorId;
    strncpy(_modelName, modelName, BACNET_MAX_NAME_LEN - 1);
    _modelName[BACNET_MAX_NAME_LEN - 1] = '\0';
    strncpy(_firmwareRev, firmware, sizeof(_firmwareRev) - 1);
    _firmwareRev[sizeof(_firmwareRev) - 1] = '\0';
    strncpy(_softwareVer, software, sizeof(_softwareVer) - 1);
    _softwareVer[sizeof(_softwareVer) - 1] = '\0';
}

// ============================================================
// I-Am
// ============================================================

void BACnetLight::sendIAm() {
    _txLen = 0;

    _txBuf[_txLen++] = 0x81;
    _txBuf[_txLen++] = 0x0A;
    _txBuf[_txLen++] = 0x00;
    _txBuf[_txLen++] = 0x00;

    // NPDU with global broadcast
    _txBuf[_txLen++] = 0x01;
    _txBuf[_txLen++] = 0x20;
    _txBuf[_txLen++] = 0xFF;
    _txBuf[_txLen++] = 0xFF;
    _txBuf[_txLen++] = 0x00;
    _txBuf[_txLen++] = 0xFF;

    _txBuf[_txLen++] = BACNET_PDU_UNCONFIRMED;
    _txBuf[_txLen++] = BACNET_SERVICE_I_AM;

    // Device Object ID
    _txBuf[_txLen++] = 0xC4;
    uint32_t oid = ((uint32_t)BACNET_OBJ_DEVICE << 22) | (_deviceInstance & 0x3FFFFF);
    _txBuf[_txLen++] = (oid >> 24) & 0xFF;
    _txBuf[_txLen++] = (oid >> 16) & 0xFF;
    _txBuf[_txLen++] = (oid >> 8) & 0xFF;
    _txBuf[_txLen++] = oid & 0xFF;

    // Max APDU (480)
    _txBuf[_txLen++] = 0x22;
    _txBuf[_txLen++] = 0x01;
    _txBuf[_txLen++] = 0xE0;

    // Segmentation (none)
    _txBuf[_txLen++] = 0x91;
    _txBuf[_txLen++] = 0x03;

    // Vendor ID (uint16_t — tag 0x22 = app-unsigned, 2 bytes, big-endian)
    _txBuf[_txLen++] = 0x22;
    _txBuf[_txLen++] = (_vendorId >> 8) & 0xFF;
    _txBuf[_txLen++] = _vendorId & 0xFF;

    _txBuf[2] = (_txLen >> 8) & 0xFF;
    _txBuf[3] = _txLen & 0xFF;

    if (_ipEnabled && _udp != nullptr) {
        _udp->beginPacket(_targetIP, BACNET_IP_PORT);
        _udp->write(_txBuf, _txLen);
        _udp->endPacket();
    }
}

// ============================================================
// Encoding Helpers
// ============================================================

int BACnetLight::encodeContextTag(uint8_t *buf, uint8_t tagNum, uint32_t value, int lenBytes) {
    int pos = 0;
    uint8_t tag = (tagNum << 4) | 0x08;
    if (lenBytes <= 4) {
        tag |= (lenBytes & 0x07);
        buf[pos++] = tag;
    } else {
        tag |= 0x05;
        buf[pos++] = tag;
        buf[pos++] = lenBytes;
    }
    for (int i = lenBytes - 1; i >= 0; i--) {
        buf[pos++] = (value >> (i * 8)) & 0xFF;
    }
    return pos;
}

int BACnetLight::encodeContextUnsigned(uint8_t *buf, uint8_t tagNum, uint32_t value) {
    int lenBytes;
    if (value < 0x100) lenBytes = 1;
    else if (value < 0x10000) lenBytes = 2;
    else if (value < 0x1000000) lenBytes = 3;
    else lenBytes = 4;
    return encodeContextTag(buf, tagNum, value, lenBytes);
}

int BACnetLight::encodeOpeningTag(uint8_t *buf, uint8_t tagNum) {
    buf[0] = (tagNum << 4) | 0x0E;
    return 1;
}

int BACnetLight::encodeClosingTag(uint8_t *buf, uint8_t tagNum) {
    buf[0] = (tagNum << 4) | 0x0F;
    return 1;
}

int BACnetLight::encodeAppUnsigned(uint8_t *buf, uint32_t value) {
    int pos = 0, lenBytes;
    if (value < 0x100) lenBytes = 1;
    else if (value < 0x10000) lenBytes = 2;
    else if (value < 0x1000000) lenBytes = 3;
    else lenBytes = 4;
    buf[pos++] = (BACNET_TAG_UNSIGNED << 4) | lenBytes;
    for (int i = lenBytes - 1; i >= 0; i--)
        buf[pos++] = (value >> (i * 8)) & 0xFF;
    return pos;
}

int BACnetLight::encodeAppSigned(uint8_t *buf, int32_t value) {
    int pos = 0, lenBytes;
    if (value >= -128 && value <= 127) lenBytes = 1;
    else if (value >= -32768 && value <= 32767) lenBytes = 2;
    else lenBytes = 4;
    buf[pos++] = (BACNET_TAG_SIGNED << 4) | lenBytes;
    for (int i = lenBytes - 1; i >= 0; i--)
        buf[pos++] = (value >> (i * 8)) & 0xFF;
    return pos;
}

int BACnetLight::encodeAppEnumerated(uint8_t *buf, uint32_t value) {
    int pos = 0;
    int lenBytes = (value < 0x100) ? 1 : (value < 0x10000) ? 2 : 4;
    buf[pos++] = (BACNET_TAG_ENUMERATED << 4) | lenBytes;
    for (int i = lenBytes - 1; i >= 0; i--)
        buf[pos++] = (value >> (i * 8)) & 0xFF;
    return pos;
}

int BACnetLight::encodeAppBoolean(uint8_t *buf, bool value) {
    buf[0] = (BACNET_TAG_BOOLEAN << 4) | (value ? 1 : 0);
    return 1;
}

int BACnetLight::encodeAppReal(uint8_t *buf, float value) {
    int pos = 0;
    buf[pos++] = (BACNET_TAG_REAL << 4) | 4;
    uint8_t *f = (uint8_t *)&value;
    buf[pos++] = f[3]; buf[pos++] = f[2];
    buf[pos++] = f[1]; buf[pos++] = f[0];
    return pos;
}

int BACnetLight::encodeAppObjectId(uint8_t *buf, uint16_t objectType, uint32_t instance) {
    int pos = 0;
    buf[pos++] = (BACNET_TAG_OBJECT_ID << 4) | 4;
    uint32_t v = ((uint32_t)objectType << 22) | (instance & 0x3FFFFF);
    buf[pos++] = (v >> 24) & 0xFF; buf[pos++] = (v >> 16) & 0xFF;
    buf[pos++] = (v >> 8) & 0xFF;  buf[pos++] = v & 0xFF;
    return pos;
}

int BACnetLight::encodeAppCharString(uint8_t *buf, const char *str) {
    int pos = 0;
    int slen = strlen(str);
    int totalLen = slen + 1;
    uint8_t tag = (BACNET_TAG_CHAR_STRING << 4);
    if (totalLen <= 4) { tag |= totalLen; buf[pos++] = tag; }
    else { tag |= 5; buf[pos++] = tag; buf[pos++] = totalLen; }
    buf[pos++] = 0; // UTF-8
    memcpy(&buf[pos], str, slen);
    pos += slen;
    return pos;
}

int BACnetLight::encodeAppBitString(uint8_t *buf, uint8_t bits, uint8_t numBits) {
    int pos = 0;
    int byteCount = (numBits + 7) / 8;
    int totalLen = byteCount + 1;
    buf[pos++] = (BACNET_TAG_BIT_STRING << 4) | totalLen;
    buf[pos++] = (8 * byteCount) - numBits;
    buf[pos++] = bits << (8 - numBits);
    return pos;
}

int BACnetLight::encodeAppNull(uint8_t *buf) {
    buf[0] = (BACNET_TAG_NULL << 4);
    return 1;
}

// ============================================================
// Property Encoding
// ============================================================

int BACnetLight::encodeDeviceProperty(uint8_t *buf, uint32_t property) {
    int pos = 0;
    switch (property) {
        case BACNET_PROP_OBJECT_IDENTIFIER:
            pos += encodeAppObjectId(&buf[pos], BACNET_OBJ_DEVICE, _deviceInstance); break;
        case BACNET_PROP_OBJECT_NAME:
            pos += encodeAppCharString(&buf[pos], _deviceName); break;
        case BACNET_PROP_OBJECT_TYPE:
            pos += encodeAppEnumerated(&buf[pos], BACNET_OBJ_DEVICE); break;
        case BACNET_PROP_SYSTEM_STATUS:
            pos += encodeAppEnumerated(&buf[pos], 0); break;
        case BACNET_PROP_VENDOR_NAME:
            pos += encodeAppCharString(&buf[pos], _vendorName); break;
        case BACNET_PROP_VENDOR_IDENTIFIER:
            pos += encodeAppUnsigned(&buf[pos], _vendorId); break;
        case BACNET_PROP_MODEL_NAME:
            pos += encodeAppCharString(&buf[pos], _modelName); break;
        case BACNET_PROP_FIRMWARE_REVISION:
            pos += encodeAppCharString(&buf[pos], _firmwareRev); break;
        case BACNET_PROP_APP_SOFTWARE_VERSION:
            pos += encodeAppCharString(&buf[pos], _softwareVer); break;
        case BACNET_PROP_PROTOCOL_VERSION:
            pos += encodeAppUnsigned(&buf[pos], 1); break;
        case BACNET_PROP_PROTOCOL_REVISION:
            pos += encodeAppUnsigned(&buf[pos], 14); break;
        case BACNET_PROP_MAX_APDU_LENGTH:
            pos += encodeAppUnsigned(&buf[pos], 480); break;
        case BACNET_PROP_SEGMENTATION_SUPPORTED:
            pos += encodeAppEnumerated(&buf[pos], 3); break;
        case BACNET_PROP_APDU_TIMEOUT:
            pos += encodeAppUnsigned(&buf[pos], 3000); break;
        case BACNET_PROP_NUM_APDU_RETRIES:
            pos += encodeAppUnsigned(&buf[pos], 3); break;
        case BACNET_PROP_DATABASE_REVISION:
            pos += encodeAppUnsigned(&buf[pos], 1); break;
        case BACNET_PROP_OBJECT_LIST: {
            pos += encodeAppObjectId(&buf[pos], BACNET_OBJ_DEVICE, _deviceInstance);
            for (uint8_t i = 0; i < _objectCount; i++)
                pos += encodeAppObjectId(&buf[pos], _objects[i].type, _objects[i].instance);
            break;
        }
        default: return -1;
    }
    return pos;
}

int BACnetLight::encodePriorityArray(uint8_t *buf, BACnetObject *obj) {
    int pos = 0;
    for (int i = 0; i < BACNET_NUM_PRIORITIES; i++) {
        if (isnan(obj->priorityArray[i])) {
            pos += encodeAppNull(&buf[pos]);
        } else {
            if (obj->type == BACNET_OBJ_BINARY_OUTPUT) {
                pos += encodeAppEnumerated(&buf[pos], obj->priorityArray[i] > 0.5f ? 1 : 0);
            } else {
                pos += encodeAppReal(&buf[pos], obj->priorityArray[i]);
            }
        }
    }
    return pos;
}

int BACnetLight::encodeObjectProperty(uint8_t *buf, BACnetObject *obj, uint32_t property) {
    int pos = 0;
    switch (property) {
        case BACNET_PROP_OBJECT_IDENTIFIER:
            pos += encodeAppObjectId(&buf[pos], obj->type, obj->instance); break;
        case BACNET_PROP_OBJECT_NAME:
            pos += encodeAppCharString(&buf[pos], obj->name); break;
        case BACNET_PROP_OBJECT_TYPE:
            pos += encodeAppEnumerated(&buf[pos], obj->type); break;
        case BACNET_PROP_PRESENT_VALUE:
            if (obj->type == BACNET_OBJ_BINARY_VALUE || obj->type == BACNET_OBJ_BINARY_INPUT ||
                obj->type == BACNET_OBJ_BINARY_OUTPUT) {
                pos += encodeAppEnumerated(&buf[pos], obj->presentValue > 0.5f ? 1 : 0);
            } else {
                pos += encodeAppReal(&buf[pos], obj->presentValue);
            }
            break;
        case BACNET_PROP_DESCRIPTION:
            pos += encodeAppCharString(&buf[pos], obj->description); break;
        case BACNET_PROP_STATUS_FLAGS:
            pos += encodeAppBitString(&buf[pos], 0x00, 4); break;
        case BACNET_PROP_EVENT_STATE:
            pos += encodeAppEnumerated(&buf[pos], 0); break;
        case BACNET_PROP_OUT_OF_SERVICE:
            pos += encodeAppBoolean(&buf[pos], obj->outOfService); break;
        case BACNET_PROP_UNITS:
            if (obj->type == BACNET_OBJ_ANALOG_VALUE || obj->type == BACNET_OBJ_ANALOG_INPUT ||
                obj->type == BACNET_OBJ_ANALOG_OUTPUT) {
                pos += encodeAppEnumerated(&buf[pos], obj->units);
            } else { return -1; }
            break;
        case BACNET_PROP_COV_INCREMENT:
            if (obj->type == BACNET_OBJ_ANALOG_VALUE || obj->type == BACNET_OBJ_ANALOG_INPUT ||
                obj->type == BACNET_OBJ_ANALOG_OUTPUT) {
                pos += encodeAppReal(&buf[pos], obj->covIncrement);
            } else { return -1; }
            break;
        case BACNET_PROP_PRIORITY_ARRAY:
            if (obj->hasPriorityArray) {
                pos += encodePriorityArray(&buf[pos], obj);
            } else { return -1; }
            break;
        case BACNET_PROP_RELINQUISH_DEFAULT:
            if (obj->hasPriorityArray) {
                if (obj->type == BACNET_OBJ_BINARY_OUTPUT)
                    pos += encodeAppEnumerated(&buf[pos], obj->relinquishDefault > 0.5f ? 1 : 0);
                else
                    pos += encodeAppReal(&buf[pos], obj->relinquishDefault);
            } else { return -1; }
            break;
        case BACNET_PROP_POLARITY:
            if (obj->type == BACNET_OBJ_BINARY_INPUT || obj->type == BACNET_OBJ_BINARY_OUTPUT) {
                pos += encodeAppEnumerated(&buf[pos], 0); // normal polarity
            } else { return -1; }
            break;
        default: return -1;
    }
    return pos;
}

int BACnetLight::encodePropertyValue(uint8_t *buf, uint16_t objectType, uint32_t instance, uint32_t property) {
    if (objectType == BACNET_OBJ_DEVICE)
        return encodeDeviceProperty(buf, property);
    BACnetObject *obj = getObject(objectType, instance);
    if (!obj) return -1;
    return encodeObjectProperty(buf, obj, property);
}

int BACnetLight::encodeCOVPropertyList(uint8_t *buf, BACnetObject *obj) {
    int pos = 0;
    // Property 1: present-value
    pos += encodeOpeningTag(&buf[pos], 1);
    pos += encodeContextTag(&buf[pos], 0, BACNET_PROP_PRESENT_VALUE, 1);
    pos += encodeOpeningTag(&buf[pos], 2);
    if (obj->type == BACNET_OBJ_BINARY_VALUE || obj->type == BACNET_OBJ_BINARY_INPUT ||
        obj->type == BACNET_OBJ_BINARY_OUTPUT) {
        pos += encodeAppEnumerated(&buf[pos], obj->presentValue > 0.5f ? 1 : 0);
    } else {
        pos += encodeAppReal(&buf[pos], obj->presentValue);
    }
    pos += encodeClosingTag(&buf[pos], 2);
    pos += encodeClosingTag(&buf[pos], 1);

    // Property 2: status-flags
    pos += encodeOpeningTag(&buf[pos], 1);
    pos += encodeContextTag(&buf[pos], 0, BACNET_PROP_STATUS_FLAGS, 1);
    pos += encodeOpeningTag(&buf[pos], 2);
    pos += encodeAppBitString(&buf[pos], 0x00, 4);
    pos += encodeClosingTag(&buf[pos], 2);
    pos += encodeClosingTag(&buf[pos], 1);

    return pos;
}

// ============================================================
// Decode Helpers
// ============================================================

float BACnetLight::decodeAppValue(uint8_t *buf, int *pos, int len) {
    if (*pos >= len) return NAN;
    uint8_t tag = buf[*pos]; (*pos)++;
    uint8_t tagNumber = (tag >> 4) & 0x0F;
    int dataLen = tag & 0x07;
    if (dataLen == 5 && *pos < len) { dataLen = buf[*pos]; (*pos)++; }

    if (tagNumber == BACNET_TAG_REAL && dataLen == 4 && (*pos + 4) <= len) {
        uint8_t fb[4];
        fb[3] = buf[*pos]; fb[2] = buf[*pos+1]; fb[1] = buf[*pos+2]; fb[0] = buf[*pos+3];
        *pos += 4;
        float r; memcpy(&r, fb, 4); return r;
    } else if (tagNumber == BACNET_TAG_ENUMERATED || tagNumber == BACNET_TAG_UNSIGNED) {
        uint32_t val = 0;
        for (int i = 0; i < dataLen && *pos < len; i++) val = (val << 8) | buf[(*pos)++];
        return (float)val;
    } else if (tagNumber == BACNET_TAG_BOOLEAN) {
        return (float)(tag & 0x01);
    } else if (tagNumber == BACNET_TAG_NULL) {
        return NAN;
    }
    *pos += dataLen;
    return NAN;
}

bool BACnetLight::decodeObjectId(uint8_t *buf, int *pos, int len, uint16_t *type, uint32_t *instance) {
    if (len <= 0 || *pos + 4 > len) return false;
    uint32_t v = ((uint32_t)buf[*pos] << 24) | ((uint32_t)buf[*pos+1] << 16) |
                 ((uint32_t)buf[*pos+2] << 8) | buf[*pos+3];
    *type = (v >> 22) & 0x3FF;
    *instance = v & 0x3FFFFF;
    *pos += 4;
    return true;
}

uint32_t BACnetLight::decodeUnsigned(uint8_t *buf, int *pos, int len) {
    uint32_t val = 0;
    for (int i = 0; i < len; i++) val = (val << 8) | buf[(*pos)++];
    return val;
}

// ============================================================
// IP Packet Handler
// ============================================================

void BACnetLight::handleIPPacket(int packetSize) {
    if (_udp == nullptr) {
        return;
    }

    IPAddress remoteIP = _udp->remoteIP();
    uint16_t remotePort = _udp->remotePort();
    int len = _udp->read(_rxBuf, sizeof(_rxBuf));
    if (len < 4 || _rxBuf[0] != 0x81) return;

    int pos = 4;
    if (pos >= len) return;
    pos++; // NPDU version
    if (pos >= len) return;
    uint8_t npduControl = _rxBuf[pos++];

    if (npduControl & 0x20) { pos += 2; uint8_t dl = _rxBuf[pos++]; pos += dl; }
    if (npduControl & 0x08) { pos += 2; uint8_t sl = _rxBuf[pos++]; pos += sl; }
    if (npduControl & 0x10) pos++; // hop count (bit 4)

    if (pos >= len) return;
    handleAPDU(&_rxBuf[pos], len - pos, remoteIP, remotePort);
}

void BACnetLight::handleAPDU(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort) {
    if (apduLen < 2) return;
    uint8_t pduType = apdu[0] & 0xF0;

    switch (pduType) {
        case BACNET_PDU_UNCONFIRMED:
            if (apdu[1] == BACNET_SERVICE_WHO_IS) handleWhoIs(apdu, apduLen);
            break;
        case BACNET_PDU_CONFIRMED:
            if (apduLen < 4) return;
            switch (apdu[3]) {
                case BACNET_SERVICE_READ_PROPERTY:
                    handleReadProperty(apdu, apduLen, remoteIP, remotePort); break;
                case BACNET_SERVICE_READ_PROPERTY_MULTIPLE:
                    handleReadPropertyMultiple(apdu, apduLen, remoteIP, remotePort); break;
                case BACNET_SERVICE_WRITE_PROPERTY:
                    handleWriteProperty(apdu, apduLen, remoteIP, remotePort); break;
                case BACNET_SERVICE_SUBSCRIBE_COV:
                    handleSubscribeCOV(apdu, apduLen, remoteIP, remotePort); break;
            }
            break;
        case BACNET_PDU_SIMPLE_ACK:
            // apdu[1]=invokeId, apdu[2]=service — ACK for our confirmed COV notification
            if (apduLen >= 3 && apdu[2] == BACNET_SERVICE_CONFIRMED_COV_NOTIF)
                handleCOVSimpleAck(apdu[1]);
            break;
        case BACNET_PDU_ERROR:
            // apdu[1]=invokeId, apdu[2]=service — remote rejected our confirmed COV notification
            if (apduLen >= 3 && apdu[2] == BACNET_SERVICE_CONFIRMED_COV_NOTIF)
                handleCOVError(apdu[1]);
            break;
    }
}

void BACnetLight::sendIPResponse(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort) {
    _txLen = 0;
    _txBuf[_txLen++] = 0x81;
    _txBuf[_txLen++] = 0x0A;
    _txBuf[_txLen++] = 0x00;
    _txBuf[_txLen++] = 0x00;
    _txBuf[_txLen++] = 0x01; // NPDU version
    _txBuf[_txLen++] = 0x00;
    memcpy(&_txBuf[_txLen], apdu, apduLen);
    _txLen += apduLen;
    _txBuf[2] = (_txLen >> 8) & 0xFF;
    _txBuf[3] = _txLen & 0xFF;

    if (_ipEnabled) {
        if (_udp == nullptr) {
            return;
        }
        _udp->beginPacket(remoteIP, remotePort);
        _udp->write(_txBuf, _txLen);
        _udp->endPacket();
    }
}

// ============================================================
// WhoIs
// ============================================================

void BACnetLight::handleWhoIs(uint8_t *apdu, int apduLen) {
    if (apduLen > 2) {
        int pos = 2;
        uint32_t lo = 0, hi = 0;
        if (pos < apduLen) { uint8_t t = apdu[pos++]; int vl = t & 0x07;
            for (int i = 0; i < vl && pos < apduLen; i++) lo = (lo << 8) | apdu[pos++]; }
        if (pos < apduLen) { uint8_t t = apdu[pos++]; int vl = t & 0x07;
            for (int i = 0; i < vl && pos < apduLen; i++) hi = (hi << 8) | apdu[pos++]; }
        if (_deviceInstance < lo || _deviceInstance > hi) return;
    }
    sendIAm();
}

// ============================================================
// ReadProperty
// ============================================================

void BACnetLight::handleReadProperty(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort) {
    int pos = 0;
    pos++; // pdu type
    pos++; // max info
    uint8_t invokeId = apdu[pos++];
    pos++; // service choice

    uint8_t tag0 = apdu[pos++]; int tag0Len = tag0 & 0x07;
    uint16_t objectType = 0; uint32_t instance = 0;
    if (tag0Len == 4) {
        uint32_t oid = ((uint32_t)apdu[pos]<<24)|((uint32_t)apdu[pos+1]<<16)|((uint32_t)apdu[pos+2]<<8)|apdu[pos+3];
        objectType = (oid >> 22) & 0x3FF; instance = oid & 0x3FFFFF; pos += 4;
    }

    uint32_t propertyId = 0;
    if (pos < apduLen) {
        uint8_t t1 = apdu[pos++]; int t1l = t1 & 0x07;
        if (t1l == 5 && pos < apduLen) t1l = apdu[pos++];
        for (int i = 0; i < t1l && pos < apduLen; i++) propertyId = (propertyId << 8) | apdu[pos++];
    }

    uint8_t respBuf[256]; int respLen = 0;
    uint8_t valueBuf[256];
    int valueLen = encodePropertyValue(valueBuf, objectType, instance, propertyId);

    if (valueLen < 0) {
        respBuf[respLen++] = BACNET_PDU_ERROR;
        respBuf[respLen++] = invokeId;
        respBuf[respLen++] = BACNET_SERVICE_READ_PROPERTY;
        respLen += encodeAppEnumerated(&respBuf[respLen], 2);
        respLen += encodeAppEnumerated(&respBuf[respLen], 32);
    } else {
        respBuf[respLen++] = BACNET_PDU_COMPLEX_ACK;
        respBuf[respLen++] = invokeId;
        respBuf[respLen++] = BACNET_SERVICE_READ_PROPERTY;
        uint32_t oid = ((uint32_t)objectType << 22) | (instance & 0x3FFFFF);
        respLen += encodeContextTag(&respBuf[respLen], 0, oid, 4);
        if (propertyId < 0x100) respLen += encodeContextTag(&respBuf[respLen], 1, propertyId, 1);
        else respLen += encodeContextTag(&respBuf[respLen], 1, propertyId, 2);
        respLen += encodeOpeningTag(&respBuf[respLen], 3);
        memcpy(&respBuf[respLen], valueBuf, valueLen); respLen += valueLen;
        respLen += encodeClosingTag(&respBuf[respLen], 3);
    }

    sendIPResponse(respBuf, respLen, remoteIP, remotePort);
}

// ============================================================
// ReadPropertyMultiple
// ============================================================

void BACnetLight::handleReadPropertyMultiple(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort) {
    int pos = 0;
    pos++; // pdu type
    pos++; // max info
    uint8_t invokeId = apdu[pos++];
    pos++; // service choice

    uint8_t respBuf[480]; int respLen = 0;
    respBuf[respLen++] = BACNET_PDU_COMPLEX_ACK;
    respBuf[respLen++] = invokeId;
    respBuf[respLen++] = BACNET_SERVICE_READ_PROPERTY_MULTIPLE;

    // Parse each object specification
    while (pos < apduLen && respLen < 400) {
        // Object identifier (context tag 0)
        uint8_t ctag = apdu[pos++];
        if ((ctag & 0x0F) != 0x0C) break; // expect context 0, length 4

        uint16_t objectType = 0; uint32_t instance = 0;
        uint32_t oid = ((uint32_t)apdu[pos]<<24)|((uint32_t)apdu[pos+1]<<16)|
                       ((uint32_t)apdu[pos+2]<<8)|apdu[pos+3];
        objectType = (oid >> 22) & 0x3FF; instance = oid & 0x3FFFFF; pos += 4;

        // Opening tag 1 (list of property references)
        if (pos < apduLen && apdu[pos] == 0x1E) pos++;

        // Object identifier in response
        respLen += encodeOpeningTag(&respBuf[respLen], 0);
        respLen += encodeAppObjectId(&respBuf[respLen], objectType, instance);

        // List of results opening tag 1
        respLen += encodeOpeningTag(&respBuf[respLen], 1);

        // Parse each property reference
        while (pos < apduLen && apdu[pos] != 0x1F && respLen < 440) {
            uint8_t ptag = apdu[pos++];
            int plen = ptag & 0x07;
            if (plen == 5 && pos < apduLen) plen = apdu[pos++];

            uint32_t propId = 0;
            for (int i = 0; i < plen && pos < apduLen; i++)
                propId = (propId << 8) | apdu[pos++];

            // Encode result for this property
            respLen += encodeOpeningTag(&respBuf[respLen], 2);
            if (propId < 0x100)
                respLen += encodeContextTag(&respBuf[respLen], 2, propId, 1);
            else
                respLen += encodeContextTag(&respBuf[respLen], 2, propId, 2);

            uint8_t valBuf[128];
            int valLen = encodePropertyValue(valBuf, objectType, instance, propId);

            if (valLen >= 0) {
                respLen += encodeOpeningTag(&respBuf[respLen], 4);
                if (respLen + valLen < 460) {
                    memcpy(&respBuf[respLen], valBuf, valLen); respLen += valLen;
                }
                respLen += encodeClosingTag(&respBuf[respLen], 4);
            } else {
                // Error for this property
                respLen += encodeOpeningTag(&respBuf[respLen], 5);
                respLen += encodeAppEnumerated(&respBuf[respLen], 2);
                respLen += encodeAppEnumerated(&respBuf[respLen], 32);
                respLen += encodeClosingTag(&respBuf[respLen], 5);
            }
            respLen += encodeClosingTag(&respBuf[respLen], 2);
        }

        // Closing tag 1 (property references)
        if (pos < apduLen && apdu[pos] == 0x1F) pos++;

        respLen += encodeClosingTag(&respBuf[respLen], 1);
        respLen += encodeClosingTag(&respBuf[respLen], 0);
    }

    sendIPResponse(respBuf, respLen, remoteIP, remotePort);
}

// ============================================================
// WriteProperty
// ============================================================

void BACnetLight::handleWriteProperty(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort) {
    int pos = 0;
    pos++; pos++;
    uint8_t invokeId = apdu[pos++];
    uint8_t serviceChoice = apdu[pos++];

    uint8_t tag0 = apdu[pos++]; int tag0Len = tag0 & 0x07;
    uint16_t objectType = 0; uint32_t instance = 0;
    if (tag0Len == 4) {
        uint32_t oid = ((uint32_t)apdu[pos]<<24)|((uint32_t)apdu[pos+1]<<16)|
                       ((uint32_t)apdu[pos+2]<<8)|apdu[pos+3];
        objectType = (oid >> 22) & 0x3FF; instance = oid & 0x3FFFFF; pos += 4;
    }

    uint32_t propertyId = 0;
    if (pos < apduLen) {
        uint8_t t1 = apdu[pos++]; int t1l = t1 & 0x07;
        if (t1l == 5 && pos < apduLen) t1l = apdu[pos++];
        for (int i = 0; i < t1l && pos < apduLen; i++) propertyId = (propertyId << 8) | apdu[pos++];
    }

    // Find opening tag 3
    while (pos < apduLen && apdu[pos] != 0x3E) pos++;
    if (pos >= apduLen) return;
    pos++; // skip opening tag

    float newValue = NAN;
    if (pos < apduLen) newValue = decodeAppValue(apdu, &pos, apduLen);

    // Find priority (context tag 4, optional)
    uint8_t priority = 16; // default lowest
    // Skip closing tag 3
    while (pos < apduLen && apdu[pos] != 0x3F) pos++;
    if (pos < apduLen) pos++; // closing tag
    // Check for priority (context tag 4)
    if (pos < apduLen && (apdu[pos] & 0xF8) == 0x48) {
        uint8_t ptag = apdu[pos++];
        int plen = ptag & 0x07;
        priority = 0;
        for (int i = 0; i < plen && pos < apduLen; i++) priority = (priority << 8) | apdu[pos++];
    }

    uint8_t respBuf[32]; int respLen = 0;
    BACnetObject *obj = getObject(objectType, instance);
    bool accepted = false;

    if (!obj) {
        respBuf[respLen++] = BACNET_PDU_ERROR; respBuf[respLen++] = invokeId;
        respBuf[respLen++] = serviceChoice;
        respLen += encodeAppEnumerated(&respBuf[respLen], 1);
        respLen += encodeAppEnumerated(&respBuf[respLen], 31);
    } else if (propertyId != BACNET_PROP_PRESENT_VALUE) {
        respBuf[respLen++] = BACNET_PDU_ERROR; respBuf[respLen++] = invokeId;
        respBuf[respLen++] = serviceChoice;
        respLen += encodeAppEnumerated(&respBuf[respLen], 2);
        respLen += encodeAppEnumerated(&respBuf[respLen], 40);
    } else if (!obj->writable && !obj->hasPriorityArray) {
        respBuf[respLen++] = BACNET_PDU_ERROR; respBuf[respLen++] = invokeId;
        respBuf[respLen++] = serviceChoice;
        respLen += encodeAppEnumerated(&respBuf[respLen], 2);
        respLen += encodeAppEnumerated(&respBuf[respLen], 40);
    } else if (isnan(newValue) && obj->hasPriorityArray) {
        // NULL write = relinquish
        relinquish(objectType, instance, priority);
        accepted = true;
        respBuf[respLen++] = BACNET_PDU_SIMPLE_ACK;
        respBuf[respLen++] = invokeId;
        respBuf[respLen++] = serviceChoice;
    } else if (isnan(newValue)) {
        respBuf[respLen++] = BACNET_PDU_ERROR; respBuf[respLen++] = invokeId;
        respBuf[respLen++] = serviceChoice;
        respLen += encodeAppEnumerated(&respBuf[respLen], 2);
        respLen += encodeAppEnumerated(&respBuf[respLen], 9);
    } else {
        accepted = true;
        if (_writeCallback) accepted = _writeCallback(obj, newValue, priority);
        if (accepted) {
            if (obj->hasPriorityArray) {
                commandObject(objectType, instance, newValue, priority);
            } else {
                obj->presentValue = newValue;
            }
            respBuf[respLen++] = BACNET_PDU_SIMPLE_ACK;
            respBuf[respLen++] = invokeId;
            respBuf[respLen++] = serviceChoice;
        } else {
            respBuf[respLen++] = BACNET_PDU_ERROR; respBuf[respLen++] = invokeId;
            respBuf[respLen++] = serviceChoice;
            respLen += encodeAppEnumerated(&respBuf[respLen], 2);
            respLen += encodeAppEnumerated(&respBuf[respLen], 40);
        }
    }

    sendIPResponse(respBuf, respLen, remoteIP, remotePort);
}

// ============================================================
// SubscribeCOV
// ============================================================

void BACnetLight::handleSubscribeCOV(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort) {
    int pos = 0;
    pos++; pos++;
    uint8_t invokeId = apdu[pos++];
    uint8_t serviceChoice = apdu[pos++];

    uint8_t processId = 0;
    uint16_t objectType = 0; uint32_t objectInstance = 0;
    bool confirmed = false;
    uint32_t lifetime = 0;
    bool hasLifetime = false;

    // Parse context tags
    while (pos < apduLen) {
        uint8_t ctag = apdu[pos];
        if ((ctag & 0x08) == 0) break; // not context tag
        uint8_t tagNum = (ctag >> 4) & 0x0F;
        int clen = ctag & 0x07;
        pos++;
        if (clen == 5 && pos < apduLen) { clen = apdu[pos]; pos++; }

        switch (tagNum) {
            case 0: // subscriber process ID
                processId = 0;
                for (int i = 0; i < clen && pos < apduLen; i++) processId = (processId << 8) | apdu[pos++];
                break;
            case 1: // monitored object ID
                if (clen == 4) {
                    uint32_t oid = ((uint32_t)apdu[pos]<<24)|((uint32_t)apdu[pos+1]<<16)|
                                   ((uint32_t)apdu[pos+2]<<8)|apdu[pos+3];
                    objectType = (oid >> 22) & 0x3FF;
                    objectInstance = oid & 0x3FFFFF;
                    pos += 4;
                } else pos += clen;
                break;
            case 2: // confirmed notifications
                confirmed = (clen > 0 && apdu[pos] != 0);
                pos += clen;
                break;
            case 3: // lifetime
                hasLifetime = true;
                lifetime = 0;
                for (int i = 0; i < clen && pos < apduLen; i++) lifetime = (lifetime << 8) | apdu[pos++];
                break;
            default:
                pos += clen;
        }
    }

    uint8_t respBuf[16]; int respLen = 0;

    // Check if object exists
    BACnetObject *obj = getObject(objectType, objectInstance);
    if (!obj) {
        respBuf[respLen++] = BACNET_PDU_ERROR; respBuf[respLen++] = invokeId;
        respBuf[respLen++] = serviceChoice;
        respLen += encodeAppEnumerated(&respBuf[respLen], 1);
        respLen += encodeAppEnumerated(&respBuf[respLen], 31);
        sendIPResponse(respBuf, respLen, remoteIP, remotePort);
        return;
    }

    // Find or create subscription slot
    int slot = -1;
    // First check for existing subscription from same subscriber
    for (int i = 0; i < BACNET_MAX_COV_SUBSCRIPTIONS; i++) {
        if (_covSubs[i].active && _covSubs[i].subscriberIP == remoteIP &&
            _covSubs[i].subscriberProcessId == processId &&
            _covSubs[i].objectType == objectType &&
            _covSubs[i].objectInstance == objectInstance) {
            slot = i;
            break;
        }
    }

    // Cancellation: lifetime tag omitted OR lifetime == 0 (BACnet clause 13.1.2)
    if (!hasLifetime || lifetime == 0) {
        if (slot >= 0) {
            _covSubs[slot].active = false;
            // Drop any in-flight confirmed request for this slot
            for (int i = 0; i < BACNET_MAX_COV_SUBSCRIPTIONS; i++) {
                if (_pendingCOV[i].active && _pendingCOV[i].subIndex == (uint8_t)slot)
                    _pendingCOV[i].active = false;
            }
        }
        // ACK regardless — cancelling a non-existent subscription is not an error
        respBuf[respLen++] = BACNET_PDU_SIMPLE_ACK;
        respBuf[respLen++] = invokeId;
        respBuf[respLen++] = serviceChoice;
        sendIPResponse(respBuf, respLen, remoteIP, remotePort);
        return;
    }

    // Find free slot if no existing
    if (slot < 0) {
        for (int i = 0; i < BACNET_MAX_COV_SUBSCRIPTIONS; i++) {
            if (!_covSubs[i].active) { slot = i; break; }
        }
    }

    if (slot < 0) {
        // No room
        respBuf[respLen++] = BACNET_PDU_ERROR; respBuf[respLen++] = invokeId;
        respBuf[respLen++] = serviceChoice;
        respLen += encodeAppEnumerated(&respBuf[respLen], 2);
        respLen += encodeAppEnumerated(&respBuf[respLen], 42); // no-space-to-add
        sendIPResponse(respBuf, respLen, remoteIP, remotePort);
        return;
    }

    // Create/update subscription
    _covSubs[slot].active = true;
    _covSubs[slot].subscriberProcessId = processId;
    _covSubs[slot].objectType = objectType;
    _covSubs[slot].objectInstance = objectInstance;
    _covSubs[slot].issueConfirmedNotifications = confirmed;
    _covSubs[slot].lifetime = lifetime;
    _covSubs[slot].startTime = millis();
    _covSubs[slot].subscriberIP = remoteIP;
    _covSubs[slot].subscriberPort = remotePort;

    // ACK
    respBuf[respLen++] = BACNET_PDU_SIMPLE_ACK;
    respBuf[respLen++] = invokeId;
    respBuf[respLen++] = serviceChoice;
    sendIPResponse(respBuf, respLen, remoteIP, remotePort);

    // Send initial notification
    sendCOVNotification(&_covSubs[slot], obj);
}

// ============================================================
// COV Processing
// ============================================================

void BACnetLight::checkCOV() {
    for (uint8_t i = 0; i < _objectCount; i++) {
        BACnetObject *obj = &_objects[i];
        bool changed = false;

        if (obj->type == BACNET_OBJ_BINARY_VALUE || obj->type == BACNET_OBJ_BINARY_INPUT ||
            obj->type == BACNET_OBJ_BINARY_OUTPUT) {
            // Binary: any change triggers COV
            changed = ((obj->presentValue > 0.5f) != (obj->lastCovValue > 0.5f));
        } else {
            // Analog: change exceeding increment triggers COV
            changed = (fabs(obj->presentValue - obj->lastCovValue) >= obj->covIncrement);
        }

        if (changed) {
            obj->lastCovValue = obj->presentValue;

            if (_covCallback) _covCallback(obj);

            // Notify all subscribers for this object
            for (int s = 0; s < BACNET_MAX_COV_SUBSCRIPTIONS; s++) {
                if (_covSubs[s].active &&
                    _covSubs[s].objectType == obj->type &&
                    _covSubs[s].objectInstance == obj->instance) {
                    sendCOVNotification(&_covSubs[s], obj);
                }
            }
        }
    }
}

void BACnetLight::sendConfirmedCOVApdu(uint8_t invokeId, COVSubscription *sub, BACnetObject *obj) {
    uint8_t apdu[256]; int len = 0;

    apdu[len++] = BACNET_PDU_CONFIRMED;
    apdu[len++] = 0x05; // max-segments=unspecified, max-APDU=1024
    apdu[len++] = invokeId;
    apdu[len++] = BACNET_SERVICE_CONFIRMED_COV_NOTIF;

    len += encodeContextUnsigned(&apdu[len], 0, sub->subscriberProcessId);

    uint32_t devOid = ((uint32_t)BACNET_OBJ_DEVICE << 22) | (_deviceInstance & 0x3FFFFF);
    len += encodeContextTag(&apdu[len], 1, devOid, 4);

    uint32_t objOid = ((uint32_t)obj->type << 22) | (obj->instance & 0x3FFFFF);
    len += encodeContextTag(&apdu[len], 2, objOid, 4);

    uint32_t timeRemaining = 0;
    if (sub->lifetime > 0) {
        unsigned long elapsed = (millis() - sub->startTime) / 1000;
        timeRemaining = (elapsed < sub->lifetime) ? sub->lifetime - elapsed : 0;
    }
    len += encodeContextUnsigned(&apdu[len], 3, timeRemaining);

    len += encodeOpeningTag(&apdu[len], 4);
    len += encodeCOVPropertyList(&apdu[len], obj);
    len += encodeClosingTag(&apdu[len], 4);

    sendIPResponse(apdu, len, sub->subscriberIP, sub->subscriberPort);
}

void BACnetLight::sendCOVNotification(COVSubscription *sub, BACnetObject *obj) {
    if (sub->issueConfirmedNotifications) {
        // Suppress if a confirmed notification for this subscription is already in flight
        uint8_t subIdx = (uint8_t)(sub - _covSubs);
        for (int i = 0; i < BACNET_MAX_COV_SUBSCRIPTIONS; i++) {
            if (_pendingCOV[i].active && _pendingCOV[i].subIndex == subIdx)
                return;
        }

        // Allocate a pending slot
        int slot = -1;
        for (int i = 0; i < BACNET_MAX_COV_SUBSCRIPTIONS; i++) {
            if (!_pendingCOV[i].active) { slot = i; break; }
        }
        if (slot < 0) return; // no room in pending table

        uint8_t id = _invokeId++;
        _pendingCOV[slot].active     = true;
        _pendingCOV[slot].invokeId   = id;
        _pendingCOV[slot].subIndex   = subIdx;
        _pendingCOV[slot].objIndex   = (uint8_t)(obj - _objects);
        _pendingCOV[slot].sentTime   = millis();
        _pendingCOV[slot].retryCount = 0;

        sendConfirmedCOVApdu(id, sub, obj);
    } else {
        // Unconfirmed-COV-Notification
        uint8_t apdu[256]; int len = 0;
        apdu[len++] = BACNET_PDU_UNCONFIRMED;
        apdu[len++] = BACNET_SERVICE_UNCONFIRMED_COV_NOTIF;

        len += encodeContextUnsigned(&apdu[len], 0, sub->subscriberProcessId);

        uint32_t devOid = ((uint32_t)BACNET_OBJ_DEVICE << 22) | (_deviceInstance & 0x3FFFFF);
        len += encodeContextTag(&apdu[len], 1, devOid, 4);

        uint32_t objOid = ((uint32_t)obj->type << 22) | (obj->instance & 0x3FFFFF);
        len += encodeContextTag(&apdu[len], 2, objOid, 4);

        uint32_t timeRemaining = 0;
        if (sub->lifetime > 0) {
            unsigned long elapsed = (millis() - sub->startTime) / 1000;
            timeRemaining = (elapsed < sub->lifetime) ? sub->lifetime - elapsed : 0;
        }
        len += encodeContextUnsigned(&apdu[len], 3, timeRemaining);

        len += encodeOpeningTag(&apdu[len], 4);
        len += encodeCOVPropertyList(&apdu[len], obj);
        len += encodeClosingTag(&apdu[len], 4);

        sendIPResponse(apdu, len, sub->subscriberIP, sub->subscriberPort);
    }
}

void BACnetLight::expireCOVSubscriptions() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 1000) return;
    lastCheck = millis();

    for (int i = 0; i < BACNET_MAX_COV_SUBSCRIPTIONS; i++) {
        if (_covSubs[i].active && _covSubs[i].lifetime > 0) {
            unsigned long elapsed = (millis() - _covSubs[i].startTime) / 1000;
            if (elapsed >= _covSubs[i].lifetime) {
                _covSubs[i].active = false;
            }
        }
    }
}

void BACnetLight::handleCOVSimpleAck(uint8_t invokeId) {
    for (int i = 0; i < BACNET_MAX_COV_SUBSCRIPTIONS; i++) {
        if (_pendingCOV[i].active && _pendingCOV[i].invokeId == invokeId) {
            _pendingCOV[i].active = false;
            return;
        }
    }
}

void BACnetLight::handleCOVError(uint8_t invokeId) {
    for (int i = 0; i < BACNET_MAX_COV_SUBSCRIPTIONS; i++) {
        if (_pendingCOV[i].active && _pendingCOV[i].invokeId == invokeId) {
            // Subscriber explicitly rejected the notification — cancel the subscription
            _covSubs[_pendingCOV[i].subIndex].active = false;
            _pendingCOV[i].active = false;
            return;
        }
    }
}

void BACnetLight::retryPendingCOV() {
    unsigned long now = millis();
    for (int i = 0; i < BACNET_MAX_COV_SUBSCRIPTIONS; i++) {
        if (!_pendingCOV[i].active) continue;
        if (now - _pendingCOV[i].sentTime < BACNET_COV_TIMEOUT_MS) continue;

        if (_pendingCOV[i].retryCount >= BACNET_COV_MAX_RETRIES ||
            !_covSubs[_pendingCOV[i].subIndex].active) {
            // Retries exhausted or subscription already gone — cancel both
            _covSubs[_pendingCOV[i].subIndex].active = false;
            _pendingCOV[i].active = false;
            continue;
        }

        // Retransmit with the original invoke ID (BACnet clause 5.4.4)
        _pendingCOV[i].retryCount++;
        _pendingCOV[i].sentTime = now;
        sendConfirmedCOVApdu(_pendingCOV[i].invokeId,
                             &_covSubs[_pendingCOV[i].subIndex],
                             &_objects[_pendingCOV[i].objIndex]);
    }
}
