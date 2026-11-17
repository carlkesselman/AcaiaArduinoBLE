// shotStopper recovery firmware (Micra Interlock board, ESP32-C3).
//
// A small, fixed firmware in the `factory` partition. It never changes after
// the first USB flash, so there is always a way to load a new main firmware,
// whatever state the main firmware is in. It does three things:
//
//   1. accepts a main firmware over WiFi (ArduinoOTA, the usual upload password)
//   2. shows the event log the main firmware wrote (read-only)
//   3. hands back to the main firmware
//
// It does not touch the machine: STOP is held low, so the interlock stays
// closed and the paddle works by hand. There is no brew-by-weight in recovery.
//
// The board gets here when
//   - there is no main firmware yet, or it is damaged,
//   - a main firmware reset before it confirmed itself (the bootloader then
//     refuses to start it; recovery tries it twice more, then waits for an upload),
//   - the main firmware asked for it: over Bluetooth, from its web page, after
//     three crashes in a row, or after the power was switched on and off five
//     times in a row.
//
// WiFi: joins the network in secrets.h. If that fails for WIFI_JOIN_S it opens
// its own network instead, "shotstopper-recovery", password = OTA_PASSWORD,
// address 192.168.4.1. That also covers a router that has been replaced.
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include "secrets.h"
#include "eventlog.h"

#ifndef OTA_HOSTNAME
  #define OTA_HOSTNAME "shotstopper"
#endif
#ifndef LOG_TZ
  #define LOG_TZ "UTC0"
#endif
static_assert(sizeof(OTA_PASSWORD) - 1 >= 8, "OTA_PASSWORD must be at least 8 characters: it is also the password of the recovery WiFi network");

#define STOP        5     // HIGH opens the paddle line. Recovery never does.
#define LED_RED     21    // active low
#define LED_BLUE    10
#define LED_GREEN   20
#define RECOVERY_AP "shotstopper-recovery"
#define WIFI_JOIN_S 25          // then open our own network
#define AP_RETRY_S 180          // with nobody connected to it, try the home network again
#define STAY_REQUESTED_S 600    // main firmware asked for recovery: wait this long for an upload
#define STAY_POWER_ON_S 30      // otherwise (power on): a short look, then start the main firmware
#define MAIN_RETRIES 2          // a main firmware that failed its start is tried again this many times:
                                //  switching the machine off during its first 30 s looks the same as a crash
#define WATCHDOG_S 30

WebServer server(80);
const esp_partition_t* mainPart = NULL;
const esp_partition_t* logPart = NULL;
LogStore logStore;
EventLog eventLog;
bool mainFailed = false;        // the bootloader has marked the main firmware as bad
bool giveUp = false;            // ...and it has used up its retries: wait for an upload
Preferences prefs;
bool mainMissing = false;       // nothing bootable in the main slot
bool uploading = false;
bool apMode = false;
bool otaStarted = false;
unsigned long stay_ms = 0;
unsigned long wifiPhase_ms = 0;
const char* whyHere = "";

void led(bool r, bool g, bool b){
  digitalWrite(LED_RED, !r); digitalWrite(LED_GREEN, !g); digitalWrite(LED_BLUE, !b);
}

bool logRead(uint32_t o, void* b, uint32_t n){ return esp_partition_read(logPart, o, b, n) == ESP_OK; }
bool logNoWrite(uint32_t, const void*, uint32_t){ return false; }   // recovery only reads the log
bool logNoErase(uint32_t, uint32_t){ return false; }

// Start the main firmware. It comes up "on probation" and has to confirm itself.
bool bootMain(){
  if(!mainPart || esp_ota_set_boot_partition(mainPart) != ESP_OK){   // also checks the image
    mainMissing = true;
    return false;
  }
  Serial.println("starting the main firmware");
  Serial.flush();
  delay(100);
  ESP.restart();
  return true;
}

void startWifiClient(){
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  apMode = false;
  wifiPhase_ms = millis();
  Serial.println("WiFi: joining " WIFI_SSID);
}

void startWifiAp(){
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(RECOVERY_AP, OTA_PASSWORD);
  apMode = true;
  wifiPhase_ms = millis();
  Serial.println("WiFi: own network " RECOVERY_AP " at 192.168.4.1");
}

bool authorised(){
  if(server.authenticate("ota", OTA_PASSWORD)){
    return true;
  }
  server.requestAuthentication();
  return false;
}

void pageStatus(){
  char buf[900];
  long left = (long)(stay_ms - millis()) / 1000;
  snprintf(buf, sizeof(buf),
    "shotStopper RECOVERY firmware (built %s %s)\n"
    "The machine works by hand. There is no brew-by-weight until the main firmware runs.\n\n"
    "Why the board is here: %s\n"
    "Main firmware: %s\n"
    "Network: %s\n"
    "%s\n\n"
    "To load a main firmware:   pio run -e micra_wifi -t upload\n"
    "Event log:                 /log   (/log?n=200 for fewer)\n"
    "Start the main firmware:   POST /boot-main   (user ota, the upload password)\n",
    __DATE__, __TIME__, whyHere,
    giveUp ? "FAILED to start three times" : mainFailed ? "failed its last start" : mainMissing ? "none, or damaged" : "present",
    apMode ? "own network " RECOVERY_AP " (could not join " WIFI_SSID ")" : "joined " WIFI_SSID,
    (giveUp || mainMissing) ? "Staying here until a firmware is uploaded."
                                : (String("Starting the main firmware in ") + (left > 0 ? left : 0) + " s unless an upload begins.").c_str());
  server.send(200, "text/plain; charset=utf-8", buf);
}

void pageLog(){
  if(!eventLog.ok()){
    server.send(200, "text/plain", "no event log found\n");
    return;
  }
  uint32_t n = server.hasArg("n") ? (uint32_t)server.arg("n").toInt() : 2000;
  if(n == 0){
    n = 2000;
  }
  static LogPrinter printer;
  printer = LogPrinter();
  eventLog.forEach(n, [](const LogRec& r){ printer.scan(r); });
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain; charset=utf-8", "");
  char line[200];
  String chunk;
  chunk.reserve(1600);
  snprintf(line, sizeof(line), "shotStopper event log, read by the recovery firmware\n%lu events stored, times in %s\n\n",
           (unsigned long)eventLog.count(), LOG_TZ);
  chunk += line;
  eventLog.forEach(n, [&](const LogRec& r){
    printer.line(r, line, sizeof(line));
    chunk += line;
    if(chunk.length() > 1200){
      server.sendContent(chunk);
      chunk = "";
      esp_task_wdt_reset();
    }
  });
  server.sendContent(chunk);
  server.sendContent("");
}

void pageBootMain(){
  if(!authorised()){
    return;
  }
  server.send(200, "text/plain", "starting the main firmware\n");
  delay(200);
  if(!bootMain()){
    Serial.println("no bootable main firmware");
  }
}

void startServices(){
  if(otaStarted){
    return;
  }
  otaStarted = true;
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([](){ uploading = true; Serial.println("upload starting"); });
  ArduinoOTA.onProgress([](unsigned int, unsigned int){ esp_task_wdt_reset(); led(false, false, (millis() / 100) % 2); });
  ArduinoOTA.onEnd([](){ prefs.putUChar("retries", 0); Serial.println("upload done: starting the main firmware"); });
  ArduinoOTA.onError([](ota_error_t e){ uploading = false; Serial.printf("upload error %d\n", (int)e); });
  ArduinoOTA.begin();
  server.on("/", pageStatus);
  server.on("/log", pageLog);
  server.on("/boot-main", HTTP_POST, pageBootMain);
  server.begin();
}

void setup(){
  digitalWrite(STOP, LOW);
  pinMode(STOP, OUTPUT);
  digitalWrite(STOP, LOW);
  pinMode(LED_RED, OUTPUT); pinMode(LED_GREEN, OUTPUT); pinMode(LED_BLUE, OUTPUT);
  led(true, true, false);
  Serial.begin(115200);
  esp_task_wdt_init(WATCHDOG_S, true);
  esp_task_wdt_add(NULL);
  setenv("TZ", LOG_TZ, 1);
  tzset();

  mainPart = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
  esp_ota_img_states_t st;
  if(mainPart && esp_ota_get_state_partition(mainPart, &st) == ESP_OK){
    mainFailed = st == ESP_OTA_IMG_ABORTED || st == ESP_OTA_IMG_INVALID;
  }
  esp_app_desc_t desc;
  mainMissing = !mainPart || esp_ota_get_partition_description(mainPart, &desc) != ESP_OK;

  // Count the starts that failed, so a main firmware that keeps failing is
  // not retried for ever. A new upload, or a main firmware that got as far as
  // asking for recovery, starts the count again.
  prefs.begin("recovery", false);
  uint8_t retries = prefs.getUChar("retries", 0);
  if(!mainFailed){
    if(retries){
      prefs.putUChar("retries", 0);
    }
  }else if(retries >= MAIN_RETRIES){
    giveUp = true;
  }else{
    prefs.putUChar("retries", retries + 1);
  }

  bool requested = esp_reset_reason() == ESP_RST_SW;
  whyHere = mainMissing ? "there is no main firmware"
          : giveUp      ? "the main firmware reset before it confirmed itself, three times"
          : mainFailed  ? "the main firmware reset before it confirmed itself (it will be tried again)"
          : requested   ? "the main firmware asked for it (Bluetooth, web page, repeated crashes, or the power-switch signal)"
                        : "power on with no main firmware selected";
  stay_ms = millis() + (requested ? STAY_REQUESTED_S : STAY_POWER_ON_S) * 1000UL;
  Serial.printf("shotStopper recovery: %s\n", whyHere);

  logPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
  if(logPart){
    logStore.size = logPart->size - logPart->size % LOG_SECTOR_BYTES;
    logStore.read = logRead; logStore.write = logNoWrite; logStore.erase = logNoErase;
    eventLog.begin(&logStore);
  }
  startWifiClient();
}

void loop(){
  esp_task_wdt_reset();
  unsigned long now = millis();

  // Slow yellow blink: recovery. Fast blue (set in onProgress): uploading.
  if(!uploading){
    bool on = (now / 1000) % 2;
    led(on, on, apMode && on);   // white when on its own network
  }

  if(!apMode){
    if(WiFi.status() == WL_CONNECTED){
      startServices();
    }else if(now - wifiPhase_ms > WIFI_JOIN_S * 1000UL){
      startWifiAp();
      startServices();
    }
  }else if(!uploading && WiFi.softAPgetStationNum() == 0 && now - wifiPhase_ms > AP_RETRY_S * 1000UL){
    startWifiClient();
  }

  if(otaStarted){
    ArduinoOTA.handle();
    server.handleClient();
  }

  // Nothing was uploaded: go back to the main firmware, unless it keeps failing.
  if(!uploading && !giveUp && !mainMissing && (long)(now - stay_ms) > 0){
    bootMain();
  }
  delay(5);
}
