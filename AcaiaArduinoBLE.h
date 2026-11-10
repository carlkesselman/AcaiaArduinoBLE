/*
  AcaiaArduinoBLE.h - Library for connecting to
  an Acaia Scale using the ArduinoBLE library.
  Created by Tate Mazer, December 13, 2023.
  Released into the public domain.

  Pio Baettig: Adding Felicita Arc support
  A-TWJ: Fixing Felicita Arc bug (issue #15)

  Known Bugs:
    * Only supports Grams
*/
#ifndef AcaiaArduinoBLE_h
#define AcaiaArduinoBLE_h

#define LIBRARY_VERSION        "3.4.0"
#define WRITE_CHAR_OLD_VERSION "2a80"
#define READ_CHAR_OLD_VERSION  "2a80"
#define WRITE_CHAR_NEW_VERSION "49535343-8841-43f4-a8d4-ecbe34729bb3"
#define READ_CHAR_NEW_VERSION  "49535343-1e4d-4bd9-ba61-23c647249616"
#define WRITE_CHAR_GENERIC     "ff12"
#define READ_CHAR_GENERIC      "ff11"
#define WRITE_CHAR_FELICITA    "ffe1"   // Felicita Arc: single ffe1 char (service ffe0) for both
#define READ_CHAR_FELICITA     "ffe1"   //   notify (weight) and write (commands)
#define HEARTBEAT_PERIOD_MS     2750
#define MAX_PACKET_PERIOD_MS    5000

// Minimum spacing between commands written to the scale. Acaia scales are
// known to drop commands that arrive back-to-back.
#ifndef COMMAND_GAP_MS
#define COMMAND_GAP_MS          50
#endif

// Tare verification: after a tare, a weight within TARE_TOLERANCE_G of zero
// must be reported within TARE_VERIFY_MS, otherwise the tare is re-sent
// (up to TARE_MAX_RETRIES times) and then reported as failed.
#ifndef TARE_VERIFY_MS
#define TARE_VERIFY_MS          750
#endif
#ifndef TARE_TOLERANCE_G
#define TARE_TOLERANCE_G        0.5
#endif
#ifndef TARE_MAX_RETRIES
#define TARE_MAX_RETRIES        2
#endif

// While disconnected the scan keeps running between init() calls; it is
// restarted periodically so the controller's duplicate filter is cleared.
#ifndef SCAN_RESTART_MS
#define SCAN_RESTART_MS         3000
#endif

// Reject Acaia messages whose checksum does not match.
#ifndef ACAIA_VERIFY_CHECKSUM
#define ACAIA_VERIFY_CHECKSUM   1
#endif

#define ACAIA_RX_BUFFER_SIZE    64

#include "Arduino.h"
#include <ArduinoBLE.h>

enum scale_type{
    OLD,     // Lunar (pre-2021)
    NEW,     // Lunar (2021), Pyxis
    GENERIC, // BooKoo Themis, Decent, etc (ff11/ff12, binary protocol)
    FELICITA // Felicita Arc (ffe1, single-byte ASCII protocol)
};

enum tare_state{
    TARE_IDLE,      // no tare outstanding
    TARE_PENDING,   // tare sent, waiting for a ~0 g reading
    TARE_CONFIRMED, // scale reported ~0 g after the last tare
    TARE_FAILED     // no ~0 g reading after all retries
};

class AcaiaArduinoBLE{
    public:
        AcaiaArduinoBLE(bool debug);
        bool init(String = "");
        bool tare();
        bool startTimer();
        bool stopTimer();
        bool resetTimer();
        bool tareStartTimer();
        bool beep();
        bool heartbeat();
        float getWeight();
        bool heartbeatRequired();
        bool isConnected();
        bool newWeightAvailable();
        void disconnect();

        tare_state tareState();
        unsigned long packetPeriod();      // ms between the last two weight packets
        unsigned long msSinceLastPacket(); // ms since the last weight packet
        unsigned long checksumErrors();    // Acaia messages dropped for a bad checksum
    private:
        bool isScaleName(String);
        bool fail(BLEDevice& peripheral, const char* msg);
        bool writeCommand(const uint8_t* data, int length, bool withResponse, const char* name);
        void waitForCommandGap();
        bool sendTare();
        void updateTare();

        // notification handling
        static void onNotify(BLEDevice device, BLECharacteristic characteristic);
        void handleNotification(const uint8_t* data, int length);
        void parseAcaiaStream();
        bool handleAcaiaMessage(const uint8_t* msg, int length);
        void setWeight(float weight);

        //debug functions
        void exploreService(BLEService service);
        void exploreCharacteristic(BLECharacteristic characteristic);
        void exploreDescriptor(BLEDescriptor descriptor);
        void printData(const unsigned char data[], int length);

        static AcaiaArduinoBLE* _instance;

        float               _currentWeight;
        bool                _newWeight;
        BLEDevice           _peripheral;
        BLECharacteristic   _write;
        BLECharacteristic   _read;
        unsigned long       _lastHeartBeat;
        unsigned long       _lastCommand;
        bool                _connected;
        bool                _scanning;
        unsigned long       _scanStarted;
        scale_type          _type;
        bool                _debug;
        unsigned long       _packetPeriod;
        unsigned long       _lastPacket;

        uint8_t             _rx[ACAIA_RX_BUFFER_SIZE];
        int                 _rxLen;
        unsigned long       _checksumErrors;

        tare_state          _tareState;
        unsigned long       _tareSent;
        int                 _tareRetries;
};

#endif
