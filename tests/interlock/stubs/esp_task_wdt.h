#pragma once
#ifndef ESP_OK
#define ESP_OK 0
#endif
void wdt_start(int seconds); void wdt_feed();
inline int esp_task_wdt_init(int s, bool) { wdt_start(s); return 0; }
inline int esp_task_wdt_add(void*) { return 0; }
inline void esp_task_wdt_reset() { wdt_feed(); }
