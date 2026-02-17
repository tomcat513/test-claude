#ifndef NETWORK_MONITOR_H
#define NETWORK_MONITOR_H

#include <Arduino.h>
#include <ArduinoJson.h>

// --- Ping Result ---
struct PingResult {
    uint32_t timestamp;       // Unix epoch
    char target[64];
    bool success;
    float avg_time_ms;
    uint8_t packets_sent;
    uint8_t packets_received;
};

// --- Speed Test Result ---
struct SpeedResult {
    uint32_t timestamp;
    float download_mbps;
    uint32_t bytes_transferred;
    uint32_t duration_ms;
    bool success;
    char error[64];
};

// --- Outage Record ---
enum OutageType {
    OUTAGE_INTERNET = 0,
    OUTAGE_LOCAL_NETWORK = 1,
    OUTAGE_WIFI = 2
};

struct OutageRecord {
    uint32_t start_time;
    uint32_t end_time;       // 0 if ongoing
    OutageType type;
};

// --- Network Status ---
struct NetworkStatus {
    bool wifi_connected;
    int32_t wifi_rssi;
    bool internet_reachable;
    bool gateway_reachable;
    uint32_t last_ping_time;
    uint32_t last_speed_test_time;
    uint32_t uptime_sec;
    float last_download_mbps;
    char ip_address[16];
    char gateway[16];
};

// Ring buffer sizes (in-memory, backed by flash periodically)
#define MAX_PING_HISTORY 2880      // 24h at 30s intervals
#define MAX_SPEED_HISTORY 672      // 7 days at 15min intervals
#define MAX_OUTAGE_HISTORY 200

// Initialize the network monitor
void monitor_init();

// Run a single ping round (all targets)
void monitor_run_ping();

// Run a speed test
void monitor_run_speed_test();

// Update outage detection logic
void monitor_update_outage_state();

// Get current network status
NetworkStatus monitor_get_status();

// Get ping history as JSON
void monitor_get_ping_history(JsonDocument& doc, uint32_t since = 0, uint16_t limit = 100);

// Get speed test history as JSON
void monitor_get_speed_history(JsonDocument& doc, uint32_t since = 0, uint16_t limit = 50);

// Get outage history as JSON
void monitor_get_outage_history(JsonDocument& doc);

// Save current data to flash
void monitor_save_data();

// Load data from flash
void monitor_load_data();

// Get last N ping results for a specific target
void monitor_get_target_pings(const char* target, JsonDocument& doc, uint16_t limit = 60);

#endif // NETWORK_MONITOR_H
