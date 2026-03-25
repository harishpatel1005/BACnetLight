/*
 * BACnetLight - BACnet/MSTP Implementation
 * RS485 transport layer with token-passing state machine
 * License: MIT
 */

#include "BACnetLight.h"

// MSTP timing constants (in milliseconds)
#define MSTP_T_FRAME_ABORT     3     // Max time between bytes in a frame
#define MSTP_T_NO_TOKEN        500   // Time without token before declaring lost
#define MSTP_T_REPLY_TIMEOUT   300   // Time to wait for a reply
#define MSTP_T_USAGE_TIMEOUT   50    // Max time to hold token for data
#define MSTP_T_REPLY_DELAY     1     // Min delay before reply
#define MSTP_N_POLL_STATION    50    // Token passes before polling
#define MSTP_N_RETRY_TOKEN     1     // Token retries before passing
#define MSTP_N_MAX_INFO_FRAMES 1     // Data frames per token

#define MSTP_PREAMBLE1  0x55
#define MSTP_PREAMBLE2  0xFF

// ============================================================
// CRC Tables
// ============================================================

static const uint8_t HeaderCRC_Table[256] = {
    0x00,0xFE,0xFF,0x01,0xFD,0x03,0x02,0xFC,0xF9,0x07,0x06,0xF8,0x04,0xFA,0xFB,0x05,
    0xF1,0x0F,0x0E,0xF0,0x0C,0xF2,0xF3,0x0D,0x08,0xF6,0xF7,0x09,0xF5,0x0B,0x0A,0xF4,
    0xE1,0x1F,0x1E,0xE0,0x1C,0xE2,0xE3,0x1D,0x18,0xE6,0xE7,0x19,0xE5,0x1B,0x1A,0xE4,
    0x10,0xEE,0xEF,0x11,0xED,0x13,0x12,0xEC,0xE9,0x17,0x16,0xE8,0x14,0xEA,0xEB,0x15,
    0xC1,0x3F,0x3E,0xC0,0x3C,0xC2,0xC3,0x3D,0x38,0xC6,0xC7,0x39,0xC5,0x3B,0x3A,0xC4,
    0x30,0xCE,0xCF,0x31,0xCD,0x33,0x32,0xCC,0xC9,0x37,0x36,0xC8,0x34,0xCA,0xCB,0x35,
    0x20,0xDE,0xDF,0x21,0xDD,0x23,0x22,0xDC,0xD9,0x27,0x26,0xD8,0x24,0xDA,0xDB,0x25,
    0xD1,0x2F,0x2E,0xD0,0x2C,0xD2,0xD3,0x2D,0x28,0xD6,0xD7,0x29,0xD5,0x2B,0x2A,0xD4,
    0x81,0x7F,0x7E,0x80,0x7C,0x82,0x83,0x7D,0x78,0x86,0x87,0x79,0x85,0x7B,0x7A,0x84,
    0x70,0x8E,0x8F,0x71,0x8D,0x73,0x72,0x8C,0x89,0x77,0x76,0x88,0x74,0x8A,0x8B,0x75,
    0x60,0x9E,0x9F,0x61,0x9D,0x63,0x62,0x9C,0x99,0x67,0x66,0x98,0x64,0x9A,0x9B,0x65,
    0x91,0x6F,0x6E,0x90,0x6C,0x92,0x93,0x6D,0x68,0x96,0x97,0x69,0x95,0x6B,0x6A,0x94,
    0x40,0xBE,0xBF,0x41,0xBD,0x43,0x42,0xBC,0xB9,0x47,0x46,0xB8,0x44,0xBA,0xBB,0x45,
    0xB1,0x4F,0x4E,0xB0,0x4C,0xB2,0xB3,0x4D,0x48,0xB6,0xB7,0x49,0xB5,0x4B,0x4A,0xB4,
    0x50,0xAE,0xAF,0x51,0xAD,0x53,0x52,0xAC,0xA9,0x57,0x56,0xA8,0x54,0xAA,0xAB,0x55,
    0xA1,0x5F,0x5E,0xA0,0x5C,0xA2,0xA3,0x5D,0x58,0xA6,0xA7,0x59,0xA5,0x5B,0x5A,0xA4
};

// ============================================================
// Constructor
// ============================================================

BACnetMSTP::BACnetMSTP() : BACnetLight() {
    _mstpSerial = nullptr;
    _dePin = 0;
    _macAddress = 0;
    _maxMaster = 127;
    _nextStation = 0;
    _mstpEnabled = false;
    _mstpState = MSTP_IDLE;
    _hasToken = false;
    _tokenTimer = 0;
    _silenceTimer = 0;
    _tokenCount = 0;
    _retryCount = 0;
    _framePending = 0;
    _mstpRxLen = 0;
}

// ============================================================
// Initialization
// ============================================================

bool BACnetMSTP::beginMSTP(uint32_t deviceInstance, const char *deviceName,
                            HardwareSerial &serial, uint8_t dePin, uint8_t macAddress,
                            uint32_t baud, uint8_t maxMaster) {
    _deviceInstance = deviceInstance;
    strncpy(_deviceName, deviceName, BACNET_MAX_NAME_LEN - 1);
    _deviceName[BACNET_MAX_NAME_LEN - 1] = '\0';

    _mstpSerial = &serial;
    _dePin = dePin;
    _macAddress = macAddress;
    _maxMaster = maxMaster;
    _nextStation = (_macAddress + 1) % (_maxMaster + 1);

    pinMode(_dePin, OUTPUT);
    setTxMode(false); // Start in receive mode

    _mstpSerial->begin(baud, SERIAL_8N1);

    _mstpEnabled = true;
    _mstpState = MSTP_NO_TOKEN;
    _silenceTimer = millis();
    _tokenTimer = millis();
    _tokenCount = 0;

    return true;
}

bool BACnetMSTP::beginDual(uint32_t deviceInstance, const char *deviceName,
                            IPAddress targetIP,
                            HardwareSerial &serial, uint8_t dePin, uint8_t macAddress,
                            uint32_t baud) {
    // Init IP side
    bool ipOk = begin(deviceInstance, deviceName, targetIP);

    // Init MSTP side
    beginMSTP(deviceInstance, deviceName, serial, dePin, macAddress, baud);

    return ipOk;
}

bool BACnetMSTP::beginDual(uint32_t deviceInstance, const char *deviceName,
                            IPAddress targetIP, UDP &transport,
                            HardwareSerial &serial, uint8_t dePin, uint8_t macAddress,
                            uint32_t baud) {
    bool ipOk = begin(deviceInstance, deviceName, targetIP, transport);
    beginMSTP(deviceInstance, deviceName, serial, dePin, macAddress, baud);
    return ipOk;
}

// ============================================================
// Main Loop
// ============================================================

void BACnetMSTP::loop() {
    // Process IP
    BACnetLight::loop();

    // Process MSTP
    if (_mstpEnabled) {
        mstpReceive();
        mstpStateMachine();
    }
}

// ============================================================
// RS485 Direction Control
// ============================================================

void BACnetMSTP::setTxMode(bool tx) {
    digitalWrite(_dePin, tx ? HIGH : LOW);
    if (tx) delayMicroseconds(100); // Settling time
}

// ============================================================
// CRC Calculations
// ============================================================

uint8_t BACnetMSTP::calcHeaderCRC(uint8_t *buf, int len) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc = HeaderCRC_Table[crc ^ buf[i]];
    }
    return ~crc;
}

uint16_t BACnetMSTP::calcDataCRC(uint8_t *buf, int len) {
    // BACnet MSTP data CRC uses CRC-16/IBM (poly 0xA001, reflected)
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= (uint8_t)buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

// ============================================================
// MSTP Frame Send
// ============================================================

void BACnetMSTP::mstpSendFrame(uint8_t frameType, uint8_t dest, uint8_t *data, int dataLen) {
    uint8_t header[8];
    header[0] = MSTP_PREAMBLE1;
    header[1] = MSTP_PREAMBLE2;
    header[2] = frameType;
    header[3] = dest;
    header[4] = _macAddress;
    header[5] = (dataLen >> 8) & 0xFF;
    header[6] = dataLen & 0xFF;

    // Header CRC (over bytes 2-6)
    header[7] = calcHeaderCRC(&header[2], 5);

    setTxMode(true);
    _mstpSerial->write(header, 8);

    if (dataLen > 0 && data) {
        _mstpSerial->write(data, dataLen);
        // Data CRC
        uint16_t dataCrc = calcDataCRC(data, dataLen);
        _mstpSerial->write(dataCrc & 0xFF);
        _mstpSerial->write((dataCrc >> 8) & 0xFF);
    }

    _mstpSerial->flush(); // Wait for TX complete
    setTxMode(false);

    _silenceTimer = millis();
}

void BACnetMSTP::mstpSendToken(uint8_t dest) {
    mstpSendFrame(MSTP_FRAME_TOKEN, dest, nullptr, 0);
}

// ============================================================
// MSTP Frame Receive
// ============================================================

void BACnetMSTP::mstpReceive() {
    while (_mstpSerial->available()) {
        uint8_t b = _mstpSerial->read();
        _silenceTimer = millis();

        if (_mstpRxLen == 0 && b != MSTP_PREAMBLE1) continue;
        if (_mstpRxLen == 1 && b != MSTP_PREAMBLE2) { _mstpRxLen = 0; continue; }

        _mstpRxBuf[_mstpRxLen++] = b;

        // Check if we have complete header (8 bytes)
        if (_mstpRxLen == 8) {
            uint8_t headerCrc = calcHeaderCRC(&_mstpRxBuf[2], 5);
            if (headerCrc != _mstpRxBuf[7]) {
                _mstpRxLen = 0; // Bad CRC
                continue;
            }

            int dataLen = (_mstpRxBuf[5] << 8) | _mstpRxBuf[6];
            if (dataLen == 0) {
                // No data frame - process immediately
                uint8_t frameType = _mstpRxBuf[2];
                uint8_t src = _mstpRxBuf[4];
                uint8_t dest = _mstpRxBuf[3];

                mstpHandleFrame(frameType, src, dest, nullptr, 0);
                _mstpRxLen = 0;
            }
            // Else wait for data + 2 byte CRC
        }

        // Check for complete data frame
        if (_mstpRxLen >= 8) {
            int dataLen = (_mstpRxBuf[5] << 8) | _mstpRxBuf[6];
            int expectedTotal = 8 + dataLen + 2; // header + data + data CRC

            if (_mstpRxLen >= expectedTotal) {
                // Validate data CRC before processing (BACnet Clause 9.3)
                uint16_t calcCrc = calcDataCRC(&_mstpRxBuf[8], dataLen);
                uint16_t rxCrc   = (uint16_t)_mstpRxBuf[8 + dataLen]
                                 | ((uint16_t)_mstpRxBuf[8 + dataLen + 1] << 8);
                _mstpRxLen = 0;
                if (calcCrc != rxCrc) {
                    continue; // Corrupted payload — discard silently
                }

                uint8_t frameType = _mstpRxBuf[2];
                uint8_t src = _mstpRxBuf[4];
                uint8_t dest = _mstpRxBuf[3];

                mstpHandleFrame(frameType, src, dest, &_mstpRxBuf[8], dataLen);
            }
        }

        // Prevent buffer overflow
        if (_mstpRxLen >= BACNET_BUF_SIZE - 2) _mstpRxLen = 0;
    }
}

// ============================================================
// MSTP Frame Handler
// ============================================================

void BACnetMSTP::mstpHandleFrame(uint8_t frameType, uint8_t src, uint8_t dest,
                                   uint8_t *data, int dataLen) {
    switch (frameType) {
        case MSTP_FRAME_TOKEN:
            if (dest == _macAddress) {
                _hasToken = true;
                _tokenTimer = millis();
                _tokenCount++;
                _mstpState = MSTP_USE_TOKEN;
            }
            break;

        case MSTP_FRAME_POLL_FOR_MASTER:
            if (dest == _macAddress) {
                // Reply with "I'm here"
                mstpSendFrame(MSTP_FRAME_REPLY_TO_PFM, src, nullptr, 0);
            }
            break;

        case MSTP_FRAME_BACNET_DATA_EXPECTING_REPLY:
            if (dest == _macAddress || dest == 0xFF) {
                mstpHandleDataFrame(src, data, dataLen, true);
            }
            break;

        case MSTP_FRAME_BACNET_DATA_NOT_EXPECTING:
            if (dest == _macAddress || dest == 0xFF) {
                mstpHandleDataFrame(src, data, dataLen, false);
            }
            break;

        case MSTP_FRAME_REPLY_TO_PFM:
            // A new station responded to our poll
            break;

        case MSTP_FRAME_TEST_REQUEST:
            if (dest == _macAddress) {
                mstpSendFrame(MSTP_FRAME_TEST_RESPONSE, src, data, dataLen);
            }
            break;
    }
}

void BACnetMSTP::mstpHandleDataFrame(uint8_t src, uint8_t *data, int dataLen, bool expectingReply) {
    if (dataLen < 3) return;

    // NPDU starts at data[0]
    uint8_t npduVersion = data[0];
    uint8_t npduControl = data[1];
    int pos = 2;

    // Skip DNET/SNET if present
    if (npduControl & 0x20) { pos += 2; uint8_t dl = data[pos++]; pos += dl; }
    if (npduControl & 0x08) { pos += 2; uint8_t sl = data[pos++]; pos += sl; }
    if (npduControl & 0x10) pos++; // hop count (bit 4)

    if (pos >= dataLen) return;

    // Handle APDU
    // Clear any previous response so a no-reply request cannot resend stale data
    _txLen = 0;
    // For MSTP, we use a dummy IP for response tracking
    IPAddress mstpDummy(0, 0, 0, src);
    handleAPDU(&data[pos], dataLen - pos, mstpDummy, src);

    // If we have a response and it's expecting reply, send it via MSTP
    // The response was built in _txBuf by handleAPDU -> sendIPResponse
    // We need to re-route it over MSTP instead
    if (expectingReply && _txLen > 6) {
        // Strip BVLC header (4 bytes), send NPDU+APDU via MSTP
        mstpSendFrame(MSTP_FRAME_BACNET_DATA_NOT_EXPECTING, src, &_txBuf[4], _txLen - 4);
    }
}

// ============================================================
// MSTP State Machine
// ============================================================

void BACnetMSTP::mstpStateMachine() {
    unsigned long now = millis();

    switch (_mstpState) {
        case MSTP_IDLE:
            // Waiting for frame or token timeout
            if (now - _silenceTimer > MSTP_T_NO_TOKEN) {
                // No token seen - try to grab it
                _mstpState = MSTP_NO_TOKEN;
            }
            break;

        case MSTP_NO_TOKEN:
            // Haven't received token in a while
            if (now - _silenceTimer > MSTP_T_NO_TOKEN * 2) {
                // Generate token ourselves (we might be the only device)
                _hasToken = true;
                _tokenTimer = now;
                _mstpState = MSTP_USE_TOKEN;
            }
            break;

        case MSTP_USE_TOKEN:
            // We have the token - send any pending data or pass it
            if (now - _tokenTimer < MSTP_T_USAGE_TIMEOUT) {
                // We could send queued data here
                // For now, just pass the token
            }
            _mstpState = MSTP_PASS_TOKEN;
            break;

        case MSTP_PASS_TOKEN:
            // Pass token to next station
            mstpSendToken(_nextStation);
            _hasToken = false;
            _mstpState = MSTP_IDLE;
            _silenceTimer = now;

            // Periodically poll for new masters
            if (_tokenCount >= MSTP_N_POLL_STATION) {
                _tokenCount = 0;
                _mstpState = MSTP_POLL_FOR_MASTER;
            }
            break;

        case MSTP_POLL_FOR_MASTER: {
            // Poll next address after our next station
            uint8_t pollAddr = (_nextStation + 1) % (_maxMaster + 1);
            if (pollAddr != _macAddress) {
                mstpSendFrame(MSTP_FRAME_POLL_FOR_MASTER, pollAddr, nullptr, 0);
            }
            _mstpState = MSTP_IDLE;
            _silenceTimer = now;
            break;
        }

        default:
            _mstpState = MSTP_IDLE;
            break;
    }
}

// ============================================================
// sendIPResponse Override
// Builds the standard BVLC/NPDU wrapper into _txBuf.
// Sends via UDP only when IP is active (dual-port mode).
// When called during MSTP frame processing, mstpHandleDataFrame
// reads _txBuf and re-routes the response over RS485.
// ============================================================

void BACnetMSTP::sendIPResponse(uint8_t *apdu, int apduLen, IPAddress remoteIP, uint16_t remotePort) {
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

    if (_ipEnabled && _udp != nullptr) {
        _udp->beginPacket(remoteIP, remotePort);
        _udp->write(_txBuf, _txLen);
        _udp->endPacket();
    }
}
