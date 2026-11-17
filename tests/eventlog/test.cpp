// Host test of examples/shotStopper/eventlog.h against a simulated NOR flash:
// writes can only clear bits, erase sets a whole sector, and a write can be
// cut short (power loss). Checks that the log always reads back as an
// unbroken run of the newest records.
#include "eventlog.h"
#include <vector>
#include <random>
#include <algorithm>
#include <cassert>

static std::vector<uint8_t> flash;
static int tearAfter = -1;      // bytes of the next write that reach the flash, -1 = all
static long erases = 0;
static bool rd(uint32_t o, void* b, uint32_t n){ if(o + n > flash.size()) return false; memcpy(b, &flash[o], n); return true; }
static bool wr(uint32_t o, const void* b, uint32_t n){
  if(o + n > flash.size()) return false;
  uint32_t m = tearAfter >= 0 ? (uint32_t)tearAfter : n;
  for(uint32_t i = 0; i < m && i < n; i++) flash[o + i] &= ((const uint8_t*)b)[i];
  tearAfter = -1;
  return true;
}
static bool er(uint32_t o, uint32_t n){
  if(o % LOG_SECTOR_BYTES || n != LOG_SECTOR_BYTES || o + n > flash.size()) return false;
  memset(&flash[o], 0xFF, n); erases++; return true;
}
static LogStore store;
static int failures = 0;
#define CHECK(x) do{ if(!(x)){ printf("FAIL line %d: %s\n", __LINE__, #x); failures++; } }while(0)

static LogRec rec(uint8_t code, int32_t c){ LogRec r{}; r.code = code; r.c = c; r.t_ms = (uint32_t)c * 10; return r; }
static void put(EventLog& log, LogRec r){ int tries = 0; while(!log.append(r)){ CHECK(log.ok()); CHECK(++tries < 3); } }
static std::vector<int32_t> dump(EventLog& log, uint32_t n = 1000000){
  std::vector<int32_t> v; log.forEach(n, [&](const LogRec& r){ v.push_back(r.c); }); return v;
}
// v must be a run of consecutive values ending at `last`
static bool isSuffix(const std::vector<int32_t>& v, int32_t last){
  for(size_t i = 0; i < v.size(); i++) if(v[i] != last - (int32_t)(v.size() - 1 - i)) return false;
  return true;
}

int main(){
  std::mt19937 rng(1);
  store = { 0, rd, wr, er };

  // 1. blank flash
  flash.assign(4 * LOG_SECTOR_BYTES, 0xFF); store.size = flash.size();
  { EventLog log; CHECK(log.begin(&store)); CHECK(log.count() == 0);
    for(int i = 1; i <= 10; i++) put(log, rec(LOG_GAP, i));
    auto v = dump(log); CHECK(v.size() == 10 && isSuffix(v, 10)); CHECK(erases == 0);
    CHECK(dump(log, 3).size() == 3 && isSuffix(dump(log, 3), 10)); }

  // 2. wrap around several times, restarting now and then
  { EventLog log; CHECK(log.begin(&store)); CHECK(log.count() == 10);
    uint32_t slots = store.size / LOG_REC_BYTES;
    for(int i = 11; i <= 5000; i++){
      put(log, rec(LOG_GAP, i));
      if(i % 613 == 0){ CHECK(log.begin(&store)); }
      if(i % 97 == 0){ auto v = dump(log); CHECK(isSuffix(v, i)); CHECK(v.size() >= std::min<uint32_t>(i, slots - LOG_PER_SECTOR) && v.size() <= slots); CHECK(v.size() == log.count()); }
    } }

  // 3. flash full of junk (an old filesystem)
  for(auto& b : flash) b = rng();
  { EventLog log; CHECK(log.begin(&store)); CHECK(log.count() == 0);
    for(int i = 1; i <= 300; i++) put(log, rec(LOG_GAP, i));
    auto v = dump(log); CHECK(v.size() == 300 && isSuffix(v, 300)); }

  // 4. boot number comes back from the log
  flash.assign(4 * LOG_SECTOR_BYTES, 0xFF);
  { EventLog log; CHECK(log.begin(&store)); CHECK(log.lastBootNumber() == 0);
    put(log, rec(LOG_BOOT, 1)); put(log, rec(LOG_GAP, 5)); put(log, rec(LOG_BOOT, 2));
    CHECK(log.begin(&store)); CHECK(log.lastBootNumber() == 2); }

  // 5. random run with power losses in the middle of writes and after erases
  for(int round = 0; round < 200; round++){
    flash.assign((2 + rng() % 4) * LOG_SECTOR_BYTES, 0xFF); store.size = flash.size();
    uint32_t slots = store.size / LOG_REC_BYTES;
    EventLog log; CHECK(log.begin(&store));
    int32_t next = 1, torn = 0;
    int steps = 200 + rng() % 3000;
    for(int i = 0; i < steps; i++){
      unsigned dice = rng() % 400;
      if(dice == 0){                       // power loss during a write
        if(log.nextNeedsErase()){ log.append(rec(LOG_GAP, next)); }   // the erase completes...
        else { tearAfter = rng() % 16; log.append(rec(LOG_GAP, next)); torn++; }
        CHECK(log.begin(&store));          // ...and the board restarts; that record is lost
      }else if(dice == 1){
        CHECK(log.begin(&store));
      }else{
        put(log, rec(LOG_GAP, next)); next++;
      }
    }
    CHECK(log.begin(&store));
    auto v = dump(log);
    CHECK(isSuffix(v, next - 1));
    uint32_t want = (uint32_t)(next - 1) < slots - LOG_PER_SECTOR - torn ? (uint32_t)(next - 1) - 0 : slots - LOG_PER_SECTOR - torn;
    if((uint32_t)(next - 1) >= slots) CHECK(v.size() >= want);
  }

  // 6. text
  { LogPrinter p; char line[200];
    LogRec b = rec(LOG_BOOT, 7); b.a = 1; b.t_ms = 20;
    LogRec ck = rec(LOG_CLOCK, 1794900000); ck.t_ms = 5000;
    LogRec s = rec(LOG_SHOT_END, 27400); s.a = 1; s.t_ms = 65000;
    for(auto* r : { &b, &ck, &s }) p.scan(*r);
    setenv("TZ", "UTC0", 1); tzset();
    for(auto* r : { &b, &ck, &s }){ p.line(*r, line, sizeof line); printf("%s", line); }
    CHECK(strstr(line, "shot end by weight after 27.4 s"));
    CHECK(strstr(line, "2026-11-17")); }

  printf(failures ? "%d FAILURES\n" : "eventlog: all checks passed\n", failures);
  return failures != 0;
}
