/*
  AcaiaArduinoBLE.cpp - Library for connecting to
  an Acaia Scale using the ArduinoBLE library.
  Created by Tate Mazer, December 13, 2023.
  Released into the public domain.

  Adding Generic Scale Support, Pio Baettig
  Fixing Felicita Arc Bug, A-TWJ

*/
#include "Arduino.h"
#include "AcaiaArduinoBLE.h"
#include <ArduinoBLE.h>

byte IDENTIFY[20]                   = { 0xef, 0xdd, 0x0b, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x9a, 0x6d };
byte HEARTBEAT[7]                   = { 0xef, 0xdd, 0x00, 0x02, 0x00, 0x02, 0x00 };
byte NOTIFICATION_REQUEST[14]       = { 0xef, 0xdd, 0x0c, 0x09, 0x00, 0x01, 0x01, 0x02, 0x02, 0x05, 0x03, 0x04, 0x15, 0x06 };
byte START_TIMER[7]                 = { 0xef, 0xdd, 0x0d, 0x00, 0x00, 0x00, 0x00 };
byte STOP_TIMER[7]                  = { 0xef, 0xdd, 0x0d, 0x00, 0x02, 0x00, 0x02 };
byte RESET_TIMER[7]                 = { 0xef, 0xdd, 0x0d, 0x00, 0x01, 0x00, 0x01 };
byte TARE_ACAIA[6]                  = { 0xef, 0xdd, 0x04, 0x00, 0x00, 0x00 };
byte TARE_GENERIC[6]                = { 0x03, 0x0a, 0x01, 0x00, 0x00, 0x08 };
byte START_TIMER_GENERIC[6]         = { 0x03, 0x0a, 0x04, 0x00, 0x00, 0x0a };
byte STOP_TIMER_GENERIC[6]          = { 0x03, 0x0a, 0x05, 0x00, 0x00, 0x0d };
byte RESET_TIMER_GENERIC[6]         = { 0x03, 0x0a, 0x06, 0x00, 0x00, 0x0c };
byte TARE_START_TIMER_BOOKOO[6]     = { 0x03, 0x0a, 0x07, 0x00, 0x00, 0x00 };
byte BEEP_LEVEL_1_BOOKOO[6]         = { 0x03, 0x0a, 0x02, 0x00, 0x01, 0x0a };
byte TARE_FELICITA[1]               = { 0x54 }; // 'Tare'
byte START_TIMER_FELICITA[1]        = { 0x52 }; // 'Start_Timer'
byte STOP_TIMER_FELICITA[1]         = { 0x53 }; // 'Stop_Timer'
byte RESET_TIMER_FELICITA[1]        = { 0x43 }; // 'Reset_Timer'
byte WEIGHT_TIMER_MODE_FELICITA[1]  = { 0x32 }; // 'Weight+timer mode (known-good wake state)

// Acaia framing: ef dd <cmd> <len> ... <ck1> <ck2>
//   total message length = len + 5
//   ck1/ck2 = sums of the even/odd bytes from <len> up to (not including) ck1
#define ACAIA_HEADER1      0xef
#define ACAIA_HEADER2      0xdd
#define ACAIA_CMD_EVENT    0x0c
#define ACAIA_CMD_SETTINGS 0x08
#define ACAIA_EVENT_WEIGHT 0x05
#define ACAIA_EVENT_TIMER  0x07
#define ACAIA_EVENT_ACK    0x0b   // carries a weight or timer value (see pyacaia)

// Helper struct layout to enable BLEWriteWithoutResponse (0x04) in stock ArduinoBLE.
// Matches ArduinoBLE 2.x (BLECharacteristic / BLERemoteAttribute / BLERemoteCharacteristic).
struct RemoteCharAccess {
    void* vtable;
    String uuid;
    int refCount;
    uint16_t connectionHandle;
    uint16_t startHandle;
    uint8_t properties;
};
struct CharAccess {
    void* vtable;
    void* local;
    RemoteCharAccess* remote;
};

/* Generic commands from
   https://github.com/graphefruit/Beanconqueror/blob/master/src/classes/devices/felicita/constants.ts
*/

AcaiaArduinoBLE* AcaiaArduinoBLE::_instance = nullptr;

AcaiaArduinoBLE::AcaiaArduinoBLE(bool debug){
    _debug = debug;
    _currentWeight = 0;
    _newWeight = false;
    _connected = false;
    _scanning = false;
    _scanStarted = 0;
    _type = NEW;
    _packetPeriod = 0;
    _lastPacket = 0;
    _lastHeartBeat = 0;
    _lastCommand = 0;
    _rxLen = 0;
    _checksumErrors = 0;
    _tareState = TARE_IDLE;
    _tareSent = 0;
    _tareRetries = 0;
    _timerState = TIMER_IDLE;
    _timerStartSent = 0;
    _timerRetries = 0;
    _scaleTimer = NAN;
    _timerAtStart = NAN;
    _lastTimerReport = 0;
    _battery = -1;
    _ounces = false;
    _scaleFlow = NAN;
    _instance = this;
}

// Called after a connection has been made but setup failed. Always drop the
// link: a scale that stays connected stops advertising and can never be found
// again by the next scan.
bool AcaiaArduinoBLE::fail(BLEDevice& peripheral, const char* msg){
    Serial.println(msg);
    peripheral.disconnect();
    return false;
}

bool AcaiaArduinoBLE::init(String mac){
    if(_connected){
        return true;
    }

    // Keep scanning between calls so advertisements that arrive while the
    // caller is doing other work are not missed. Restart the scan
    // periodically to clear the controller's duplicate filter.
    unsigned long now = millis();
    if(!_scanning || now - _scanStarted > SCAN_RESTART_MS){
        if(!_scanning){
            Serial.print("AcaiaArduinoBLE Library v");
            Serial.print(LIBRARY_VERSION);
            Serial.println(" scanning...");
        }else{
            BLE.stopScan();
        }
        int ok = (mac == "") ? BLE.scan() : BLE.scanForAddress(mac);
        if(!ok){
            Serial.println("failed to start scan");
            _scanning = false;
            return false;
        }
        _scanning = true;
        _scanStarted = now;
    }

    unsigned long start = millis();
    do{
        BLEDevice peripheral = BLE.available();
        if(!peripheral){
            continue;
        }

        if(_debug){
            // discovered a peripheral, print out address, local name, and advertised service
            Serial.print("Found ");
            Serial.print(peripheral.address());
            Serial.print(" '");
            Serial.print(peripheral.localName());
            Serial.print("' ");
            Serial.print(peripheral.advertisedServiceUuid());
            Serial.println();
        }

        if(!isScaleName(peripheral.localName())){
            continue;
        }

        BLE.stopScan();
        _scanning = false;

        Serial.println("Connecting ...");
        if (peripheral.connect()) {
            Serial.println("Connected");
        } else {
            Serial.println("Failed to connect!");
            return false;
        }

        // From here on every failure must disconnect (see fail()).
        Serial.println("Discovering attributes ...");
        if (peripheral.discoverAttributes()) {
            Serial.println("Attributes discovered");
        } else {
            return fail(peripheral, "Attribute discovery failed!");
        }

        if(_debug){
            // read and print device name of peripheral
            Serial.println();
            Serial.print("Device name: ");
            Serial.println(peripheral.deviceName());
            Serial.print("Appearance: 0x");
            Serial.println(peripheral.appearance(), HEX);
            Serial.println();

            // loop the services of the peripheral and explore each
            for (int i = 0; i < peripheral.serviceCount(); i++) {
                BLEService service = peripheral.service(i);
                exploreService(service);
            }
        }

        // Determine type of scale
        if(peripheral.characteristic(READ_CHAR_OLD_VERSION).canSubscribe()){
            Serial.println("Old version Acaia Detected");
            _type   = OLD;
            _write  = peripheral.characteristic(WRITE_CHAR_OLD_VERSION);
            _read   = peripheral.characteristic(READ_CHAR_OLD_VERSION);
        }else if(peripheral.characteristic(READ_CHAR_NEW_VERSION).canSubscribe()){
            Serial.println("New version Acaia Detected");
            _type   = NEW;
            _write  = peripheral.characteristic(WRITE_CHAR_NEW_VERSION);
            _read   = peripheral.characteristic(READ_CHAR_NEW_VERSION);
        } else if(peripheral.characteristic(READ_CHAR_GENERIC).canSubscribe()){
            Serial.println("Generic Scale Detected");
            _type   = GENERIC;
            _write  = peripheral.characteristic(WRITE_CHAR_GENERIC);
            _read   = peripheral.characteristic(READ_CHAR_GENERIC);
        } else if(peripheral.characteristic(READ_CHAR_FELICITA).canSubscribe()){
            Serial.println("Felicita Arc Detected");
            _type   = FELICITA;
            _write  = peripheral.characteristic(WRITE_CHAR_FELICITA);
            _read   = peripheral.characteristic(READ_CHAR_FELICITA);
        }
        else{
            return fail(peripheral, "unable to determine scale type");
        }

        if(!_write){
            return fail(peripheral, "write characteristic not found");
        }

        if(_debug){
            Serial.print("write characteristic properties 0x");
            Serial.println(_write.properties(), HEX);
        }

        // Fire-and-forget commands (heartbeat, timer) use write-without-response.
        // Stock ArduinoBLE refuses that unless the characteristic advertises
        // BLEWriteWithoutResponse (0x04), so set the flag only when it is missing.
        if(!(_write.properties() & BLEWriteWithoutResponse)){
            CharAccess* ca = (CharAccess*)(void*)&_write;
            if (ca && ca->remote) {
                ca->remote->properties |= BLEWriteWithoutResponse;
                if(_debug){
                    Serial.println("forced write-without-response on write characteristic");
                }
            }
        }

        // Reset per-connection state before notifications can start arriving.
        _rxLen = 0;
        _newWeight = false;
        _tareState = TARE_IDLE;
        _tareRetries = 0;
        _packetPeriod = 0;
        _lastPacket = 0;
        _timerState = TIMER_IDLE;
        _timerRetries = 0;
        _scaleTimer = NAN;
        _lastTimerReport = 0;
        _battery = -1;
        _ounces = false;
        _scaleFlow = NAN;

        // Every notification is delivered to onNotify() from inside the BLE
        // stack, so none are lost when several arrive between two polls.
        _read.setEventHandler(BLEUpdated, onNotify);

        if(!_read.canSubscribe()){
            return fail(peripheral, "unable to subscribe to READ");
        }else if(!_read.subscribe()){
            return fail(peripheral, "subscription failed");
        }else {
            Serial.println("subscribed!");
        }

        if(_type == OLD || _type == NEW){
            if(!writeCommand(IDENTIFY, sizeof(IDENTIFY), true, "identify")){
                return fail(peripheral, "identify write failed");
            }
            if(!writeCommand(NOTIFICATION_REQUEST, sizeof(NOTIFICATION_REQUEST), true, "notification request")){
                return fail(peripheral, "notification request write failed");
            }
        }else if(_type == FELICITA){
            // Wake the Felicita into a deterministic, known-good mode
            // (weight + timer) instead of whatever IDENTIFY used to leave it in.
            if(!writeCommand(WEIGHT_TIMER_MODE_FELICITA, sizeof(WEIGHT_TIMER_MODE_FELICITA), true, "felicita mode")){
                return fail(peripheral, "felicita mode set failed");
            }
        }

        _peripheral = peripheral;
        _connected = true;
        _lastHeartBeat = 0;          // send the first heartbeat right away
        // Start the packet timeout now, so a scale that never streams weight
        // is detected instead of looking "connected" forever.
        _lastPacket = millis();
        Serial.println("scale ready");
        return true;
    }while(millis() - start < 100);

    // Scale not seen yet. Leave the scan running for the next call.
    return false;
}

void AcaiaArduinoBLE::disconnect(){
    bool wasConnected = _connected;
    _connected = false;
    _newWeight = false;
    _rxLen = 0;
    _tareState = TARE_IDLE;
    _timerState = TIMER_IDLE;
    if(_peripheral){
        _peripheral.disconnect();
    }
    _peripheral = BLEDevice();
    if(wasConnected){
        Serial.println("scale disconnected");
    }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void AcaiaArduinoBLE::waitForCommandGap(){
    // Keep servicing the BLE stack (and therefore incoming weight) while waiting.
    while(millis() - _lastCommand < COMMAND_GAP_MS){
        BLE.poll();
    }
}

bool AcaiaArduinoBLE::writeCommand(const uint8_t* data, int length, bool withResponse, const char* name){
    waitForCommandGap();
    bool ok = _write.writeValue(data, length, withResponse);
    _lastCommand = millis();
    if(ok){
        if(_debug){
            Serial.print(name);
            Serial.println(" write successful");
        }
        return true;
    }
    Serial.print(name);
    Serial.println(" write failed");
    // A failed write means the link is gone or broken. Tear it down fully so
    // the scale starts advertising again and the next init() can find it.
    if(_connected){
        disconnect();
    }
    return false;
}

// Tare is the one command whose loss is dangerous (the shot would be measured
// with the cup on the scale), so it is written with response and verified by
// watching for a ~0 g reading (see updateTare()).
bool AcaiaArduinoBLE::sendTare(){
    bool ok;
    switch(_type){
        case GENERIC:  ok = writeCommand(TARE_GENERIC,  sizeof(TARE_GENERIC),  true, "tare"); break;
        case FELICITA: ok = writeCommand(TARE_FELICITA, sizeof(TARE_FELICITA), true, "tare"); break;
        default:       ok = writeCommand(TARE_ACAIA,    sizeof(TARE_ACAIA),    true, "tare"); break;
    }
    if(ok){
        _tareState = TARE_PENDING;
        _tareSent = millis();
    }
    return ok;
}

bool AcaiaArduinoBLE::tare(){
    _tareRetries = 0;
    return sendTare();
}

void AcaiaArduinoBLE::updateTare(){
    if(_tareState != TARE_PENDING || millis() - _tareSent < TARE_VERIFY_MS){
        return;
    }
    if(_tareRetries < TARE_MAX_RETRIES){
        _tareRetries++;
        Serial.print("tare not confirmed, retrying (");
        Serial.print(_tareRetries);
        Serial.println(")");
        sendTare();
    }else{
        Serial.println("tare failed");
        _tareState = TARE_FAILED;
    }
}

tare_state AcaiaArduinoBLE::tareState(){
    return _tareState;
}

bool AcaiaArduinoBLE::sendStartTimer(){
    bool ok;
    switch(_type){
        case GENERIC:  ok = writeCommand(START_TIMER_GENERIC,  sizeof(START_TIMER_GENERIC),  false, "start timer"); break;
        case FELICITA: ok = writeCommand(START_TIMER_FELICITA, sizeof(START_TIMER_FELICITA), false, "start timer"); break;
        default:       ok = writeCommand(START_TIMER,          sizeof(START_TIMER),          false, "start timer"); break;
    }
    if(ok){
        _timerState = TIMER_PENDING;
        _timerStartSent = millis();
        _timerAtStart = NAN;
    }
    return ok;
}

bool AcaiaArduinoBLE::sendResetTimer(){
    switch(_type){
        case GENERIC:  return writeCommand(RESET_TIMER_GENERIC,  sizeof(RESET_TIMER_GENERIC),  false, "reset timer");
        case FELICITA: return writeCommand(RESET_TIMER_FELICITA, sizeof(RESET_TIMER_FELICITA), false, "reset timer");
        default:       return writeCommand(RESET_TIMER,          sizeof(RESET_TIMER),          false, "reset timer");
    }
}

bool AcaiaArduinoBLE::startTimer(){
    _timerRetries = 0;
    return sendStartTimer();
}

bool AcaiaArduinoBLE::stopTimer(){
    _timerState = TIMER_IDLE;
    switch(_type){
        case GENERIC:  return writeCommand(STOP_TIMER_GENERIC,  sizeof(STOP_TIMER_GENERIC),  false, "stop timer");
        case FELICITA: return writeCommand(STOP_TIMER_FELICITA, sizeof(STOP_TIMER_FELICITA), false, "stop timer");
        default:       return writeCommand(STOP_TIMER,          sizeof(STOP_TIMER),          false, "stop timer");
    }
}

bool AcaiaArduinoBLE::resetTimer(){
    return sendResetTimer();
}

// The timer only matters for what the scale shows (brew-by-weight uses the
// caller's own clock), but a lost start is visible and easy to fix: watch the
// scale's timer reports and re-send reset + start if the timer isn't running.
void AcaiaArduinoBLE::updateTimer(){
    if(_timerState != TIMER_PENDING || millis() - _timerStartSent < TIMER_VERIFY_MS){
        return;
    }
    if(_lastTimerReport == 0){
        // This scale (or firmware) never reports its timer: nothing to check.
        _timerState = TIMER_UNVERIFIED;
        return;
    }
    if(_timerRetries < TIMER_MAX_RETRIES){
        _timerRetries++;
        Serial.println("scale timer not running, re-sending reset + start");
        sendResetTimer();
        sendStartTimer();
    }else{
        Serial.println("scale timer failed to start");
        _timerState = TIMER_FAILED;
    }
}

// Timer report from the scale. Confirmed once it has advanced since the first
// report after the start command, and is no larger than the time since that
// command (so a stale or un-reset timer doesn't count).
void AcaiaArduinoBLE::setTimer(float seconds){
    unsigned long now = millis();
    _scaleTimer = seconds;
    _lastTimerReport = now;
    if(_timerState != TIMER_PENDING){
        return;
    }
    float sinceStart = (now - _timerStartSent) / 1000.0;
    if(seconds > sinceStart + 1.0){
        return; // not reset: still counting from an earlier shot
    }
    if(isnan(_timerAtStart)){
        _timerAtStart = seconds;
    }else if(seconds > _timerAtStart){
        _timerState = TIMER_CONFIRMED;
        if(_debug){
            Serial.println("timer confirmed");
        }
    }
}

timer_state AcaiaArduinoBLE::timerState(){
    return _timerState;
}

float AcaiaArduinoBLE::scaleTimer(){
    return _scaleTimer;
}

unsigned long AcaiaArduinoBLE::msSinceTimerReport(){
    return _lastTimerReport ? millis() - _lastTimerReport : 0;
}

int AcaiaArduinoBLE::batteryPercent(){
    return _battery;
}

bool AcaiaArduinoBLE::unitsOunces(){
    return _ounces;
}

bool AcaiaArduinoBLE::hasScaleFlowRate(){
    return !isnan(_scaleFlow);
}

float AcaiaArduinoBLE::scaleFlowRate(){
    return _scaleFlow;
}

// Single command on BooKoo scales; emulated on everything else.
bool AcaiaArduinoBLE::tareStartTimer(){
    if(_type != GENERIC){
        bool ok = tare();
        ok = resetTimer() && ok;
        ok = startTimer() && ok;
        return ok;
    }
    _tareRetries = 0;
    if(!writeCommand(TARE_START_TIMER_BOOKOO, sizeof(TARE_START_TIMER_BOOKOO), true, "tare and start timer")){
        return false;
    }
    _tareState = TARE_PENDING;
    _tareSent = millis();
    _timerRetries = TIMER_MAX_RETRIES; // a retry would re-tare too; verify only
    _timerState = TIMER_PENDING;
    _timerStartSent = _tareSent;
    _timerAtStart = NAN;
    return true;
}

bool AcaiaArduinoBLE::beep(){
    switch(_type){
        case GENERIC:  return writeCommand(BEEP_LEVEL_1_BOOKOO, sizeof(BEEP_LEVEL_1_BOOKOO), false, "beep");
        case FELICITA: return false; // no beep command known; don't send Acaia bytes to a Felicita
        // for acaia's, use the tare command to generate the beep
        default:       return writeCommand(TARE_ACAIA, sizeof(TARE_ACAIA), false, "beep");
    }
}

bool AcaiaArduinoBLE::heartbeat(){
    if(writeCommand(HEARTBEAT, sizeof(HEARTBEAT), false, "heartbeat")){
        _lastHeartBeat = millis();
        return true;
    }
    return false;
}

float AcaiaArduinoBLE::getWeight(){
    return _currentWeight;
}

bool AcaiaArduinoBLE::heartbeatRequired(){
    return _connected
        && (_type == OLD || _type == NEW)
        && (millis() - _lastHeartBeat) > HEARTBEAT_PERIOD_MS;
}

bool AcaiaArduinoBLE::isConnected(){
    return _connected;
}

unsigned long AcaiaArduinoBLE::packetPeriod(){
    return _packetPeriod;
}

unsigned long AcaiaArduinoBLE::msSinceLastPacket(){
    return _lastPacket ? millis() - _lastPacket : 0;
}

unsigned long AcaiaArduinoBLE::checksumErrors(){
    return _checksumErrors;
}

// ---------------------------------------------------------------------------
// Incoming data
// ---------------------------------------------------------------------------

bool AcaiaArduinoBLE::newWeightAvailable(){
    if(!_connected){
        return false;
    }

    // Service the BLE stack; notifications are parsed in onNotify().
    BLE.poll();

    if(!_peripheral.connected()){
        Serial.println("link lost");
        disconnect();
        return false;
    }

    // check how long its been since we last got a weight packet
    if(millis() - _lastPacket > MAX_PACKET_PERIOD_MS){
        Serial.println("timeout!");
        disconnect();
        return false;
    }

    updateTare();
    updateTimer();

    if(_newWeight){
        _newWeight = false;
        return true;
    }
    return false;
}

void AcaiaArduinoBLE::onNotify(BLEDevice device, BLECharacteristic characteristic){
    if(_instance){
        _instance->handleNotification(characteristic.value(), characteristic.valueLength());
    }
}

void AcaiaArduinoBLE::setWeight(float weight){
    unsigned long now = millis();
    _currentWeight = weight;
    _newWeight = true;
    if(_lastPacket){
        _packetPeriod = now - _lastPacket;
    }
    _lastPacket = now;

    if(_tareState == TARE_PENDING && fabs(weight) < TARE_TOLERANCE_G){
        _tareState = TARE_CONFIRMED;
        if(_debug){
            Serial.println("tare confirmed");
        }
    }
}

void AcaiaArduinoBLE::handleNotification(const uint8_t* data, int length){
    if(!data || length <= 0){
        return;
    }

    if(_debug){
        Serial.print(length);
        Serial.print(": 0x");
        printData(data, length);
        Serial.println();
    }

    switch(_type){
        case OLD:
        case NEW: {
            // Acaia data is a byte stream: a message may be split across
            // notifications (older Lunars send "ef dd 0c" on its own) or
            // several messages may share one notification.
            //
            // Some firmware sends the body of a weight message without its
            // header. If no partial message is pending and the notification
            // starts like a weight event body, supply the header.
            if(_rxLen == 0 && length >= 2 && data[0] == 0x08 && data[1] == ACAIA_EVENT_WEIGHT){
                _rx[0] = ACAIA_HEADER1;
                _rx[1] = ACAIA_HEADER2;
                _rx[2] = ACAIA_CMD_EVENT;
                _rxLen = 3;
            }
            if(_rxLen + length > ACAIA_RX_BUFFER_SIZE){
                // Should never happen with valid data; resynchronise.
                _rxLen = 0;
                if(length > ACAIA_RX_BUFFER_SIZE){
                    data += length - ACAIA_RX_BUFFER_SIZE;
                    length = ACAIA_RX_BUFFER_SIZE;
                }
            }
            memcpy(&_rx[_rxLen], data, length);
            _rxLen += length;
            parseAcaiaStream();
            break;
        }

        case GENERIC:
            if(length == 20){
                // weight bytes 7-9 (big endian, 0.01 g), sign byte 6 ('-' = negative)
                float w = (((uint32_t)data[7] << 16) | ((uint32_t)data[8] << 8) | data[9]) / 100.0;
                if(data[6] == 45){
                    w = -w;
                }
                // BooKoo layout: [2..4] timer ms, [10] flow sign, [11..12] flow
                // (0.01 g/s), [13] battery %
                float flow = (((uint16_t)data[11] << 8) | data[12]) / 100.0;
                _scaleFlow = (data[10] == 45) ? -flow : flow;
                if(data[13] <= 100){
                    _battery = data[13];
                }
                setTimer((((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 8) | data[4]) / 1000.0);
                setWeight(w);
            }
            break;

        case FELICITA:
            if(length == 18){
                // Felicita Arc 18-byte ASCII packet (matches pyfelicita lib):
                //   byte 2     = sign ('-'/0x2D = negative)
                //   bytes 3..8 = six ASCII weight digits, value / 100 = grams
                long v = 0;
                for(int i = 3; i <= 8; i++){
                    if(data[i] < '0' || data[i] > '9'){
                        return; // not a weight packet
                    }
                    v = v * 10 + (data[i] - '0');
                }
                setWeight((data[2] == 0x2D ? -1 : 1) * v / 100.0);
            }
            break;
    }
}

void AcaiaArduinoBLE::parseAcaiaStream(){
    while(true){
        // find header
        int start = -1;
        for(int i = 0; i + 1 < _rxLen; i++){
            if(_rx[i] == ACAIA_HEADER1 && _rx[i+1] == ACAIA_HEADER2){
                start = i;
                break;
            }
        }
        if(start < 0){
            // Keep a trailing 0xef: it may be the first half of a header.
            if(_rxLen > 0 && _rx[_rxLen-1] == ACAIA_HEADER1){
                _rx[0] = ACAIA_HEADER1;
                _rxLen = 1;
            }else{
                _rxLen = 0;
            }
            return;
        }
        if(start > 0){
            memmove(_rx, &_rx[start], _rxLen - start);
            _rxLen -= start;
        }

        if(_rxLen < 4){
            return; // need cmd and len bytes
        }

        int total = _rx[3] + 5;
        if(total < 6 || total > ACAIA_RX_BUFFER_SIZE){
            // impossible length: not a real header, skip it
            memmove(_rx, &_rx[2], _rxLen - 2);
            _rxLen -= 2;
            continue;
        }
        if(_rxLen < total){
            return; // wait for the rest of the message
        }

        if(handleAcaiaMessage(_rx, total)){
            memmove(_rx, &_rx[total], _rxLen - total);
            _rxLen -= total;
        }else{
            // Bad message: drop only the header and resynchronise on the
            // next one, so a valid message following it is not lost.
            memmove(_rx, &_rx[2], _rxLen - 2);
            _rxLen -= 2;
        }
    }
}

// Returns false if the message is corrupt (bad checksum).
bool AcaiaArduinoBLE::handleAcaiaMessage(const uint8_t* msg, int length){
#if ACAIA_VERIFY_CHECKSUM
    uint8_t ck1 = 0, ck2 = 0;
    for(int i = 3; i < length - 2; i++){
        if((i - 3) % 2 == 0){
            ck1 += msg[i];
        }else{
            ck2 += msg[i];
        }
    }
    if(ck1 != msg[length-2] || ck2 != msg[length-1]){
        _checksumErrors++;
        if(_debug){
            Serial.print("checksum error: ");
            printData(msg, length);
            Serial.println();
        }
        return false;
    }
#endif

    // Settings (sent by the scale on its own): [4] battery % (bit 7 = flag),
    // [5] units (2 = grams, 5 = ounces). Layout from pyacaia.
    if(msg[2] == ACAIA_CMD_SETTINGS){
        if(length >= 8){
            int battery = msg[4] & 0x7f;
            if(battery <= 100){
                _battery = battery;
            }
            _ounces = (msg[5] == 5);
        }
        return true;
    }

    if(msg[2] != ACAIA_CMD_EVENT || length < 6){
        return true; // valid, but nothing we use
    }

    uint8_t event = msg[4];
    const uint8_t* p = &msg[5];
    int payloadLen = length - 7;          // between the event type and the checksum
    if(event == ACAIA_EVENT_ACK && payloadLen >= 3){
        // [2] says what follows: 5 = weight, 7 = timer
        event = p[2];
        p += 3;
        payloadLen -= 3;
    }

    if(event == ACAIA_EVENT_WEIGHT && payloadLen >= 6){
        // [0..1] weight (little endian), [4] decimal places, [5] bit 1 = negative
        uint8_t unit = p[4];
        if(unit < 1 || unit > 4){
            if(_debug){
                Serial.print("unexpected weight unit byte ");
                Serial.println(unit);
            }
            return true;
        }
        static const float SCALE[5] = {1, 10, 100, 1000, 10000};
        float w = (((uint16_t)p[1] << 8) | p[0]) / SCALE[unit];
        if(p[5] & 0x02){
            w = -w;
        }
        setWeight(w);
    }else if(event == ACAIA_EVENT_TIMER && payloadLen >= 3){
        // [0] minutes, [1] seconds, [2] tenths
        setTimer(p[0] * 60 + p[1] + p[2] / 10.0);
    }
    return true;
}

bool AcaiaArduinoBLE::isScaleName(String name){
    String nameShort = name.substring(0,5);

    return nameShort == "CINCO"
        || nameShort == "ACAIA"
        || nameShort == "PYXIS"
        || nameShort == "LUNAR"
        || nameShort == "PEARL"
        || nameShort == "PROCH"
        || nameShort == "BOOKO"
        || nameShort == "FELIC";
}

void AcaiaArduinoBLE::exploreService(BLEService service) {
  // print the UUID of the service
  Serial.print("Service ");
  Serial.println(service.uuid());

  // loop the characteristics of the service and explore each
  for (int i = 0; i < service.characteristicCount(); i++) {
    BLECharacteristic characteristic = service.characteristic(i);

    exploreCharacteristic(characteristic);
  }
}

void AcaiaArduinoBLE::exploreCharacteristic(BLECharacteristic characteristic) {
  // print the UUID and properties of the characteristic
  Serial.print("\tCharacteristic ");
  Serial.print(characteristic.uuid());
  Serial.print(", properties 0x");
  Serial.print(characteristic.properties(), HEX);

  // check if the characteristic is readable
  if (characteristic.canRead()) {
    // read the characteristic value
    characteristic.read();

    if (characteristic.valueLength() > 0) {
      // print out the value of the characteristic
      Serial.print(", value 0x");
      printData(characteristic.value(), characteristic.valueLength());
    }
  }
  Serial.println();

  // loop the descriptors of the characteristic and explore each
  for (int i = 0; i < characteristic.descriptorCount(); i++) {
    BLEDescriptor descriptor = characteristic.descriptor(i);

    exploreDescriptor(descriptor);
  }
}

void AcaiaArduinoBLE::exploreDescriptor(BLEDescriptor descriptor) {
  // print the UUID of the descriptor
  Serial.print("\t\tDescriptor ");
  Serial.print(descriptor.uuid());

  // read the descriptor value
  descriptor.read();

  // print out the value of the descriptor
  Serial.print(", value 0x");
  printData(descriptor.value(), descriptor.valueLength());

  Serial.println();
}

void AcaiaArduinoBLE::printData(const unsigned char data[], int length) {
  for (int i = 0; i < length; i++) {
    unsigned char b = data[i];

    if (b < 16) {
      Serial.print("0");
    }

    Serial.print(b, HEX);
  }
}
