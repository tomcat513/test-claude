#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Maximum configurable ping targets
#define MAX_PING_TARGETS 8
// Maximum hostname/IP length
#define MAX_HOST_LEN 64

struct Config {
    // WiFi
    char wifi_ssid[33];
    char wifi_password[65];

    // Authentication
    char admin_user[32];
    char admin_pass[64];
    bool password_changed;

    // Ping settings
    char ping_targets[MAX_PING_TARGETS][MAX_HOST_LEN];
    uint8_t ping_target_count;
    uint32_t ping_interval_sec;   // seconds between ping rounds
    uint8_t ping_count;           // pings per target per round

    // Speed test settings
    char speed_test_url[256];
    uint32_t speed_test_interval_sec;
    uint32_t speed_test_timeout_ms;

    // General
    int8_t timezone_offset_hours;
    uint8_t data_retention_days;

    // Device
    char device_name[32];
};

// Default configuration values
void config_set_defaults(Config& cfg);

// Load config from LittleFS, returns false if no saved config
bool config_load(Config& cfg);

// Save config to LittleFS
bool config_save(const Config& cfg);

// Serialize config to JSON (excludes sensitive fields optionally)
void config_to_json(const Config& cfg, JsonDocument& doc, bool include_passwords = false);

// Deserialize JSON into config (partial update)
bool config_from_json(Config& cfg, const JsonDocument& doc);

#endif // CONFIG_H
