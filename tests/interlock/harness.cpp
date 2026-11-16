// Host harness for shotStopper.ino (PADDLE_INTERLOCK build) + the real AcaiaArduinoBLE library.
// Virtual time; a world model (paddle, K1, machine, cup, Acaia scale); simulated resets re-exec the process.
#include "Arduino.h"
#include "ArduinoBLE.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <unistd.h>
#include <vector>
#include <cstdarg>

enum EvType { PADDLE, GLITCH, SCALE_POWER, STREAM, CUP_ON, NEWCUP, RESET, V5, HANG, MODE };
struct Ev { double t; EvType type; double a; double b; };
struct Scenario {
  const char* name; const char* desc; double end; std::vector<Ev> ev;
  bool scale_on0 = true; bool paddle0 = false; double connect_ms = 800, discover_ms = 1500;
  bool tare_ignored = false; uint32_t millis_offset = 0; bool pin_retain = false; uint32_t poweron_rtc = 0xDEADBEEF; double adv_ms = 300; bool swapped = false;
};
struct World {
  uint64_t t_us, boot_us; int next_ev;
  bool paddle, v5, stop_out_en, stop_level;
  bool glitch_active; uint64_t glitch_until; int glitch_level;
  bool scale_on, stream; uint64_t scale_off_at, scale_on_at;
  bool cup_on; double cup_g, cup_mass, zero; uint64_t tare_at;
  bool brewing; uint64_t brew_start; double brew_start_g, drip_left;
  bool k1_prev; uint64_t off_open_us, max_off_open_us;
  int reset_reason; uint32_t rtc; uint32_t rtc2[3]; uint8_t eeprom[3];
  bool wdt_on; uint64_t wdt_fed; int wdt_s;
  int hang_armed; int boots, viol3, pending_reset; bool fw_running;
  double total_brew_s;
};
static World w; static Scenario* sc; static const char* self_path; static bool verbose;
extern uint32_t savedInterlock, savedShot, savedPaddleOn_ms, savedPaddleOnCheck;
static void (*timer_cb)(void*); static uint64_t timer_period, timer_next; static bool in_timer;
int esp_timer_create(const esp_timer_create_args_t* a, esp_timer_handle_t* h) { timer_cb = a->callback; *h = (void*)1; return 0; }
int esp_timer_start_periodic(esp_timer_handle_t, uint64_t p) { timer_period = p; return 0; }
static void do_reset(int reason, const char* why);
static void fw_mode_write(int v);

static void logf(const char* fmt, ...) {
  va_list ap; va_start(ap, fmt); printf("[%8.3f] ", w.t_us / 1e6); vprintf(fmt, ap); printf("\n"); va_end(ap);
}
static double flow_at(double ts) { const double F = 2.4; return ts < 5 ? 0 : ts < 7 ? F * (ts - 5) / 2 : F; }
static bool k1_open() { return w.stop_out_en && w.stop_level; }
static void step(uint64_t dt) {
  w.t_us += dt;
  if (timer_period && !in_timer) { if (!timer_next) timer_next = w.t_us + timer_period; while (w.t_us >= timer_next) { timer_next += timer_period; in_timer = true; timer_cb(nullptr); in_timer = false; } }
  while (w.next_ev < (int)sc->ev.size() && sc->ev[w.next_ev].t * 1e6 <= w.t_us) {
    const Ev& e = sc->ev[w.next_ev++];
    switch (e.type) {
      case PADDLE: w.paddle = e.a; logf("PADDLE %s", w.paddle ? "ON" : "OFF"); break;
      case GLITCH: w.glitch_active = true; w.glitch_level = e.a; w.glitch_until = w.t_us + e.b * 1000;
                   logf("sense glitch: reads %s for %.0f ms", e.a ? "ON" : "OFF", e.b); break;
      case SCALE_POWER: w.scale_on = e.a; if (e.a) w.scale_on_at = w.t_us; else w.scale_off_at = w.t_us; logf("scale power %s", e.a ? "on" : "OFF"); break;
      case STREAM: w.stream = e.a; logf("scale weight stream %s", e.a ? "resumes" : "STALLS"); break;
      case CUP_ON: w.cup_on = e.a; logf("cup %s scale", e.a ? "on" : "REMOVED from"); break;
      case NEWCUP: logf("new empty cup (previous held %.1f g)", w.cup_g); w.cup_g = 0; break;
      case RESET: w.pending_reset = (int)e.a; break;
      case V5: w.v5 = e.a; logf("machine 5 V %s", e.a ? "on" : "OFF (USB keeps ESP32 alive)"); break;
      case MODE: logf("Bluetooth write: mode = %d (%s)", (int)e.a, e.a == 1 ? "manual" : "brew by weight"); fw_mode_write((int)e.a); break;
      case HANG: w.hang_armed = (int)e.a; logf("(fault armed: next %s write to the scale never returns)", e.a == 2 ? "stop-timer" : "any"); break;
    }
  }
  if (w.glitch_active && w.t_us >= w.glitch_until) w.glitch_active = false;
  bool open = k1_open();
  if (open != w.k1_prev) {
    w.k1_prev = open;
    if (open) logf("K1 OPEN  (STOP high)%s", (w.paddle && w.v5) ? "" : "  [paddle is off]");
    else {
      logf("K1 CLOSED (STOP low/hi-Z)%s", w.fw_running ? "" : "  [ESP32 in reset]");
      if (w.paddle && w.v5) { w.viol3++; logf("  ** REQ 3 VIOLATION: K1 re-closed while the paddle is still ON (%s)", w.fw_running ? "firmware" : "reset gap"); }
    }
  }
  if (open && !(w.paddle && w.v5)) { w.off_open_us += dt; if (w.off_open_us > w.max_off_open_us) w.max_off_open_us = w.off_open_us; }
  else w.off_open_us = 0;
  bool want = w.paddle && w.v5 && !open;
  if (want && !w.brewing) { w.brewing = true; w.brew_start = w.t_us; w.brew_start_g = w.cup_g; logf("MACHINE BREW START"); }
  else if (!want && w.brewing) {
    w.brewing = false; double d = (w.t_us - w.brew_start) / 1e6; w.total_brew_s += d;
    w.drip_left = flow_at(d) > 0 ? 1.2 : 0;
    logf("MACHINE BREW STOP after %.2f s, cup %.1f g (+%.1f g drip to come)", d, w.cup_g, w.drip_left);
  }
  if (w.brewing) w.cup_g += flow_at((w.t_us - w.brew_start) / 1e6) * dt / 1e6;
  else if (w.drip_left > 0) { double d = 1.5 * dt / 1e6; if (d > w.drip_left) d = w.drip_left; w.cup_g += d; w.drip_left -= d; }
  if (w.tare_at && w.t_us >= w.tare_at) { w.tare_at = 0; w.zero = w.cup_on ? w.cup_mass + w.cup_g : 0; }
  if (w.wdt_on && w.fw_running && w.t_us - w.wdt_fed > (uint64_t)w.wdt_s * 1000000ULL) do_reset(ESP_RST_TASK_WDT, "TASK WATCHDOG fired");
}
static void advance(uint64_t us) { while (us) { uint64_t d = us > 500 ? 500 : us; step(d); us -= d; } }

// ---- Arduino stubs ----
SerialStub Serial;
static std::string line;
void serial_putc(char c) {
  if (c != '\n') { line += c; return; }
  bool numeric = !line.empty() && (isdigit((unsigned char)line[0]) || line[0] == '-');
  if (verbose || !numeric || line.find("implausible") != std::string::npos) { if (!line.empty()) logf("    fw: %s", line.c_str()); }
  line.clear();
}
uint32_t millis() { return (uint32_t)((w.t_us - w.boot_us) / 1000 + sc->millis_offset); }
void delay(uint32_t ms) { advance((uint64_t)ms * 1000); }
void pinMode(int pin, int mode) { if (pin == 5) w.stop_out_en = (mode == OUTPUT); step(0); }
void digitalWrite(int pin, int v) { if (pin == 5) { w.stop_level = v; step(0); } }
int digitalRead(int pin) {
  if (pin != 8) return HIGH;
  bool on = w.paddle && w.v5; if (sc->swapped) on = on && !k1_open(); if (w.glitch_active) on = w.glitch_level;
  return on ? LOW : HIGH;
}
void analogWrite(int, int) {}
uint8_t eeprom_read(int a) { return w.eeprom[a]; }
void eeprom_write(int a, uint8_t v) { w.eeprom[a] = v; }
void eeprom_commit() { advance(30000); }
void wdt_start(int s) { w.wdt_on = true; w.wdt_s = s; w.wdt_fed = w.t_us; }
void wdt_feed() { w.wdt_fed = w.t_us; }
esp_reset_reason_t esp_reset_reason() { return (esp_reset_reason_t)w.reset_reason; }

// ---- BLE stub: one Acaia scale ----
BLELocalDeviceStub BLE;
static bool scanning, reported, link_, subscribed; static uint64_t scan_start, next_notify;
static BLECharacteristicEventHandler handler; static uint8_t nbuf[16]; static int nlen;
static bool link_alive() { if (link_ && !w.scale_on && w.t_us > w.scale_off_at + 2000000) link_ = false; return link_; } // 2 s supervision timeout
static double reading() { return (w.cup_on ? w.cup_mass + w.cup_g : 0) - w.zero; }
void BLELocalDeviceStub::poll() {
  advance(150);
  if (link_alive() && subscribed && w.scale_on && w.stream && w.t_us >= next_notify && handler) {
    next_notify = w.t_us + 100000;
    double r = reading(); int v = (int)lround(fabs(r) * 10); if (v > 65535) v = 65535;
    uint8_t m[13] = { 0xef, 0xdd, 0x0c, 0x08, 0x05, (uint8_t)(v & 0xff), (uint8_t)(v >> 8), 0, 0, 1, (uint8_t)(r < -0.05 ? 2 : 0), 0, 0 };
    for (int i = 3; i < 11; i++) { if ((i - 3) % 2 == 0) m[11] += m[i]; else m[12] += m[i]; }
    memcpy(nbuf, m, 13); nlen = 13; handler(BLEDevice(1), BLECharacteristic(1));
  }
}
int BLELocalDeviceStub::scan() { scanning = true; reported = false; scan_start = w.t_us; advance(2000); return 1; }
void BLELocalDeviceStub::stopScan() { scanning = false; advance(1000); }
BLEDevice BLELocalDeviceStub::available() {
  advance(100);
  if (scanning && !reported && w.scale_on && !link_alive() && w.t_us - scan_start > (uint64_t)(sc->adv_ms * 1000) && w.t_us - w.scale_on_at > 1000000) { reported = true; return BLEDevice(1); }
  return BLEDevice();
}
String BLEDevice::localName() { return "LUNAR-SIM"; }
bool BLEDevice::connect() { if (!w.scale_on) { advance(5000000); return false; } advance((uint64_t)(sc->connect_ms * 1000)); link_ = w.scale_on; return link_; }
bool BLEDevice::discoverAttributes() { advance((uint64_t)(sc->discover_ms * 1000)); return link_alive(); }
bool BLEDevice::connected() { return link_alive(); }
bool BLEDevice::disconnect() { link_ = false; subscribed = false; advance(5000); return true; }
BLECharacteristic BLEDevice::characteristic(const char* u) {
  if (!strcmp(u, "49535343-1e4d-4bd9-ba61-23c647249616")) return BLECharacteristic(1);
  if (!strcmp(u, "49535343-8841-43f4-a8d4-ecbe34729bb3")) return BLECharacteristic(2);
  return BLECharacteristic();
}
void BLECharacteristic::setEventHandler(int, BLECharacteristicEventHandler h) { handler = h; }
bool BLECharacteristic::subscribe() { advance(30000); if (!link_alive()) return false; subscribed = true; next_notify = w.t_us + 100000; return true; }
const uint8_t* BLECharacteristic::value() { return nbuf; }
int BLECharacteristic::valueLength() { return nlen; }
int BLECharacteristic::writeValue(const uint8_t* d, int len, bool withResponse) {
  bool isStop = len >= 5 && d[0] == 0xef && d[2] == 0x0d && d[4] == 0x02;
  if (w.hang_armed == 1 || (w.hang_armed == 2 && isStop)) {
    w.hang_armed = 0; logf("  (BLE write blocks forever, as ArduinoBLE sendAclPkt can)");
    for (;;) advance(1000);
  }
  if (!link_alive()) return 0;
  if (withResponse) { if (!w.scale_on) { while (link_alive()) advance(1000); return 0; } advance(15000); } else advance(200);
  if (len >= 3 && d[0] == 0xef && d[2] == 0x04 && !sc->tare_ignored) w.tare_at = w.t_us + 150000;
  return 1;
}

// ---- firmware under test ----
#include "protos.h"
#include "fw/shotStopper.ino.h"   // generated by run.sh from the real sketch

static void fw_mode_write(int v) { modeCharacteristic.v = v; modeCharacteristic.w = true; }
static void report() {   // what a Bluetooth client would be notified of
  static int st = -1; static float sw = 0; static bool first = true;
  if (statusCharacteristic.v != st) {
    st = statusCharacteristic.v;
    logf("  status 0x%02x:%s%s%s%s%s%s%s", st, st & 1 ? " paddle-on" : "", st & 2 ? " HELD" : "", st & 4 ? " shot" : "", st & 8 ? " weighing" : "",
         st & 16 ? " scale" : "", st & 32 ? " FAULT" : "", st & 64 ? " manual" : "");
  }
  float x = shotWeightCharacteristic.v;
  if (first || !(x == sw || (x != x && sw != sw))) { first = false; sw = x; if (x == x) logf("  final shot weight %.1f g, shot time %.1f s", x, shotTimeCharacteristic.v); }
}
static void do_reset(int reason, const char* why) {
  logf("===== RESET: %s =====", why);
  w.rtc = (reason == ESP_RST_POWERON) ? sc->poweron_rtc : savedInterlock;
  w.rtc2[0] = savedShot; w.rtc2[1] = savedPaddleOn_ms; w.rtc2[2] = savedPaddleOnCheck;
  w.reset_reason = reason; w.wdt_on = false; w.fw_running = false; w.pending_reset = 0; w.boots++;
  if (reason == ESP_RST_POWERON || !sc->pin_retain) { w.stop_out_en = false; w.stop_level = false; }
  char path[256]; snprintf(path, sizeof path, "%s.state.%s", self_path, sc->name);
  FILE* f = fopen(path, "wb"); fwrite(&w, sizeof w, 1, f); fclose(f); fflush(stdout);
  execl(self_path, self_path, sc->name, "--resume", path, verbose ? "-v" : (char*)0, (char*)0);
  perror("exec"); exit(2);
}

#define D30 (30u * 86400u * 1000u)
static std::vector<Scenario> scenarios() {
  std::vector<Scenario> v; Scenario s;
  s = Scenario{"normal", "normal shot, paddle off, early paddle-off on 2nd shot, 3rd shot", 150,
    {{10, PADDLE, 1}, {60, PADDLE, 0}, {62, NEWCUP}, {70, PADDLE, 1}, {80, PADDLE, 0}, {85, NEWCUP}, {90, PADDLE, 1}, {140, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"hold_long", "stop, paddle left on 6.5 min (scale switches off meanwhile), then off, on", 440,
    {{10, PADDLE, 1}, {120, SCALE_POWER, 0}, {400, PADDLE, 0}, {402, NEWCUP}, {410, PADDLE, 1}, {420, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"scale_lost", "scale dies 14 s into a weighed shot", 140,
    {{10, PADDLE, 1}, {24, SCALE_POWER, 0}, {60, PADDLE, 0}, {62, NEWCUP}, {65, SCALE_POWER, 1}, {80, PADDLE, 1}, {130, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"glitch_idle_on", "5 ms false ON on the sense line while idle, scale connected", 30, {{10, GLITCH, 1, 5}}}; v.push_back(s);
  s = Scenario{"glitch_hold_off", "false OFF glitches (20 ms, then 60 ms) while a stop is held", 120,
    {{10, PADDLE, 1}, {35, GLITCH, 0, 20}, {40, GLITCH, 0, 60}, {110, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"glitch_shot_off", "60 ms false OFF during a weighed shot", 80, {{10, PADDLE, 1}, {20, GLITCH, 0, 60}, {70, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"reset_hold", "warm reset while a stop is held (GPIO5 hi-Z during reset)", 100,
    {{10, PADDLE, 1}, {40, RESET, ESP_RST_PANIC}, {70, PADDLE, 0}, {72, NEWCUP}, {80, PADDLE, 1}, {90, PADDLE, 0}}}; v.push_back(s);
  s.name = "reset_hold_retain"; s.desc = "same, GPIO5 level retained through a CPU-only reset"; s.pin_retain = true; v.push_back(s);
  s = Scenario{"reset_midshot", "warm reset 10 s into a weighed shot, paddle stays on", 200, {{10, PADDLE, 1}, {20, RESET, ESP_RST_PANIC}, {190, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"poweron_paddle_on", "power-on with paddle on, RTC variable happens to hold the magic, no scale", 150,
    {{120, PADDLE, 0}, {125, PADDLE, 1}, {135, PADDLE, 0}}}; s.paddle0 = true; s.scale_on0 = false; s.poweron_rtc = 0x5AFE5709; v.push_back(s);
  s = Scenario{"connect_block", "paddle goes on while init() is blocked connecting to the scale", 90,
    {{10, SCALE_POWER, 1}, {12.5, PADDLE, 1}, {80, PADDLE, 0}}}; s.scale_on0 = false; s.connect_ms = 2500; s.discover_ms = 6000; v.push_back(s);
  s = Scenario{"stream_gap", "2.5 s gap in weight packets early in the pour (link stays up)", 90,
    {{10, PADDLE, 1}, {17.5, STREAM, 0}, {20.0, STREAM, 1}, {80, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"noscale_cap", "no scale: 90 s cap, then off/on", 170, {{10, PADDLE, 1}, {150, PADDLE, 0}, {155, PADDLE, 1}, {165, PADDLE, 0}}}; s.scale_on0 = false; v.push_back(s);
  s = Scenario{"uptime_30d", "normal shot after 30 days of uptime (float seconds)", 70, {{10, PADDLE, 1}, {60, PADDLE, 0}}}; s.millis_offset = D30; v.push_back(s);
  s = Scenario{"uptime_45d", "normal shot after 45 days of uptime", 70, {{10, PADDLE, 1}, {60, PADDLE, 0}}}; s.millis_offset = D30 / 2 * 3; v.push_back(s);
  s = Scenario{"rollover", "millis() wraps 15 s into a weighed shot", 150, {{10, PADDLE, 1}, {140, PADDLE, 0}}}; s.millis_offset = 0u - 25400u; v.push_back(s);
  s = Scenario{"v5_lost_hold", "machine 5 V (sense supply) lost during a held stop, USB keeps ESP32 up", 60, {{10, PADDLE, 1}, {40, V5, 0}}}; v.push_back(s);
  s = Scenario{"hang_stop", "BLE write hangs inside setBrewingState(false) at the weight stop", 220, {{10, PADDLE, 1}, {15, HANG, 2}, {200, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"hang_hold", "BLE write hangs during a held stop; paddle turned off 2 s later", 80, {{10, PADDLE, 1}, {35, HANG, 1}, {40, PADDLE, 0}, {45, PADDLE, 1}, {70, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"cup_removed", "cup lifted off the scale mid-shot", 90, {{10, PADDLE, 1}, {20, CUP_ON, 0}, {80, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"reset_loop", "no scale, paddle on, board resets every 20 s", 320, {{10, PADDLE, 1}, {300, PADDLE, 0}}}; s.scale_on0 = false;
    for (int t = 30; t < 300; t += 20) s.ev.insert(s.ev.end() - 1, Ev{(double)t, RESET, ESP_RST_PANIC}); v.push_back(s);
  s = Scenario{"tare_ignored", "scale never acts on the tare (150 g cup)", 90, {{10, PADDLE, 1}, {80, PADDLE, 0}}}; s.tare_ignored = true; v.push_back(s);
  s = Scenario{"flick", "after a stop: 30 ms off-on flick (too short), then a 200 ms one", 80,
    {{10, PADDLE, 1}, {40, PADDLE, 0}, {40.03, PADDLE, 1}, {50, PADDLE, 0}, {50, NEWCUP}, {50.2, PADDLE, 1}, {60, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"stall_reconnect", "weight stream stalls mid-shot (scale still advertising after the 5 s timeout drop); reconnect takes 5.5 s", 90,
    {{10, PADDLE, 1}, {20, STREAM, 0}, {80, PADDLE, 0}}}; s.adv_ms = 20; s.connect_ms = 2500; s.discover_ms = 3000; v.push_back(s);
  s = Scenario{"hang_tare", "BLE write hangs at the tare when the paddle goes on", 220, {{9.9, HANG, 1}, {10, PADDLE, 1}, {200, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"swapped", "J3 and J4 plugs swapped: two shots", 140, {{10, PADDLE, 1}, {60, PADDLE, 0}, {62, NEWCUP}, {70, PADDLE, 1}, {130, PADDLE, 0}}}; s.swapped = true; v.push_back(s);
  s = Scenario{"quick_off", "user turns the paddle off 0.5 s after the automatic stop, on again 3 s later", 100, {{10, PADDLE, 1}, {30.9, PADDLE, 0}, {32, NEWCUP}, {34, PADDLE, 1}, {90, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"manual", "manual mode set while idle: shot runs past the target until paddle off; back to weight for the next", 200,
    {{5, MODE, 1}, {10, PADDLE, 1}, {60, PADDLE, 0}, {62, NEWCUP}, {65, MODE, 0}, {70, PADDLE, 1}, {120, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"manual_midshot", "manual requested mid-shot (shot still stops by weight), and during the held stop (stays held)", 120,
    {{10, PADDLE, 1}, {15, MODE, 1}, {40, MODE, 0}, {45, MODE, 1}, {60, PADDLE, 0}, {62, NEWCUP}, {70, PADDLE, 1}, {110, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"manual_off_midshot", "brew-by-weight requested during a manual shot: this shot is not stopped", 120,
    {{5, MODE, 1}, {10, PADDLE, 1}, {20, MODE, 0}, {110, PADDLE, 0}}}; v.push_back(s);
  s = Scenario{"manual_reset", "manual mode survives a power cycle", 120,
    {{5, MODE, 1}, {8, RESET, ESP_RST_POWERON}, {20, PADDLE, 1}, {115, PADDLE, 0}}}; v.push_back(s);
  return v;
}

int main(int argc, char** argv) {
  self_path = argv[0];
  static std::vector<Scenario> all = scenarios();
  if (argc < 2) { for (auto& s : all) printf("%s\n", s.name); return 0; }
  for (auto& s : all) if (!strcmp(s.name, argv[1])) sc = &s;
  if (!sc) { fprintf(stderr, "unknown scenario\n"); return 1; }
  bool resume = argc > 3 && !strcmp(argv[2], "--resume");
  verbose = !strcmp(argv[argc - 1], "-v");
  if (resume) { FILE* f = fopen(argv[3], "rb"); fread(&w, sizeof w, 1, f); fclose(f); }
  else {
    memset(&w, 0, sizeof w);
    printf("\n=== %s: %s ===\n", sc->name, sc->desc);
    w.v5 = true; w.paddle = sc->paddle0; w.scale_on = sc->scale_on0; w.stream = true; w.cup_on = true; w.cup_mass = 150;
    w.eeprom[0] = w.eeprom[1] = w.eeprom[2] = 0xFF; w.reset_reason = ESP_RST_POWERON; w.rtc = sc->poweron_rtc;
    if (w.paddle) logf("PADDLE ON (at power-on)");
  }
  advance(400000);                 // boot: ROM + bootloader + app start, firmware not running, GPIO5 undriven
  w.boot_us = w.t_us; savedInterlock = w.rtc; savedShot = w.rtc2[0]; savedPaddleOn_ms = w.rtc2[1]; savedPaddleOnCheck = w.rtc2[2];
  setup(); w.fw_running = true;
  logf("boot %d done: reset reason %s, interlockOpen=%d", w.boots, w.reset_reason == ESP_RST_POWERON ? "POWERON" : w.reset_reason == ESP_RST_TASK_WDT ? "TASK_WDT" : "warm", (int)interlockOpen);
  while (w.t_us < sc->end * 1e6) {
    if (w.pending_reset) do_reset(w.pending_reset, w.pending_reset == ESP_RST_POWERON ? "power cycle" : "warm reset (panic/crash)");
    loop(); report(); advance(200);
  }
  logf("END: cup %.1f g, total machine brew time %.1f s, K1 %s, interlockOpen=%d shot.brewing=%d buttonPressed=%d weightOffset=%.1f",
       w.cup_g, w.total_brew_s + (w.brewing ? (w.t_us - w.brew_start) / 1e6 : 0), k1_open() ? "OPEN" : "closed", (int)interlockOpen, (int)shot.brewing, (int)buttonPressed, weightOffset);
  printf("   summary: resets=%d  req-3 violations=%d  longest K1-open-with-paddle-off=%.0f ms\n", w.boots, w.viol3, w.max_off_open_us / 1000.0);
  return 0;
}
