#pragma once
#include <cstdint>
uint8_t eeprom_read(int a); void eeprom_write(int a, uint8_t v); void eeprom_commit();
struct EEPROMStub { void begin(int) {} uint8_t read(int a) { return eeprom_read(a); } void write(int a, uint8_t v) { eeprom_write(a, v); } void commit() { eeprom_commit(); } };
static EEPROMStub EEPROM;
