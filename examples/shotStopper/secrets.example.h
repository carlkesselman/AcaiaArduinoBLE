// Copy to secrets.h (which git ignores) and fill in, then set OTA_ENABLED true.
#define WIFI_SSID     "your-wifi-network"
#define WIFI_PASSWORD "your-wifi-password"
#define OTA_PASSWORD  "choose-an-upload-password"
#define OTA_HOSTNAME  "shotstopper"   // reachable as shotstopper.local
// Time zone for the dates in the event log (POSIX TZ string). Default is UTC.
// #define LOG_TZ "PST8PDT,M3.2.0,M11.1.0"      // US Pacific
// #define LOG_TZ "EST5EDT,M3.2.0,M11.1.0"      // US Eastern
// #define LOG_TZ "CET-1CEST,M3.5.0,M10.5.0/3"  // Central Europe
