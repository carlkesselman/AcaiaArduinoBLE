// Event log for the shotStopper sketch: a ring of fixed 16-byte records in a
// flash partition, written oldest-over-newest one 4 KB sector at a time.
//
// Nothing here touches hardware. The sketch supplies read/write/erase for the
// partition (LogStore), and tests/eventlog runs the same code against a
// simulated flash chip.
#ifndef SHOTSTOPPER_EVENTLOG_H
#define SHOTSTOPPER_EVENTLOG_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOG_SECTOR_BYTES 4096u
#define LOG_REC_BYTES    16u
#define LOG_PER_SECTOR   (LOG_SECTOR_BYTES / LOG_REC_BYTES)

struct LogRec {
  uint32_t seq;   // counts up for ever; 0xFFFFFFFF is erased flash
  uint32_t t_ms;  // millis() when the event happened
  uint8_t  code;  // LOG_* below
  uint8_t  a;     // small argument
  uint16_t crc;   // over the other 14 bytes: a half-written record is ignored
  int32_t  c;     // large argument
};

enum : uint8_t {
  LOG_BOOT = 1,        // a = reset reason, c = boot number
  LOG_FIRMWARE,        // c = build id
  LOG_CLOCK,           // c = Unix time, from the network
  LOG_SCALE_READY,     // a = -RSSI (0 unknown), c = ms the connect + set-up took
  LOG_SCALE_CONNECT_FAIL, // a = failures in a row, c = ms the attempt took
  LOG_SCALE_SETUP_FAIL,   // a = step, as in AcaiaArduinoBLE.h
  LOG_SCALE_LINK_LOST, // a = 1 during a shot, c = ms since the last weight packet
  LOG_SCALE_TIMEOUT,   // a = 1 during a shot, c = ms since the last weight packet
  LOG_SCALE_WRITE_FAIL,// a = 1 during a shot
  LOG_SCALE_SESSION,   // connection ended: a = checksum errors (255 = more), c = seconds connected
  LOG_TARE_RETRY,      // a = retry number
  LOG_TARE_FAILED,
  LOG_TIMER_RETRY,     // a = retry number
  LOG_TIMER_FAILED,
  LOG_PADDLE,          // a = 0 off, 1 on, 2 on but noticed late, 3 on right after an off
  LOG_SHOT_START,      // a = flags (LOG_SS_*), c = goal weight x10 | offset x10 << 16
  LOG_SHOT_END,        // a = reason (0 time, 1 weight, 2 paddle, 3 scale lost, 4 undefined), c = ms
  LOG_SHOT_LINK,       // a = gaps over LOG_GAP_MS, c = weight packets | longest gap ms << 16
  LOG_SHOT_QUALITY,    // a = flags (LOG_SQ_*), c = checksum errors | peak flow x100 << 16
  LOG_SHOT_WEIGHT,     // a = 1 valid, c = final weight x100
  LOG_OFFSET,          // a = 1 learned, 0 rejected, c = offset x10
  LOG_GAP,             // weight stream gap in a shot: a = shot second, c = ms
  LOG_INTERLOCK,       // a = 1 brew stopped, 0 released by paddle off
  LOG_RESUME_HOLD,     // reset during a stop or a weighed shot
  LOG_FAULT,           // plugs look swapped
  LOG_MAX_BREW,        // paddle on too long
  LOG_READINGS_LOST,   // weight readings not believable (cup lifted?)
  LOG_MODE,            // a = 1 manual, 0 brew by weight
  LOG_GOAL,            // c = goal weight
  LOG_BATTERY,         // a = scale battery %
  LOG_OUNCES,          // scale is set to ounces
  LOG_WIFI,            // a = 0 off, 1 on, 2 connected (c = RSSI)
  LOG_OTA,             // firmware and recovery: a = LOG_OTA_*, c = detail
  LOG_LOOP_STALL,      // a = 1 during a shot, c = ms one pass of loop() took
  LOG_DROPPED,         // c = events lost because the queue was full
  LOG_CODE_COUNT
};

#define LOG_GAP_MS 700
// LOG_SHOT_START flags
#define LOG_SS_WEIGHED  0x01
#define LOG_SS_MANUAL   0x02
#define LOG_SS_STALE    0x04  // no recent weight from the scale
#define LOG_SS_OUNCES   0x08
// LOG_SHOT_QUALITY flags
#define LOG_SQ_TARED     0x01
#define LOG_SQ_DISTURBED 0x02
#define LOG_SQ_LEARN     0x04
// LOG_OTA
enum : uint8_t { LOG_OTA_REQUESTED = 1, LOG_OTA_CLOSED, LOG_OTA_PROBATION, LOG_OTA_CONFIRMED,
                 LOG_OTA_TO_RECOVERY };   // c = why: 1 Bluetooth, 2 web page, 3 crashes, 4 power switch

struct LogStore {
  uint32_t size;  // bytes: a whole number of sectors
  bool (*read)(uint32_t offset, void* buf, uint32_t len);
  bool (*write)(uint32_t offset, const void* buf, uint32_t len);
  bool (*erase)(uint32_t offset, uint32_t len);
};

static inline uint16_t logCrc(const LogRec& r){
  // CRC-16/CCITT over everything except the crc field
  uint8_t b[14];
  memcpy(b, &r.seq, 4); memcpy(b + 4, &r.t_ms, 4);
  b[8] = r.code; b[9] = r.a;
  memcpy(b + 10, &r.c, 4);
  uint16_t crc = 0xFFFF;
  for(unsigned i = 0; i < sizeof(b); i++){
    crc ^= (uint16_t)b[i] << 8;
    for(int k = 0; k < 8; k++){
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
  }
  return crc;
}

static inline bool logValid(const LogRec& r){
  return r.seq != 0xFFFFFFFFu && r.code != 0 && r.code != 0xFF && r.crc == logCrc(r);
}

class EventLog {
 public:
  // Find where the log ends. False if the storage cannot be read.
  bool begin(const LogStore* store){
    _s = store;
    _slots = store->size / LOG_REC_BYTES;
    _ok = false;
    _count = 0;
    _lastBoot = 0;
    bool any = false;
    uint32_t best = 0, bestSlot = 0, bootSeq = 0;
    LogRec buf[16];
    for(uint32_t slot = 0; slot < _slots; slot += 16){
      if(!_s->read(slot * LOG_REC_BYTES, buf, sizeof(buf))){
        return false;
      }
      for(uint32_t i = 0; i < 16; i++){
        if(!logValid(buf[i])){
          continue;
        }
        _count++;
        if(!any || buf[i].seq > best){
          any = true; best = buf[i].seq; bestSlot = slot + i;
        }
        if(buf[i].code == LOG_BOOT && buf[i].seq >= bootSeq){
          bootSeq = buf[i].seq; _lastBoot = (uint32_t)buf[i].c;
        }
      }
    }
    _seq = any ? best + 1 : 1;
    _head = any ? (bestSlot + 1) % _slots : 0;
    // A write cut short by a power loss leaves a slot that is neither valid
    // nor blank. Step over those: flash can only be rewritten after an erase.
    while(_head % LOG_PER_SECTOR != 0 && !slotBlank(_head)){
      _head = (_head + 1) % _slots;
    }
    _ok = true;
    return true;
  }

  bool ok() const { return _ok; }
  uint32_t capacity() const { return _slots; }
  uint32_t count() const { return _count; }       // valid records found by begin(), plus appended
  uint32_t lastBootNumber() const { return _lastBoot; }

  // True if the next append() has to erase a sector first (tens of ms).
  bool nextNeedsErase(){
    return _ok && _head % LOG_PER_SECTOR == 0 && !sectorBlank(_head);
  }

  // One slow step at most: either erases the next sector or writes the record.
  // Returns true once the record is written. The caller keeps the record and
  // calls again until then.
  bool append(LogRec r){
    if(!_ok){
      return false;
    }
    if(nextNeedsErase()){
      uint32_t lost = countValid(_head, LOG_PER_SECTOR);
      if(!_s->erase(_head * LOG_REC_BYTES, LOG_SECTOR_BYTES)){
        _ok = false;
      }
      _count -= lost < _count ? lost : _count;
      return false;
    }
    r.seq = _seq;
    r.crc = logCrc(r);
    if(!_s->write(_head * LOG_REC_BYTES, &r, sizeof(r))){
      _ok = false;
      return false;
    }
    _seq++;
    _count++;
    _head = (_head + 1) % _slots;
    return true;
  }

  // Calls f(rec) for the newest `lastN` records, oldest first.
  template <class F> void forEach(uint32_t lastN, F f){
    if(!_ok){
      return;
    }
    uint32_t skip = _count > lastN ? _count - lastN : 0;
    LogRec buf[16];
    uint32_t last = 0;
    bool first = true;
    // Slots after the head hold the oldest records.
    for(uint32_t n = 0; n < _slots; ){
      uint32_t slot = (_head + n) % _slots;
      uint32_t run = 16 - slot % 16;
      if(run > _slots - n){
        run = _slots - n;
      }
      if(!_s->read(slot * LOG_REC_BYTES, buf, run * LOG_REC_BYTES)){
        return;
      }
      for(uint32_t i = 0; i < run; i++){
        if(!logValid(buf[i]) || (!first && buf[i].seq <= last)){
          continue;
        }
        first = false;
        last = buf[i].seq;
        if(skip){
          skip--;
          continue;
        }
        f(buf[i]);
      }
      n += run;
    }
  }

 private:
  bool slotBlank(uint32_t slot){
    uint8_t b[LOG_REC_BYTES];
    if(!_s->read(slot * LOG_REC_BYTES, b, sizeof(b))){
      return false;
    }
    for(unsigned i = 0; i < sizeof(b); i++){
      if(b[i] != 0xFF){
        return false;
      }
    }
    return true;
  }
  bool sectorBlank(uint32_t firstSlot){
    uint8_t b[256];
    for(uint32_t off = 0; off < LOG_SECTOR_BYTES; off += sizeof(b)){
      if(!_s->read(firstSlot * LOG_REC_BYTES + off, b, sizeof(b))){
        return false;
      }
      for(unsigned i = 0; i < sizeof(b); i++){
        if(b[i] != 0xFF){
          return false;
        }
      }
    }
    return true;
  }
  uint32_t countValid(uint32_t firstSlot, uint32_t n){
    LogRec buf[16];
    uint32_t valid = 0;
    for(uint32_t i = 0; i < n; i += 16){
      if(!_s->read((firstSlot + i) * LOG_REC_BYTES, buf, sizeof(buf))){
        break;
      }
      for(int k = 0; k < 16; k++){
        valid += logValid(buf[k]);
      }
    }
    return valid;
  }

  const LogStore* _s = nullptr;
  uint32_t _slots = 0, _head = 0, _seq = 1, _count = 0, _lastBoot = 0;
  bool _ok = false;
};

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

static inline const char* logResetReason(int r){
  switch(r){   // esp_reset_reason_t
    case 1: return "power on";
    case 2: return "external reset";
    case 3: return "software restart";
    case 4: return "crash";
    case 5: case 6: case 7: return "watchdog";
    case 8: return "deep sleep";
    case 9: return "brownout";
    case 11: return "USB";
    default: return "unknown reason";
  }
}

// One line of text for a record, without the time. Returns the length.
static inline int logText(const LogRec& r, char* out, size_t n){
  int32_t c = r.c;
  unsigned lo = (uint32_t)c & 0xFFFF, hi = (uint32_t)c >> 16;
  switch(r.code){
    case LOG_BOOT: return snprintf(out, n, "BOOT %ld: %s", (long)c, logResetReason(r.a));
    case LOG_FIRMWARE: return snprintf(out, n, "firmware build %08lX", (unsigned long)(uint32_t)c);
    case LOG_CLOCK: return snprintf(out, n, "clock set from the network");
    case LOG_SCALE_READY:
      if(r.a){
        return snprintf(out, n, "scale ready: connect took %ld ms, signal -%u dBm", (long)c, r.a);
      }
      return snprintf(out, n, "scale ready: connect took %ld ms", (long)c);
    case LOG_SCALE_CONNECT_FAIL: return snprintf(out, n, "scale found but connect failed after %ld ms (%u in a row)", (long)c, r.a);
    case LOG_SCALE_SETUP_FAIL: {
      static const char* const steps[] = { "?", "subscribe", "identify", "notification request", "mode set",
                                           "attribute discovery", "scale type", "write characteristic" };
      const char* step = steps[r.a < 8 ? r.a : 0];
      return snprintf(out, n, "scale connected but set-up failed at: %s", step);
    }
    case LOG_SCALE_LINK_LOST: return snprintf(out, n, "scale link lost%s, %ld ms after its last weight", r.a ? " DURING A SHOT" : "", (long)c);
    case LOG_SCALE_TIMEOUT: return snprintf(out, n, "scale stopped sending weight%s: dropped after %ld ms", r.a ? " DURING A SHOT" : "", (long)c);
    case LOG_SCALE_WRITE_FAIL: return snprintf(out, n, "command to the scale failed%s: link dropped", r.a ? " DURING A SHOT" : "");
    case LOG_SCALE_SESSION:
      return snprintf(out, n, "scale was connected for %ld s, %u%s bad packets", (long)c, r.a, r.a == 255 ? "+" : "");
    case LOG_TARE_RETRY: return snprintf(out, n, "tare not confirmed, retry %u", r.a);
    case LOG_TARE_FAILED: return snprintf(out, n, "tare FAILED: shot not weighed");
    case LOG_TIMER_RETRY: return snprintf(out, n, "scale timer not running, retry %u", r.a);
    case LOG_TIMER_FAILED: return snprintf(out, n, "scale timer failed to start (display only)");
    case LOG_PADDLE:
      return snprintf(out, n, "%s", r.a == 0 ? "paddle off" : r.a == 1 ? "paddle on"
                            : r.a == 2 ? "paddle on, noticed late: shot left to the paddle"
                            : "paddle on right after an off: shot left to the paddle");
    case LOG_SHOT_START:
      return snprintf(out, n, "shot start: %s%s%s, goal %u.%u g, offset %u.%u g",
                      (r.a & LOG_SS_MANUAL) ? "manual" : (r.a & LOG_SS_WEIGHED) ? "weighed" : "NOT weighed",
                      (r.a & LOG_SS_STALE) ? " (no recent weight from the scale)" : "",
                      (r.a & LOG_SS_OUNCES) ? " (scale in ounces)" : "",
                      lo / 10, lo % 10, hi / 10, hi % 10);
    case LOG_SHOT_END: {
      const char* why = r.a == 0 ? "time limit" : r.a == 1 ? "weight" : r.a == 2 ? "paddle" : r.a == 3 ? "SCALE LOST" : "undefined";
      return snprintf(out, n, "shot end by %s after %ld.%ld s", why, (long)(c / 1000), (long)(c % 1000 / 100));
    }
    case LOG_SHOT_LINK: return snprintf(out, n, "  shot link: %u weight packets, longest gap %u ms, %u gaps over %d ms", lo, hi, r.a, LOG_GAP_MS);
    case LOG_SHOT_QUALITY:
      return snprintf(out, n, "  shot quality: %s%s%s, %u bad packets, peak flow %u.%02u g/s",
                      (r.a & LOG_SQ_TARED) ? "tare confirmed" : "tare NOT confirmed",
                      (r.a & LOG_SQ_DISTURBED) ? ", implausible readings ignored" : "",
                      (r.a & LOG_SQ_LEARN) ? "" : ", not used for learning", lo, hi / 100, hi % 100);
    case LOG_SHOT_WEIGHT:
      if(!r.a){
        return snprintf(out, n, "  final weight not available");
      }
      return snprintf(out, n, "  final weight %s%ld.%02ld g", c < 0 ? "-" : "", (long)(labs(c) / 100), (long)(labs(c) % 100));
    case LOG_OFFSET:
      return snprintf(out, n, r.a ? "  offset learned: %ld.%ld g" : "  offset unchanged at %ld.%ld g (error too large)", (long)(c / 10), (long)(labs(c) % 10));
    case LOG_GAP: return snprintf(out, n, "  no weight for %ld ms at %u s into the shot", (long)c, r.a);
    case LOG_INTERLOCK: return snprintf(out, n, "%s", r.a ? "brew stopped: interlock open" : "paddle off: interlock closed");
    case LOG_RESUME_HOLD: return snprintf(out, n, "reset during a stop or a weighed shot: brew held off until paddle off");
    case LOG_FAULT: return snprintf(out, n, "FAULT: plugs look swapped, interlock disabled until restart");
    case LOG_MAX_BREW: return snprintf(out, n, "paddle on too long: brew stopped");
    case LOG_READINGS_LOST: return snprintf(out, n, "weight readings not believable (cup lifted?)");
    case LOG_MODE: return snprintf(out, n, "mode: %s", r.a ? "manual" : "brew by weight");
    case LOG_GOAL: return snprintf(out, n, "goal weight set to %ld g", (long)c);
    case LOG_BATTERY: return snprintf(out, n, "scale battery %u%%", r.a);
    case LOG_OUNCES: return snprintf(out, n, "scale is set to ounces: shots not weighed");
    case LOG_WIFI:
      if(r.a == 2){
        return snprintf(out, n, "WiFi connected, signal %ld dBm", (long)c);
      }
      return snprintf(out, n, "WiFi %s", r.a ? "on" : "off");
    case LOG_OTA:
      switch(r.a){
        case LOG_OTA_REQUESTED: return snprintf(out, n, "WiFi window opened over Bluetooth");
        case LOG_OTA_CLOSED: return snprintf(out, n, "WiFi window closed over Bluetooth");
        case LOG_OTA_PROBATION: return snprintf(out, n, "firmware started by the recovery firmware: on probation");
        case LOG_OTA_CONFIRMED: return snprintf(out, n, "firmware confirmed");
        case LOG_OTA_TO_RECOVERY:
          return snprintf(out, n, "RESTARTING INTO RECOVERY: %s", c == 1 ? "asked over Bluetooth" : c == 2 ? "asked from the web page"
                                 : c == 3 ? "crashed several times in a row" : "power switched on and off several times");
      }
      break;
    case LOG_LOOP_STALL: return snprintf(out, n, "firmware busy for %ld ms%s", (long)c, r.a ? " DURING A SHOT" : "");
    case LOG_DROPPED: return snprintf(out, n, "%ld events lost (queue full)", (long)c);
  }
  return snprintf(out, n, "event %u a=%u c=%ld", r.code, r.a, (long)c);
}

// Turns records into dated lines. Records carry only milliseconds since their
// boot; a LOG_CLOCK record ties one boot to the calendar. Feed every record to
// scan() first, then to line() in the same order.
class LogPrinter {
 public:
  void scan(const LogRec& r){
    if(r.code == LOG_BOOT){
      _scanBoot++;
    }else if(r.code == LOG_CLOCK && _scanBoot < MAX_BOOTS){
      _epochAtBoot[_scanBoot] = (uint32_t)r.c - r.t_ms / 1000;
    }
  }
  int line(const LogRec& r, char* out, size_t n){
    if(r.code == LOG_BOOT){
      _boot++;
    }
    int len;
    uint32_t base = _boot < MAX_BOOTS ? _epochAtBoot[_boot] : 0;
    if(base){
      time_t t = (time_t)(base + r.t_ms / 1000);
      struct tm tmv;
      localtime_r(&t, &tmv);
      len = (int)strftime(out, n, "%Y-%m-%d %H:%M:%S", &tmv);
      len += snprintf(out + len, n - len, ".%lu  ", (unsigned long)(r.t_ms % 1000 / 100));
    }else{
      // No network time for this boot: time since the board started.
      unsigned long s = r.t_ms / 1000;
      len = snprintf(out, n, "    +%4luh%02lum%02lu.%lus  ", s / 3600, s / 60 % 60, s % 60, (unsigned long)(r.t_ms % 1000 / 100));
    }
    len += logText(r, out + len, n - len);
    if(len > (int)n - 2){
      len = (int)n - 2;
    }
    out[len++] = '\n';
    out[len] = 0;
    return len;
  }
 private:
  static const unsigned MAX_BOOTS = 256;
  uint32_t _epochAtBoot[MAX_BOOTS] = {0};  // index 0: records from before the first BOOT in the log
  unsigned _scanBoot = 0, _boot = 0;
};

#endif
