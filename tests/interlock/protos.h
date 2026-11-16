struct Shot;
void setup(); void loop();
void setBrewingState(bool brewing); void calculateEndTime(Shot* s); bool plausibleReading(Shot* s, float w, float t);
float seconds_f(); void setColor(int rgb[3]); void readButton(); void setInterlock(bool open); void startWatchdog();
void paddleInterlock_task(); void paddleInterlock_events(); void peripheral_task(); void scaleHealth_task(); void ota_task(); void scaleConnected_task();

float shotElapsed_s(); void paddleTick(void*); void startPaddleTimer();
void status_task();
