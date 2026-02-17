#include "config.h"
#include <LittleFS.h>

static const char* CONFIG_FILE = "/config.json";

void config_set_defaults(Config& cfg) {
    memset(&cfg, 0, sizeof(Config));

    strlcpy(cfg.wifi_ssid, "", sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_password, "", sizeof(cfg.wifi_password));

    strlcpy(cfg.admin_user, "admin", sizeof(cfg.admin_user));
    strlcpy(cfg.admin_pass, "admin", sizeof(cfg.admin_pass));
    cfg.password_changed = false;

    // Default ping targets (gateway added dynamically at runtime)
    strlcpy(cfg.ping_targets[0], "8.8.8.8", MAX_HOST_LEN);
    strlcpy(cfg.ping_targets[1], "1.1.1.1", MAX_HOST_LEN);
    cfg.ping_target_count = 2;
    cfg.ping_interval_sec = 30;
    cfg.ping_count = 3;

    strlcpy(cfg.speed_test_url, "http://speedtest.tele2.net/1MB.zip", sizeof(cfg.speed_test_url));
    cfg.speed_test_interval_sec = 900;  // 15 minutes
    cfg.speed_test_timeout_ms = 30000;  // 30 seconds

    cfg.timezone_offset_hours = 0;
    cfg.data_retention_days = 7;

    strlcpy(cfg.device_name, "NetMonitor", sizeof(cfg.device_name));
}

bool config_load(Config& cfg) {
    if (!LittleFS.exists(CONFIG_FILE)) {
        return false;
    }

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        Serial.printf("Config parse error: %s\n", err.c_str());
        return false;
    }

    // Start with defaults, then overlay saved values
    config_set_defaults(cfg);

    if (doc["wifi_ssid"].is<const char*>())
        strlcpy(cfg.wifi_ssid, doc["wifi_ssid"], sizeof(cfg.wifi_ssid));
    if (doc["wifi_password"].is<const char*>())
        strlcpy(cfg.wifi_password, doc["wifi_password"], sizeof(cfg.wifi_password));

    if (doc["admin_user"].is<const char*>())
        strlcpy(cfg.admin_user, doc["admin_user"], sizeof(cfg.admin_user));
    if (doc["admin_pass"].is<const char*>())
        strlcpy(cfg.admin_pass, doc["admin_pass"], sizeof(cfg.admin_pass));
    if (doc["password_changed"].is<bool>())
        cfg.password_changed = doc["password_changed"];

    if (doc["ping_targets"].is<JsonArray>()) {
        JsonArray targets = doc["ping_targets"];
        cfg.ping_target_count = 0;
        for (size_t i = 0; i < targets.size() && i < MAX_PING_TARGETS; i++) {
            if (targets[i].is<const char*>()) {
                strlcpy(cfg.ping_targets[i], targets[i], MAX_HOST_LEN);
                cfg.ping_target_count++;
            }
        }
    }

    if (doc["ping_interval_sec"].is<uint32_t>())
        cfg.ping_interval_sec = constrain(doc["ping_interval_sec"].as<uint32_t>(), 10, 300);
    if (doc["ping_count"].is<uint8_t>())
        cfg.ping_count = constrain(doc["ping_count"].as<uint8_t>(), 1, 10);

    if (doc["speed_test_url"].is<const char*>())
        strlcpy(cfg.speed_test_url, doc["speed_test_url"], sizeof(cfg.speed_test_url));
    if (doc["speed_test_interval_sec"].is<uint32_t>())
        cfg.speed_test_interval_sec = constrain(doc["speed_test_interval_sec"].as<uint32_t>(), 300, 86400);
    if (doc["speed_test_timeout_ms"].is<uint32_t>())
        cfg.speed_test_timeout_ms = constrain(doc["speed_test_timeout_ms"].as<uint32_t>(), 5000, 60000);

    if (doc["timezone_offset_hours"].is<int8_t>())
        cfg.timezone_offset_hours = constrain(doc["timezone_offset_hours"].as<int8_t>(), -12, 14);
    if (doc["data_retention_days"].is<uint8_t>())
        cfg.data_retention_days = constrain(doc["data_retention_days"].as<uint8_t>(), 1, 30);

    if (doc["device_name"].is<const char*>())
        strlcpy(cfg.device_name, doc["device_name"], sizeof(cfg.device_name));

    return true;
}

bool config_save(const Config& cfg) {
    JsonDocument doc;
    config_to_json(cfg, doc, true);

    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("Failed to open config file for writing");
        return false;
    }

    size_t written = serializeJson(doc, file);
    file.close();
    return written > 0;
}

void config_to_json(const Config& cfg, JsonDocument& doc, bool include_passwords) {
    doc["wifi_ssid"] = cfg.wifi_ssid;
    if (include_passwords) {
        doc["wifi_password"] = cfg.wifi_password;
        doc["admin_pass"] = cfg.admin_pass;
    }
    doc["admin_user"] = cfg.admin_user;
    doc["password_changed"] = cfg.password_changed;

    JsonArray targets = doc["ping_targets"].to<JsonArray>();
    for (uint8_t i = 0; i < cfg.ping_target_count; i++) {
        targets.add(cfg.ping_targets[i]);
    }

    doc["ping_interval_sec"] = cfg.ping_interval_sec;
    doc["ping_count"] = cfg.ping_count;

    doc["speed_test_url"] = cfg.speed_test_url;
    doc["speed_test_interval_sec"] = cfg.speed_test_interval_sec;
    doc["speed_test_timeout_ms"] = cfg.speed_test_timeout_ms;

    doc["timezone_offset_hours"] = cfg.timezone_offset_hours;
    doc["data_retention_days"] = cfg.data_retention_days;
    doc["device_name"] = cfg.device_name;
}

bool config_from_json(Config& cfg, const JsonDocument& doc) {
    bool changed = false;

    if (doc["wifi_ssid"].is<const char*>()) {
        strlcpy(cfg.wifi_ssid, doc["wifi_ssid"], sizeof(cfg.wifi_ssid));
        changed = true;
    }
    if (doc["wifi_password"].is<const char*>()) {
        strlcpy(cfg.wifi_password, doc["wifi_password"], sizeof(cfg.wifi_password));
        changed = true;
    }
    if (doc["admin_user"].is<const char*>()) {
        strlcpy(cfg.admin_user, doc["admin_user"], sizeof(cfg.admin_user));
        changed = true;
    }
    if (doc["admin_pass"].is<const char*>()) {
        strlcpy(cfg.admin_pass, doc["admin_pass"], sizeof(cfg.admin_pass));
        cfg.password_changed = true;
        changed = true;
    }

    if (doc["ping_targets"].is<JsonArray>()) {
        JsonArray targets = doc["ping_targets"].as<JsonArray>();
        cfg.ping_target_count = 0;
        for (size_t i = 0; i < targets.size() && i < MAX_PING_TARGETS; i++) {
            if (targets[i].is<const char*>()) {
                strlcpy(cfg.ping_targets[i], targets[i], MAX_HOST_LEN);
                cfg.ping_target_count++;
            }
        }
        changed = true;
    }

    if (doc["ping_interval_sec"].is<uint32_t>()) {
        cfg.ping_interval_sec = constrain(doc["ping_interval_sec"].as<uint32_t>(), 10, 300);
        changed = true;
    }
    if (doc["ping_count"].is<uint8_t>()) {
        cfg.ping_count = constrain(doc["ping_count"].as<uint8_t>(), 1, 10);
        changed = true;
    }

    if (doc["speed_test_url"].is<const char*>()) {
        strlcpy(cfg.speed_test_url, doc["speed_test_url"], sizeof(cfg.speed_test_url));
        changed = true;
    }
    if (doc["speed_test_interval_sec"].is<uint32_t>()) {
        cfg.speed_test_interval_sec = constrain(doc["speed_test_interval_sec"].as<uint32_t>(), 300, 86400);
        changed = true;
    }

    if (doc["timezone_offset_hours"].is<int8_t>()) {
        cfg.timezone_offset_hours = constrain(doc["timezone_offset_hours"].as<int8_t>(), -12, 14);
        changed = true;
    }
    if (doc["data_retention_days"].is<uint8_t>()) {
        cfg.data_retention_days = constrain(doc["data_retention_days"].as<uint8_t>(), 1, 30);
        changed = true;
    }
    if (doc["device_name"].is<const char*>()) {
        strlcpy(cfg.device_name, doc["device_name"], sizeof(cfg.device_name));
        changed = true;
    }

    return changed;
}
