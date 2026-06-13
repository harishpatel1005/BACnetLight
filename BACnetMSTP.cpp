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
    _mstpTxPending = false;
    _mstpTxDest = 0xFF;
    _mstpTxDataLen = 0;
    _lastIAmMs = 0;
    memset(_masterMap, 0, sizeof(_masterMap));
    _pollAddr = 0;
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

    // The caller is responsible for calling serial.begin() with the
    // correct baud rate and pin mapping *before* calling beginMSTP().
    // Re-initialising the port here would reset ESP32 RX/TX pins to
    // their defaults, breaking any custom pin assignment.

    _mstpEnabled = true;
    _mstpState = MSTP_NO_TOKEN;
    _silenceTimer = millis();
    _tokenTimer = millis();
    _tokenCount = 0;

    sendIAm();
    _lastIAmMs = millis();
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

        if (millis() - _lastIAmMs >= 60000UL) {
            _lastIAmMs = millis();
            sendIAm();
        }
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
    // ASHRAE 135-2016 Clause 9.3.2 header CRC, computed bit-by-bit.
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        uint16_t c = (uint16_t)(crc ^ buf[i]);
        c = c ^ (c << 1) ^ (c << 2) ^ (c << 3)
              ^ (c << 4) ^ (c << 5) ^ (c << 6) ^ (c << 7);
        crc = (uint8_t)((c & 0xFE) ^ ((c >> 8) & 1));
    }
    return ~crc;
}

uint16_t BACnetMSTP::calcDataCRC(uint8_t *buf, int len) {
    // ASHRAE 135-2016 Clause 9.3.3 data CRC: CRC-CCITT, polynomial
    // X^16 + X^12 + X^5 + 1 (0x1021), reflected form 0x8408.
    // Init 0xFFFF, final one's-complement, emitted low byte then high byte.
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= (uint8_t)buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0x8408;
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
                    continue; // Corrupted payload -- discard silently
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
    // Any frame we hear identifies its source as a live master on the bus.
    markMaster(src);

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
            // Only a unicast request may be answered. A broadcast
            // Data-Expecting-Reply is processed but never replied to
            // (you cannot reply to a broadcast, and doing so collides).
            if (dest == _macAddress) {
                mstpHandleDataFrame(src, data, dataLen, true);
            } else if (dest == 0xFF) {
                mstpHandleDataFrame(src, data, dataLen, false);
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
    if (data[0] != 0x01) return; // reject non-v1 NPDUs
    uint8_t npduControl = data[1];
    int pos = 2;

    // Skip DNET/DADR if present (bit 5)
    if (npduControl & 0x20) {
        if (pos + 3 > dataLen) return;
        pos += 2;
        uint8_t dl = data[pos++];
        if (pos + dl > dataLen) return;
        pos += dl;
    }
    // Skip SNET/SADR if present (bit 3)
    if (npduControl & 0x08) {
        if (pos + 3 > dataLen) return;
        pos += 2;
        uint8_t sl = data[pos++];
        if (pos + sl > dataLen) return;
        pos += sl;
    }
    // Skip hop count if DNET present
    if (npduControl & 0x20) {
        if (pos >= dataLen) return;
        pos++;
    }

    if (pos >= dataLen) return;

    // Handle APDU
    // Clear any previous response so a no-reply request cannot resend stale data
    _txLen = 0;
    // For MSTP, we use a dummy IP for response tracking
    IPAddress mstpDummy(0, 0, 0, src);
    _processingMSTP = true;
    handleAPDU(&data[pos], dataLen - pos, mstpDummy, src);
    _processingMSTP = false;

    // A confirmed-request reply is time-critical: the requesting node is
    // sitting in WAIT_FOR_REPLY and will abandon the transaction after
    // Treply_timeout (~255 ms). It does NOT wait for us to next hold the
    // token, so the reply must be sent NOW, directly, while we still own the
    // medium. (Deferring it to the next token hold -- the old behaviour --
    // meant every ReadProperty silently timed out, which is why YABE could
    // discover the device but never read its object list.)
    //
    // The response APDU lives in _txBuf as [BVLC(4)][NPDU(01 00)][APDU];
    // skip the 4-byte BVLC and transmit the NPDU+APDU as the MSTP payload.
    if (expectingReply && _txLen > 6) {
        int replyLen = _txLen - 4;
        mstpSendFrame(MSTP_FRAME_BACNET_DATA_NOT_EXPECTING, src,
                      &_txBuf[4], replyLen);
    }
}

// ============================================================
// Known-master tracking
// ============================================================

void BACnetMSTP::markMaster(uint8_t mac) {
    if (mac < 128 && mac != _macAddress)
        _masterMap[mac >> 3] |= (uint8_t)(1 << (mac & 7));
}

bool BACnetMSTP::isMaster(uint8_t mac) {
    if (mac >= 128) return false;
    return (_masterMap[mac >> 3] & (uint8_t)(1 << (mac & 7))) != 0;
}

int BACnetMSTP::nextKnownMaster() {
    // Walk circularly from the address just above ours; return the first known
    // master. This is the MS/TP successor (next master in address order).
    for (int i = 1; i <= _maxMaster + 1; i++) {
        uint8_t cand = (uint8_t)((_macAddress + i) % (_maxMaster + 1));
        if (cand == _macAddress) continue;
        if (isMaster(cand)) return cand;
    }
    return -1;
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
            if (_mstpTxPending) {
                mstpSendFrame(MSTP_FRAME_BACNET_DATA_NOT_EXPECTING,
                              _mstpTxDest, _mstpTxBuf, _mstpTxDataLen);
                _mstpTxPending = false;
            }
            _mstpState = MSTP_PASS_TOKEN;
            break;

        case MSTP_PASS_TOKEN: {
            int ns = nextKnownMaster();
            if (ns < 0) {
                // No peer discovered yet. Stay sole master: drop to idle and let
                // NO_TOKEN recovery keep us alive until another master (e.g. the
                // BACnet client) polls us or sends us the token. Marching through
                // every dead address here is what made the token take minutes to
                // return to the client, starving ReadProperty.
                _hasToken = false;
                _mstpState = MSTP_IDLE;
                _silenceTimer = now;
                break;
            }

            _nextStation = (uint8_t)ns;
            mstpSendToken(_nextStation);
            _hasToken = false;
            _mstpState = MSTP_IDLE;
            _silenceTimer = now;

            // Periodically poll for masters that may have joined since.
            if (_tokenCount >= MSTP_N_POLL_STATION) {
                _tokenCount = 0;
                _mstpState = MSTP_POLL_FOR_MASTER;
            }
            break;
        }

        case MSTP_POLL_FOR_MASTER: {
            // Probe one address per visit; a Reply-To-PFM (or any frame) from it
            // is recorded by markMaster() and becomes a token target.
            do {
                _pollAddr = (_pollAddr + 1) % (_maxMaster + 1);
            } while (_pollAddr == _macAddress);
            mstpSendFrame(MSTP_FRAME_POLL_FOR_MASTER, _pollAddr, nullptr, 0);
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
    if (apduLen < 0 || (apduLen + 6) > BACNET_BUF_SIZE) {
        _txLen = 0;
        return;
    }

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

    if (_ipEnabled && !_processingMSTP && _udp != nullptr) {
        _udp->beginPacket(remoteIP, remotePort);
        _udp->write(_txBuf, _txLen);
        _udp->endPacket();
    }
}

// ============================================================
// sendIAm Override
// Builds the I-Am via the base class (which also handles UDP in
// dual-port mode), then queues the NPDU+APDU for the next token.
// ============================================================

void BACnetMSTP::sendIAm() {
    BACnetLight::sendIAm(); // builds _txBuf and handles UDP in dual-port mode

    if (!_mstpEnabled || _txLen <= 10) return;

    // _txBuf layout: [0..3]=BVLC, [4..9]=6-byte global-broadcast NPDU, [10..]=APDU.
    // The IP-form NPDU (DNET=0xFFFF) is wrong for a local MSTP broadcast.
    // Build the MSTP payload with a 2-byte local-broadcast NPDU instead.
    int apduLen = _txLen - 10;
    int n = 2 + apduLen;
    if (n > (int)sizeof(_mstpTxBuf)) return;

    _mstpTxBuf[0] = 0x01; // NPDU version
    _mstpTxBuf[1] = 0x00; // local broadcast, no special flags
    memcpy(&_mstpTxBuf[2], &_txBuf[10], apduLen);

    _mstpTxDataLen = n;
    _mstpTxDest    = 0xFF;
    _mstpTxPending = true;
}
