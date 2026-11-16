// Host stub of the Arduino API for the shotStopper harness.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
using std::isnan; using std::abs; using std::fabs;
typedef uint8_t byte;
#define HIGH 1
#define LOW 0
#define INPUT 1
#define OUTPUT 3
#define INPUT_PULLUP 5
#define HEX 16
class String : public std::string {
public:
  String() {}
  String(const char* s) : std::string(s) {}
  String(const std::string& s) : std::string(s) {}
  String substring(int a, int b) const { return String(substr(a, b - a)); }
};
uint32_t millis();
void delay(uint32_t ms);
void pinMode(int pin, int mode);
void digitalWrite(int pin, int v);
int digitalRead(int pin);
void analogWrite(int pin, int v);
inline void setCpuFrequencyMhz(int) {}
void serial_putc(char c);
struct SerialStub {
  void begin(int) {}
  void puts_(const char* s) { while (*s) serial_putc(*s++); }
  void print(const char* s) { puts_(s); }
  void print(const String& s) { puts_(s.c_str()); }
  void print(char c) { serial_putc(c); }
  void print(int v, int base = 10) { char b[32]; snprintf(b, 32, base == 16 ? "%X" : "%d", v); puts_(b); }
  void print(unsigned v, int base = 10) { char b[32]; snprintf(b, 32, base == 16 ? "%X" : "%u", v); puts_(b); }
  void print(long v) { char b[32]; snprintf(b, 32, "%ld", v); puts_(b); }
  void print(unsigned long v) { char b[32]; snprintf(b, 32, "%lu", v); puts_(b); }
  void print(unsigned char v, int base = 10) { print((unsigned)v, base); }
  void print(double v) { char b[32]; snprintf(b, 32, "%.2f", v); puts_(b); }
  void println() { serial_putc('\n'); }
  template <class T> void println(T v) { print(v); println(); }
  template <class T> void println(T v, int base) { print(v, base); println(); }
};
extern SerialStub Serial;
