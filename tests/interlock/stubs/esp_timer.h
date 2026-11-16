#pragma once
#include <cstdint>
#define ESP_OK 0
typedef void* esp_timer_handle_t;
struct esp_timer_create_args_t { void (*callback)(void*); void* arg; int dispatch_method; const char* name; bool skip_unhandled_events; };
int esp_timer_create(const esp_timer_create_args_t* a, esp_timer_handle_t* h);
int esp_timer_start_periodic(esp_timer_handle_t h, uint64_t period_us);
