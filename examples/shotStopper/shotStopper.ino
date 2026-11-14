/*
  shotStopper.ino - Example of using an acaia scale to brew by weight with an espresso machine

  Immediately Connects to a nearby acaia scale, 
  tare's the scale when the "in" gpio is triggered (active low),
  and then triggers the "out" gpio to stop the shot once ( goalWeight - weightOffset ) is achieved.

  Tested on a Acaia Pyxis, Arduino nano ESP32, and La Marzocco GS3. 

  Note that only the EEPROM library only supports ESP32-based controllers.

  To set the Weight over BLE, use a BLE app such as LightBlue to connect
  to the "shotStopper" BLE device and read/write to the weight characteristic,
  otherwise the weight is defaulted to 36g.

  Created by Tate Mazer, 2023.

  Released under the MIT license.

  https://github.com/tatemazer/AcaiaArduinoBLE

*/

#include <AcaiaArduinoBLE.h>
#include <EEPROM.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_attr.h>
#include <math.h>

#define MAX_OFFSET 5                // In case an error in brewing occured
#define MIN_SHOT_DURATION_S 3       //Useful for flushing the group.
                                    // This ensure that the system will ignore
                                    // "shots" that last less than this duration
#define MAX_SHOT_DURATION_S 50      //Primarily useful for latching switches, since user
                                    // looses control of the paddle once the system
                                    // latches.
#define BUTTON_READ_PERIOD_MS 5
#define DRIP_DELAY_S          3     // Time after the shot ended to measure the final weight

#define EEPROM_SIZE 2  // This is 1-Byte
#define WEIGHT_ADDR 0  // Use the first byte of EEPROM to store the goal weight
#define OFFSET_ADDR 1  

#define DEBUG false

#define N 10                        // Number of datapoints used to calculate trend line


//User defined***
#ifndef MOMENTARY
#define MOMENTARY false       //Define brew switch style. 
                              // True for momentary switches such as GS3 AV, Silvia Pro
                              // false for latching switches such as Linea Mini/Micra
#endif
#define REEDSWITCH false      // Set to true if the brew state is being determined 
                              //  by a reed switch attached to the brew solenoid
#define AUTOTARE true         // Automatically tare when shot is started 
                              // Will tare also 3 seconds after a latching switch brew for non-bookoo scales
                              // (as defined by MOMENTARY)
#define CAN_TARE_START_TIMER false // If set, reset timer, start timer and tare are done in one command (supported by Bookoo scales only)
#define TIMER_ONLY false      // disables brew by weight functionality, and only automates the timer/tare
#ifndef PADDLE_INTERLOCK
#define PADDLE_INTERLOCK false // Linea Mini-style stop for the "ShotStopper Micra Interlock" board
                              //  (hardware/shotstopper-micra-interlock). The paddle stays on;
                              //  at the target weight STOP opens a normally-closed switch in
                              //  series with the paddle, which stops the shot. It closes again
                              //  once the paddle has been turned off. Requires MOMENTARY false.
#endif
#define PADDLE_DEBOUNCE_MS 50 // Paddle must read "off" this long to count as off (interlock mode)
#define INTERLOCK_MAX_BREW_S 90 // Interlock mode: stop any brew once the paddle has been on this
                              //  long, with or without a scale. Above MAX_SHOT_DURATION_S, which
                              //  only covers shots that are being weighed. 0 disables it.
#ifndef STOP_ON_SCALE_LOSS
#define STOP_ON_SCALE_LOSS true // Interlock mode: if the scale is lost during a weighed shot, stop
                              //  the shot (as the V2.1 board does). false leaves it to the paddle.
#endif
#ifndef OTA_ENABLED
#define OTA_ENABLED false     // Over-the-air updates over WiFi. Needs secrets.h (copy
                              //  secrets.example.h). WiFi is only on while idle, for
                              //  OTA_BOOT_WINDOW_S after boot or after an OTA request over BLE.
#endif
#define OTA_BOOT_WINDOW_S 300
#define OTA_REQUEST_WINDOW_S 600
#define WATCHDOG_S 20         // Interlock mode: reset the board if loop() stalls this long.
                              //  On reset the interlock closes, so the paddle works again.
//***************
// Settings marked #ifndef can also be set from platformio.ini build_flags.

#if OTA_ENABLED
  #include <WiFi.h>
  #include <ArduinoOTA.h>
  #if __has_include("secrets.h")
    #include "secrets.h"
  #else
    #error "OTA_ENABLED needs secrets.h: copy secrets.example.h to secrets.h and fill it in"
  #endif
  #ifndef OTA_HOSTNAME
    #define OTA_HOSTNAME "shotstopper"
  #endif
#endif

// Board Hardware 
#ifdef ARDUINO_ESP32S3_DEV
  #define LED_RED     46
  #define LED_BLUE    45
  #define LED_GREEN   47
  #define IN          21
  #define OUT         38
  #define REED_IN     18
#elif defined(ARDUINO_ESP32C3_DEV)
  #define LED_RED     21
  #define LED_BLUE    10
  #define LED_GREEN   20
  #define IN          8
  #define OUT         6
  #define REED_IN     7
#else //todo: find nano esp32 identifier
  //LED's are defined by framework
  // Use the Dn names: they are right whether or not the core remaps pin
  // numbers (Arduino IDE remaps by default, PlatformIO doesn't).
  #define IN          D10
  #define OUT         D11
  #define REED_IN     D9
  #define STOP        D5      // Micra Interlock board: HIGH opens the paddle line
#endif 
#ifndef STOP
  #define STOP        5
#endif

// Weight sanity checks (during a shot)
#define STREAM_STALE_MS 1000  // no weight for this long at paddle-on: shot left to the paddle
#define MAX_STEP_G 5.0        // larger change between two readings is a bump, not espresso
#define MAX_FLOW_GPS 12.0     // faster "flow" than this is a bump
#define MAX_DROP_G 1.0        // weight falling by more than this: cup lifted or knocked
#define LOW_BATTERY_PCT 15

#define BUTTON_STATE_ARRAY_LENGTH 31
#define MAX_DATAPOINTS 1000

typedef enum {BUTTON, WEIGHT, TIME, DISCONNECT, UNDEF} ENDTYPE;

// RGB Colors {Red,Green,Blue}
int RED[3] = {255, 0, 0};
int GREEN[3] = {0, 255, 0};
int BLUE[3] = {0, 0, 255};
int MAGENTA[3] = {255, 0, 255};
int CYAN[3] = {0, 255, 255};
int YELLOW[3] = {255, 255, 0};
int WHITE[3] = {255, 255, 255};
int OFF[3] = {0,0,0};
int currentColor[3] = {0,0,0};

AcaiaArduinoBLE scale(DEBUG);
static unsigned long lastConnectAttempt = 0;
float currentWeight = 0;
uint8_t goalWeight = 0;      // Goal Weight to be read from EEPROM
float weightOffset = 0;
float error = 0;
tare_state lastTareState = TARE_IDLE;
int buttonArr[BUTTON_STATE_ARRAY_LENGTH];            // last 4 readings of the button

// button 
int in = REEDSWITCH ? REED_IN : IN;
bool buttonPressed = false; //physical status of button
bool buttonLatched = false; //electrical status of button
unsigned long lastButtonRead_ms = 0;
int newButtonState = 0;

// paddle interlock (PADDLE_INTERLOCK)
bool interlockOpen = false;          // STOP is HIGH: the machine sees the paddle as off
bool paddleWasOn = false;            // paddle state at the previous loop, for INTERLOCK_MAX_BREW_S
unsigned long paddleOnSince_ms = 0;
// Survives a crash or watchdog reset (not a power cycle), so a held stop stays
// held instead of letting the machine restart a finished shot.
#define INTERLOCK_HELD 0x5AFE5709
RTC_NOINIT_ATTR uint32_t savedInterlock;

struct Shot {
  float start_timestamp_s; // Relative to runtime
  float shotTimer;         // Reset when the final drip measurement is made
  float end_s;             // Number of seconds after the shot started
  float expected_end_s;    // Estimated duration of the shot
  float weight[MAX_DATAPOINTS]; // A scatter plot of the weight measurements, along with time_s[]
  float time_s[MAX_DATAPOINTS]; // Number of seconds after the shot started
  int datapoints;          // Number of datapoitns in the scatter plot
  bool brewing;            // True when actively brewing, otherwise false
  ENDTYPE end;
  float flow;              // g/s, slope of the last N accepted readings
  float peakFlow;          // g/s
  bool tracked;            // weight data is trusted for this shot (stop by weight allowed)
  bool disturbed;          // implausible readings were seen; don't learn the offset
  bool learn;              // set at the end: this shot may adjust weightOffset
  float lastWeight;        // last accepted reading (NAN = none yet)
  float lastTime;
};

//Initialize shot
Shot shot = {0,0,0,0,{},{},0,false,ENDTYPE::UNDEF,0,0,true,false,false,NAN,0};

//BLE peripheral device
BLEService weightService("0x0FFE"); // create service
BLEByteCharacteristic weightCharacteristic("0xFF11",  BLEWrite | BLERead);
BLEFloatCharacteristic flowCharacteristic("0xFF12",  BLERead | BLENotify);   // g/s during a shot
BLEByteCharacteristic batteryCharacteristic("0xFF13", BLERead | BLENotify);  // scale battery %, 255 = unknown
BLEByteCharacteristic otaCharacteristic("0xFF14",     BLEWrite | BLERead);   // write 1 to open the OTA window, 0 to close
unsigned long otaWindowEnd_ms = OTA_BOOT_WINDOW_S * 1000UL;
int lastBattery = -2;
timer_state lastTimerState = TIMER_IDLE;
bool unitWarned = false;

void setup() {
  setCpuFrequencyMhz(80);
  Serial.begin(115200); // per-packet logging at 9600 baud adds loop latency
  EEPROM.begin(EEPROM_SIZE);

  // Get stored setpoint and offset
  goalWeight = EEPROM.read(WEIGHT_ADDR);
  weightOffset = EEPROM.read(OFFSET_ADDR)/10.0;
  Serial.print("Goal Weight retrieved: ");
  Serial.println(goalWeight);
  Serial.print("offset retrieved: ");
  Serial.println(weightOffset);

  //If eeprom isn't initialized and has an 
  // unreasonable weight/offset, default to 36g/1.5g
  if( (goalWeight < 10) || (goalWeight > 200) ){
    goalWeight = 36;
    Serial.print("Goal Weight set to: ");
    Serial.println(goalWeight);
  }
  if(weightOffset > MAX_OFFSET){
    weightOffset = 1.5;
    Serial.print("Offset set to: ");
    Serial.println(weightOffset);
  }
  
  // initialize the GPIO hardware
  if(PADDLE_INTERLOCK){
    digitalWrite(STOP, LOW); // interlock closed: the paddle works normally
    pinMode(STOP, OUTPUT);
  }
  pinMode(in, INPUT_PULLUP);
  pinMode(OUT, OUTPUT);
  if(PADDLE_INTERLOCK){
    delay(20);
    bool paddleOn = !digitalRead(in);
    bool warmReset = esp_reset_reason() != ESP_RST_POWERON;
    if(paddleOn && warmReset && savedInterlock == INTERLOCK_HELD){
      // Reset while a stop was being held: keep holding until the paddle is off.
      setInterlock(true);
      Serial.println("reset during a held stop: interlock open until paddle off");
    }else{
      // Otherwise the paddle passes straight through, as on a stock machine.
      setInterlock(false);
    }
    // A shot already running at boot is left to the paddle, not tracked
    // (taring partway through would overshoot the target).
    buttonPressed = paddleOn;
    newButtonState = paddleOn;
    paddleWasOn = paddleOn;
    paddleOnSince_ms = millis();
    for(int i=0; i<BUTTON_STATE_ARRAY_LENGTH; i++){
      buttonArr[i] = paddleOn;
    }
    startWatchdog();
  }
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  setColor(OFF);

  // initialize the BLE hardware
  BLE.begin();
  BLE.setLocalName("shotStopper");
  BLE.setAdvertisedService(weightService);
  weightService.addCharacteristic(weightCharacteristic);
  weightService.addCharacteristic(flowCharacteristic);
  weightService.addCharacteristic(batteryCharacteristic);
  weightService.addCharacteristic(otaCharacteristic);
  BLE.addService(weightService);
  weightCharacteristic.writeValue(goalWeight);
  flowCharacteristic.writeValue(0.0f);
  batteryCharacteristic.writeValue(255);
  otaCharacteristic.writeValue(OTA_ENABLED ? 1 : 0);
  BLE.advertise();
  Serial.println("Bluetooth® device active, waiting for connections...");
}

void loop() {
  if(PADDLE_INTERLOCK){
    esp_task_wdt_reset();
  }
  readButton();
  paddleInterlock_task();
  peripheral_task();
  ota_task();

  // Without a scale the paddle works by hand. Track it so that reconnecting
  // in the middle of a shot does not start (and tare) a new one.
  if(PADDLE_INTERLOCK && !scale.isConnected()){
    buttonPressed = newButtonState;
  }

  // Attempt to connect to scale every 1 seconds. Not while a stop is held:
  // connecting can block for seconds and would delay closing the interlock.
  if(!scale.isConnected() && millis() - lastConnectAttempt >= 1000 && !interlockOpen){
      lastConnectAttempt = millis();
      setColor(RED);
      unitWarned = false;      // re-check the new connection
      lastBattery = -2;
      lastTimerState = TIMER_IDLE;
      scale.init(); 
      currentWeight = 0;
      if(shot.brewing){
        shot.brewing = false;
        shot.end = ENDTYPE::DISCONNECT;
        setBrewingState(false);
      }
      if(scale.isConnected()){
        setColor(YELLOW);
      }
  }
  else if(scale.isConnected()){
    scaleConnected_task();
  }
}

void setBrewingState(bool brewing){
  if(brewing){
    Serial.println("shot started");
    shot.start_timestamp_s = seconds_f();
    shot.shotTimer = 0;
    shot.datapoints = 0;
    shot.expected_end_s = MAX_SHOT_DURATION_S; // don't reuse the previous shot's prediction
    shot.flow = 0;
    shot.peakFlow = 0;
    shot.disturbed = false;
    shot.learn = false;
    shot.lastWeight = NAN;
    // Only trust the scale for this shot if it is streaming weight right now
    // and is set to grams. Otherwise the shot is left to the paddle.
    shot.tracked = true;
    if(!TIMER_ONLY && scale.msSinceLastPacket() > STREAM_STALE_MS){
      Serial.println("no recent weight from the scale: shot left to the paddle");
      shot.tracked = false;
    }
    if(!TIMER_ONLY && scale.unitsOunces()){
      Serial.println("scale is set to ounces: shot left to the paddle");
      shot.tracked = false;
    }
    if(!shot.tracked){
      setColor(MAGENTA);
      return;
    }
    if (CAN_TARE_START_TIMER && AUTOTARE) {
      scale.tareStartTimer();
    } else {
      // Tare first: it is the command that matters for brew-by-weight.
      // The library spaces the commands and verifies the tare.
      if(AUTOTARE){
        scale.tare();
      }
      scale.resetTimer();
      scale.startTimer();
    }
    lastTareState = TARE_IDLE; // so the next TARE_CONFIRMED is seen as a transition
    Serial.println("Weight Timer End");
  }else{
    Serial.print("ShotEnded by ");
    switch (shot.end) {
      case ENDTYPE::TIME:
        Serial.println("time");
        break;
      case ENDTYPE::WEIGHT:
        Serial.println("weight");
        break;
      case ENDTYPE::BUTTON:
        Serial.println("button");
        break;
      case ENDTYPE::DISCONNECT:
        Serial.println("disconnect");
        break;
      case ENDTYPE::UNDEF:
        Serial.println("undef");
        break;
    }

    shot.end_s = seconds_f() - shot.start_timestamp_s;
    scale.stopTimer();
    shot.learn = shot.tracked && !shot.disturbed
              && (!AUTOTARE || scale.tareState() == TARE_CONFIRMED);
    Serial.print("shot summary: ");
    Serial.print(shot.end_s);
    Serial.print(" s, peak flow ");
    Serial.print(shot.peakFlow);
    Serial.print(" g/s");
    if(!shot.tracked){
      Serial.print(", not tracked");
    }
    if(shot.disturbed){
      Serial.print(", disturbed readings ignored");
    }
    Serial.println();
    flowCharacteristic.writeValue(0.0f);
    if(PADDLE_INTERLOCK){
      // Open the interlock for an automatic stop. A scale lost during a
      // weighed shot also stops it (STOP_ON_SCALE_LOSS): nothing else would
      // before INTERLOCK_MAX_BREW_S. A shot that was never weighed is left
      // to the paddle.
      bool lostScale = STOP_ON_SCALE_LOSS && ENDTYPE::DISCONNECT == shot.end && shot.tracked && newButtonState;
      if(!TIMER_ONLY && (ENDTYPE::WEIGHT == shot.end || ENDTYPE::TIME == shot.end || lostScale)){
        setInterlock(true);
        Serial.println("interlock open, waiting for paddle off");
      }
    }
    else if(!TIMER_ONLY 
    && MOMENTARY 
    && (ENDTYPE::WEIGHT == shot.end || ENDTYPE::TIME == shot.end)){
      //Pulse button to stop brewing
      digitalWrite(OUT,HIGH);Serial.println("wrote high");
      delay(300);
      digitalWrite(OUT,LOW);Serial.println("wrote low");
      buttonPressed = false;
    }else if(!TIMER_ONLY && !MOMENTARY){
      buttonLatched = false;
      buttonPressed = false;
      Serial.println("Button Unlatched and not pressed");
      digitalWrite(OUT,LOW); Serial.println("wrote low");
    }
  } 

  // Reset
  shot.end = ENDTYPE::UNDEF;
}
void calculateEndTime(Shot* s){
  
  // Do not predict anything until there are enough measurements
  if(s->datapoints < N){
    s->expected_end_s = MAX_SHOT_DURATION_S;
    return;
  }

  //Get line of best fit (y=mx+b) from the last 10 measurements 
  float sumXY = 0, sumX = 0, sumY = 0, sumSquaredX = 0, m = 0, b = 0, meanX = 0, meanY = 0;

  for(int i = s->datapoints - N; i < s->datapoints; i++){
    sumXY+=s->time_s[i]*s->weight[i];
    sumX+=s->time_s[i];
    sumY+=s->weight[i];
    sumSquaredX += ( s->time_s[i] * s->time_s[i] );
  }

  float den = N*sumSquaredX-(sumX*sumX);
  if(den <= 0){
    s->expected_end_s = MAX_SHOT_DURATION_S;
    return;
  }
  m = (N*sumXY-sumX*sumY) / den;
  meanX = sumX/N;
  meanY = sumY/N;
  b = meanY-m*meanX;

  // The slope is the flow rate.
  s->flow = m;
  if(m > s->peakFlow){
    s->peakFlow = m;
  }

  // Too little espresso yet for a useful prediction
  if(s->weight[s->datapoints-1] < 10){
    s->expected_end_s = MAX_SHOT_DURATION_S;
    return;
  }

  //Calculate time at which goal weight will be reached (x = (y-b)/m)
  // if M is negative (which can happen during a blooming shot when the flow stops) assume max duration (issue #29)
  s->expected_end_s = (m <= 0) ? MAX_SHOT_DURATION_S : (goalWeight - weightOffset - b)/m;
}

// A reading is accepted into the shot only if it is consistent with the
// previous accepted one: espresso doesn't arrive in big jumps, faster than
// MAX_FLOW_GPS, or make the cup lighter.
bool plausibleReading(Shot* s, float w, float t){
  if(isnan(s->lastWeight)){
    return w > -MAX_DROP_G;
  }
  float dw = w - s->lastWeight;
  float dt = t - s->lastTime;
  if(fabs(dw) > MAX_STEP_G || dw < -MAX_DROP_G){
    return false;
  }
  if(dt >= 0.05 && dw / dt > MAX_FLOW_GPS){
    return false;
  }
  return true;
}

float seconds_f(){
  return millis()/1000.0;
}

void setColor(int rgb[3]){
  analogWrite(LED_RED,   255-rgb[0] );
  analogWrite(LED_GREEN, 255-rgb[1] );
  analogWrite(LED_BLUE,  255-rgb[2] );
  currentColor[0] = rgb[0];
  currentColor[1] = rgb[1];
  currentColor[2] = rgb[2];
}

// Sample the brew switch every BUTTON_READ_PERIOD_MS. Runs whether or not
// the scale is connected, so the paddle interlock can always be released.
void readButton(){
  // Read button every period
  // Subtract, don't compare sums: this stays right when millis() wraps (49.7 days).
  if(millis() - lastButtonRead_ms >= BUTTON_READ_PERIOD_MS){
    lastButtonRead_ms = millis();

    //push back for new entry
    for(int i = BUTTON_STATE_ARRAY_LENGTH - 2;i>=0;i--){
      buttonArr[i+1] = buttonArr[i];
    }
    buttonArr[0] = !digitalRead(in); //Active Low

    //only return 1 if contains 1
    // Also assume the button is off for a few milliseconds
    // after the shot is done, there can be residual noise
    // from the reed switch
    // Interlock mode uses a short window so a quick off-on of the paddle
    // still starts the next shot.
    int window = PADDLE_INTERLOCK ? (PADDLE_DEBOUNCE_MS / BUTTON_READ_PERIOD_MS) : BUTTON_STATE_ARRAY_LENGTH;
    newButtonState = 0;
    for(int i=0; i<window; i++){
      if(buttonArr[i]){
        newButtonState = 1;          
      }
      //Serial.print(buttonArr[i]);
    }
    //Serial.println();

    //The reed switch measurements require a small amount of delay for accuracy.
    //  if the shot just stopped, assume that the reed switch should read "open" for the first 0.5s
    if(REEDSWITCH && !shot.brewing && seconds_f() < (shot.start_timestamp_s + shot.end_s + 1)){
      //Serial.println("force reedSwitch Off");
      newButtonState = 0;
    }
  }
}

void setInterlock(bool open){
  digitalWrite(STOP, open ? HIGH : LOW);
  interlockOpen = open;
  savedInterlock = open ? INTERLOCK_HELD : 0;
}

void startWatchdog(){
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t cfg = { .timeout_ms = WATCHDOG_S * 1000, .idle_core_mask = 0, .trigger_panic = true };
  if(esp_task_wdt_reconfigure(&cfg) != ESP_OK){
    esp_task_wdt_init(&cfg);
  }
#else
  esp_task_wdt_init(WATCHDOG_S, true);
#endif
  esp_task_wdt_add(NULL);
}

// Close the interlock as soon as the paddle reads off (debounced), so the
// next paddle-on always reaches the machine.
// Also stop any brew that has had the paddle on for INTERLOCK_MAX_BREW_S.
// This needs only the paddle, so it works without a scale.
void paddleInterlock_task(){
  if(!PADDLE_INTERLOCK){
    return;
  }
  if(newButtonState && !paddleWasOn){
    paddleOnSince_ms = millis();
  }
  paddleWasOn = newButtonState;

  if(interlockOpen && !newButtonState){
    setInterlock(false);
    Serial.println("paddle off, interlock closed");
  }
  else if(INTERLOCK_MAX_BREW_S > 0 && !TIMER_ONLY && newButtonState && !interlockOpen
  && millis() - paddleOnSince_ms > INTERLOCK_MAX_BREW_S * 1000UL){
    Serial.println("paddle on too long: stopping the brew");
    if(shot.brewing && scale.isConnected()){
      shot.brewing = false;
      shot.end = ENDTYPE::TIME;
      setBrewingState(false);   // opens the interlock
    }else{
      setInterlock(true);
    }
  }
}

// Paddle events in PADDLE_INTERLOCK mode: the paddle position is the brew state
// unless the interlock is holding the shot stopped.
void paddleInterlock_events(){
  if(newButtonState && !buttonPressed){
    Serial.println("Paddle on");
    buttonPressed = true;
    if(!interlockOpen){
      shot.brewing = true;
      setBrewingState(shot.brewing);
    }
  }
  else if(!newButtonState && buttonPressed){
    Serial.println("Paddle off");
    buttonPressed = false;
    if(shot.brewing){
      shot.brewing = false;
      shot.end = ENDTYPE::BUTTON;
      setBrewingState(shot.brewing);
    }
  }
}

// Our own BLE peripheral (the "shotStopper" service): goal weight and OTA requests.
void peripheral_task(){
  BLE.poll();
  if (weightCharacteristic.written()) {
    Serial.print("goal weight updated from ");
    Serial.print(goalWeight);
    Serial.print(" to ");
    goalWeight = weightCharacteristic.value();
    Serial.println(goalWeight);
    EEPROM.write(WEIGHT_ADDR, goalWeight); //1 byte, 0-255
    EEPROM.commit();
  }
  if (otaCharacteristic.written()) {
    otaWindowEnd_ms = otaCharacteristic.value() ? millis() + OTA_REQUEST_WINDOW_S * 1000UL : 0;
    Serial.println(otaCharacteristic.value() ? "OTA window requested" : "OTA window closed");
  }
}

// Report scale battery, units and timer problems once, when they change.
void scaleHealth_task(){
  int battery = scale.batteryPercent();
  if(battery != lastBattery){
    lastBattery = battery;
    batteryCharacteristic.writeValue(battery < 0 ? 255 : battery);
    if(battery >= 0){
      Serial.print("scale battery ");
      Serial.print(battery);
      Serial.println(battery <= LOW_BATTERY_PCT ? "% (low: expect dropouts)" : "%");
    }
  }
  if(scale.unitsOunces() && !unitWarned){
    unitWarned = true;
    Serial.println("scale is set to ounces: set it to grams for brew-by-weight");
  }
  timer_state ts = scale.timerState();
  if(ts != lastTimerState){
    lastTimerState = ts;
    if(ts == TIMER_FAILED){
      Serial.println("scale timer did not start (display only, brew-by-weight unaffected)");
    }else if(ts == TIMER_UNVERIFIED){
      Serial.println("scale does not report its timer: timer start not verified");
    }
  }
}

// Over-the-air updates. WiFi shares the radio with BLE, so it is only on
// while nothing is happening: no shot, interlock closed, paddle off, and
// inside the OTA window (after boot, or requested over BLE).
void ota_task(){
#if OTA_ENABLED
  static bool wifiOn = false;
  static bool otaReady = false;
  bool idle = !shot.brewing && !interlockOpen && !newButtonState;
  bool want = idle && (long)(otaWindowEnd_ms - millis()) > 0;
  if(want && !wifiOn){
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(OTA_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiOn = true;
    otaReady = false;
    Serial.println("WiFi on for OTA");
  }else if(!want && wifiOn){
    if(otaReady){
      ArduinoOTA.end();
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiOn = false;
    otaReady = false;
    Serial.println("WiFi off");
  }
  if(wifiOn && !otaReady && WiFi.status() == WL_CONNECTED){
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([](){
      // The paddle is off and the interlock closed (idle), so the machine
      // stays under manual control while the board is being reflashed.
      Serial.println("OTA update starting");
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total){
      if(PADDLE_INTERLOCK){
        esp_task_wdt_reset(); // flashing blocks loop() for longer than WATCHDOG_S
      }
    });
    ArduinoOTA.onEnd([](){ Serial.println("OTA update done, rebooting"); });
    ArduinoOTA.onError([](ota_error_t e){ Serial.print("OTA error "); Serial.println((int)e); });
    ArduinoOTA.begin();
    otaReady = true;
    Serial.print("OTA ready: ");
    Serial.print(OTA_HOSTNAME);
    Serial.print(".local / ");
    Serial.println(WiFi.localIP().toString().c_str());
  }
  if(otaReady){
    ArduinoOTA.handle();
  }
#endif
}

void scaleConnected_task(){

  // Send a heartbeat message to the scale periodically to maintain connection
  if(scale.heartbeatRequired()){
    scale.heartbeat();
  }

  scaleHealth_task();

  // Advance the shot timer in real time, not only when a weight packet
  // arrives. Otherwise a stalled weight stream freezes the timer and neither
  // the predicted end time nor MAX_SHOT_DURATION_S can end the shot.
  if(shot.brewing){
    shot.shotTimer = seconds_f() - shot.start_timestamp_s;
  }

  // always call newWeightAvailable to actually receive the datapoint from the scale,
  // otherwise getWeight() will return stale data
  if(scale.newWeightAvailable()){
    currentWeight = scale.getWeight();

    Serial.print(currentWeight);

    if(!shot.brewing){
      int battery = scale.batteryPercent();
      setColor((battery >= 0 && battery <= LOW_BATTERY_PCT) ? CYAN : GREEN);
    }

    // Readings taken before the tare took effect include the cup; drop them
    // so they don't skew the trend line.
    tare_state tareNow = scale.tareState();
    if(shot.brewing && AUTOTARE && tareNow == TARE_CONFIRMED && lastTareState != TARE_CONFIRMED){
      shot.datapoints = 0;
      shot.lastWeight = NAN;
      shot.disturbed = false;
    }
    if(tareNow == TARE_FAILED && lastTareState != TARE_FAILED){
      Serial.println("tare failed: brew-by-weight disabled for this shot");
    }
    lastTareState = tareNow;

    // update shot trajectory
    float t = seconds_f()-shot.start_timestamp_s;
    if(shot.brewing && shot.tracked && !TIMER_ONLY && shot.datapoints < MAX_DATAPOINTS){
      if(plausibleReading(&shot, currentWeight, t)){
        shot.time_s[shot.datapoints] = t;
        shot.weight[shot.datapoints] = currentWeight;
        shot.datapoints++;
        shot.lastWeight = currentWeight;
        shot.lastTime = t;

        //get the likely end time and the flow rate
        calculateEndTime(&shot);
        flowCharacteristic.writeValue(shot.flow);
      }else{
        if(!shot.disturbed){
          Serial.print(" (implausible reading ignored)");
        }
        shot.disturbed = true;
      }

      Serial.print(" ");
      Serial.print(shot.shotTimer);
      Serial.print(" ");
      Serial.print(shot.expected_end_s);
      Serial.print(" flow ");
      Serial.print(shot.flow);
      if(scale.hasScaleFlowRate()){
        Serial.print(" scale flow ");
        Serial.print(scale.scaleFlowRate());
      }
    }
    Serial.println();
  }

  // SHOT INITIATION EVENTS --------------------------------
  
  if(PADDLE_INTERLOCK){
    paddleInterlock_events();
  }

  //button just pressed (and released)
  else if(newButtonState && buttonPressed == false ){
    Serial.println("ButtonPressed");
    buttonPressed = true;
    if(!MOMENTARY || REEDSWITCH){
      shot.brewing = true;
      setBrewingState(shot.brewing);
    }
  }
    
  // button held. Take over for the rest of the shot.
  else if(!TIMER_ONLY
  && !MOMENTARY 
  && shot.brewing 
  && shot.tracked
  && !buttonLatched 
  && (shot.shotTimer > MIN_SHOT_DURATION_S) 
  ){
    buttonLatched = true;
    Serial.println("Button Latched");
    digitalWrite(OUT,HIGH); Serial.println("wrote high");
    // Get the scale to beep to inform user.
    if(AUTOTARE){
      scale.beep();
    }
  }

  // SHOT COMPLETION EVENTS --------------------------------

  //button released
  else if(!buttonLatched 
  && !newButtonState 
  && buttonPressed == true 
  ){
    Serial.println("Button Released");
    buttonPressed = false;
    shot.brewing = !shot.brewing;
    if(!shot.brewing){
      shot.end = ENDTYPE::BUTTON;
    }
    setBrewingState(shot.brewing);
  }
    
  //Max duration reached
  if(!TIMER_ONLY 
  && shot.brewing 
  && shot.tracked
  && shot.shotTimer > MAX_SHOT_DURATION_S ){
    shot.brewing = false;
    Serial.println("Max brew duration reached");
    shot.end = ENDTYPE::TIME;
    setBrewingState(shot.brewing);
  }

  //Blink LED while brewing
  if(shot.brewing){
    setColor( (millis()/1000)%2 ? (shot.tracked ? GREEN : MAGENTA) : BLUE );
  }

  //End shot
  // Only stop by weight once the tare is confirmed: with the cup still on the
  // scale the trend line predicts an end time in the past and would stop the
  // shot as soon as MIN_SHOT_DURATION_S is reached.
  if(!TIMER_ONLY
  && shot.brewing
  && shot.tracked
  && (!AUTOTARE || scale.tareState() == TARE_CONFIRMED)
  && shot.shotTimer >= shot.expected_end_s
  && shot.shotTimer >  MIN_SHOT_DURATION_S
  ){
    Serial.println("weight achieved");
    shot.brewing = false;
    shot.end = ENDTYPE::WEIGHT;
    setBrewingState(shot.brewing); 
  }

  // SHOT ANALYSIS  --------------------------------

  //Detect error of shot
  if(!TIMER_ONLY
  && shot.start_timestamp_s
  && shot.end_s
  && shot.learn
  && currentWeight >= (goalWeight - weightOffset)
  && seconds_f() > shot.start_timestamp_s + shot.end_s + DRIP_DELAY_S){
    shot.start_timestamp_s = 0;
    shot.end_s = 0;

    Serial.print("I detected a final weight of ");
    Serial.print(currentWeight);
    Serial.print("g. The goal was ");
    Serial.print(goalWeight);
    Serial.print("g with a negative offset of ");
    Serial.print(weightOffset);

    if( abs(currentWeight - goalWeight + weightOffset) > MAX_OFFSET ){
      Serial.print("g. Error assumed. Offset unchanged. ");
    }
    else{
      Serial.print("g. Next time I'll create an offset of ");
      weightOffset += currentWeight - goalWeight;
      Serial.print(weightOffset);

      EEPROM.write(OFFSET_ADDR, weightOffset*10); //1 byte, 0-255
      EEPROM.commit();
    }
    Serial.println();
  }
}