// Host stub of ArduinoBLE: one simulated Acaia ("NEW" type) scale, driven by the world model in harness.cpp.
#pragma once
#include "Arduino.h"
enum { BLERead = 0x02, BLEWriteWithoutResponse = 0x04, BLEWrite = 0x08, BLENotify = 0x10 };
enum BLECharacteristicEvent { BLESubscribed, BLEUnsubscribed, BLERead_, BLEWritten, BLEUpdated };
class BLEDevice; class BLECharacteristic;
typedef void (*BLECharacteristicEventHandler)(BLEDevice, BLECharacteristic);
struct BLEDescriptor { const char* uuid() { return ""; } void read() {} const uint8_t* value() { return nullptr; } int valueLength() { return 0; } };
class BLECharacteristic {
public:
  int kind = 0; // 0 invalid, 1 scale notify char, 2 scale write char, 3 local
  BLECharacteristic() {}
  explicit BLECharacteristic(int k) : kind(k) {}
  operator bool() const { return kind != 0; }
  bool canSubscribe() { return kind == 1; }
  bool canRead() { return false; }
  void read() {}
  int properties() { return kind == 2 ? (BLEWrite | BLEWriteWithoutResponse) : BLENotify; }
  const char* uuid() { return ""; }
  int descriptorCount() { return 0; }
  BLEDescriptor descriptor(int) { return BLEDescriptor(); }
  void setEventHandler(int ev, BLECharacteristicEventHandler h);
  bool subscribe();
  int writeValue(const uint8_t* data, int len, bool withResponse);
  const uint8_t* value(); int valueLength();
};
struct BLEService {
  BLEService() {} BLEService(const char*) {}
  const char* uuid() { return ""; }
  int characteristicCount() { return 0; }
  BLECharacteristic characteristic(int) { return BLECharacteristic(); }
  void addCharacteristic(BLECharacteristic&) {}
};
class BLEDevice {
public:
  int id = 0;
  BLEDevice() {} explicit BLEDevice(int i) : id(i) {}
  operator bool() const { return id != 0; }
  String address() { return "00:00"; } String localName(); String advertisedServiceUuid() { return ""; }
  String deviceName() { return ""; } int appearance() { return 0; }
  int serviceCount() { return 0; } BLEService service(int) { return BLEService(); }
  bool connect(); bool discoverAttributes(); bool connected(); bool disconnect(); int rssi() { return -60; }
  BLECharacteristic characteristic(const char* uuid);
};
struct BLEByteCharacteristic : BLECharacteristic {
  uint8_t v = 0; bool w = false;
  BLEByteCharacteristic(const char*, int) : BLECharacteristic(3) {}
  void writeValue(uint8_t x) { v = x; } bool written() { bool r = w; w = false; return r; } uint8_t value() { return v; }
};
struct BLEFloatCharacteristic : BLECharacteristic {
  float v = 0;
  BLEFloatCharacteristic(const char*, int) : BLECharacteristic(3) {}
  void writeValue(float x) { v = x; }
};
struct BLELocalDeviceStub {
  int begin() { return 1; } void setTimeout(uint32_t) {} void setLocalName(const char*) {} void setAdvertisedService(BLEService&) {} void addService(BLEService&) {} void advertise() {}
  void poll(); int scan(); int scanForAddress(String) { return scan(); } void stopScan(); BLEDevice available();
};
extern BLELocalDeviceStub BLE;
