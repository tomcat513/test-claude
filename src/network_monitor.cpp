#include "network_monitor.h"
#include "config.h"
#include <WiFi.h>
#include <ESP32Ping.h>
#include <HTTPClient.h>
#include <LittleFS.h>

extern Config g_config;

// --- Ring Buffers ---
static PingResult ping_history[MAX_PING_HISTORY];
static uint16_t ping_head = 0;
static uint16_t ping_count = 0;

static SpeedResult speed_history[MAX_SPEED_HISTORY];
static uint16_t speed_head = 0;
static uint16_t speed_count = 0;

static OutageRecord outage_history[MAX_OUTAGE_HISTORY];
static uint16_t outage_head = 0;
static uint16_t outage_count = 0;

// --- Current State ---
static bool internet_was_reachable = true;
static bool gateway_was_reachable = true;
static bool wifi_was_connected = true;
static uint32_t current_outage_start = 0;
static OutageType current_outage_type = OUTAGE_INTERNET;
static bool in_outage = false;

// --- File paths ---
static const char* PING_DATA_FILE = "/data/ping.bin";
static const char* SPEED_DATA_FILE = "/data/speed.bin";
static const char* OUTAGE_DATA_FILE = "/data/outage.bin";
static const char* META_FILE = "/data/meta.json";

// --- Helper: get current unix timestamp ---
static uint32_t get_timestamp() {
    time_t now;
    time(&now);
    return (uint32_t)now;
}

// --- Helper: add ping result to ring buffer ---
static void add_ping_result(const PingResult& result) {
    ping_history[ping_head] = result;
    ping_head = (ping_head + 1) % MAX_PING_HISTORY;
    if (ping_count < MAX_PING_HISTORY) ping_count++;
}

// --- Helper: add speed result to ring buffer ---
static void add_speed_result(const SpeedResult& result) {
    speed_history[speed_head] = result;
    speed_head = (speed_head + 1) % MAX_SPEED_HISTORY;
    if (speed_count < MAX_SPEED_HISTORY) speed_count++;
}

// --- Helper: add outage record ---
static void add_outage_record(const OutageRecord& record) {
    outage_history[outage_head] = record;
    outage_head = (outage_head + 1) % MAX_OUTAGE_HISTORY;
    if (outage_count < MAX_OUTAGE_HISTORY) outage_count++;
}

// --- Helper: end current outage ---
static void end_current_outage() {
    if (!in_outage) return;
    // Find the current outage record and set end time
    uint16_t idx = (outage_head == 0) ? MAX_OUTAGE_HISTORY - 1 : outage_head - 1;
    if (outage_count > 0 && outage_history[idx].end_time == 0) {
        outage_history[idx].end_time = get_timestamp();
    }
    in_outage = false;
    Serial.println("Outage ended");
}

void monitor_init() {
    // Ensure data directory exists
    if (!LittleFS.exists("/data")) {
        LittleFS.mkdir("/data");
    }
    monitor_load_data();
}

void monitor_run_ping() {
    uint32_t ts = get_timestamp();
    bool any_external_success = false;
    bool gateway_success = false;

    // Ping the gateway first
    IPAddress gw = WiFi.gatewayIP();
    if (gw != IPAddress(0, 0, 0, 0)) {
        PingResult result;
        result.timestamp = ts;
        snprintf(result.target, sizeof(result.target), "%s", gw.toString().c_str());
        result.packets_sent = g_config.ping_count;

        bool ok = Ping.ping(gw, g_config.ping_count);
        result.success = ok;
        result.avg_time_ms = ok ? Ping.averageTime() : 0;
        result.packets_received = ok ? g_config.ping_count : 0;

        gateway_success = ok;
        add_ping_result(result);

        Serial.printf("Ping %s: %s (%.1fms)\n", result.target,
                       ok ? "OK" : "FAIL", result.avg_time_ms);
    }

    // Ping configured targets
    for (uint8_t i = 0; i < g_config.ping_target_count; i++) {
        PingResult result;
        result.timestamp = ts;
        strlcpy(result.target, g_config.ping_targets[i], sizeof(result.target));
        result.packets_sent = g_config.ping_count;

        IPAddress target_ip;
        bool resolved = WiFi.hostByName(g_config.ping_targets[i], target_ip);

        if (resolved) {
            bool ok = Ping.ping(target_ip, g_config.ping_count);
            result.success = ok;
            result.avg_time_ms = ok ? Ping.averageTime() : 0;
            result.packets_received = ok ? g_config.ping_count : 0;
            if (ok) any_external_success = true;
        } else {
            result.success = false;
            result.avg_time_ms = 0;
            result.packets_received = 0;
        }

        add_ping_result(result);

        Serial.printf("Ping %s: %s (%.1fms)\n", result.target,
                       result.success ? "OK" : "FAIL", result.avg_time_ms);
    }

    // Update reachability state
    gateway_was_reachable = gateway_success;
    internet_was_reachable = any_external_success;
    wifi_was_connected = WiFi.isConnected();
}

void monitor_run_speed_test() {
    SpeedResult result;
    memset(&result, 0, sizeof(result));
    result.timestamp = get_timestamp();

    if (!WiFi.isConnected()) {
        result.success = false;
        strlcpy(result.error, "WiFi not connected", sizeof(result.error));
        add_speed_result(result);
        return;
    }

    Serial.printf("Starting speed test: %s\n", g_config.speed_test_url);

    HTTPClient http;
    http.setTimeout(g_config.speed_test_timeout_ms);
    http.begin(g_config.speed_test_url);

    unsigned long start = millis();
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        int len = http.getSize();
        WiFiClient* stream = http.getStreamPtr();

        uint32_t total_bytes = 0;
        uint8_t buf[1024];

        while (http.connected() && (len > 0 || len == -1)) {
            size_t avail = stream->available();
            if (avail) {
                int read = stream->readBytes(buf, min(avail, sizeof(buf)));
                total_bytes += read;
                if (len > 0) len -= read;
            }

            // Timeout check
            if (millis() - start > g_config.speed_test_timeout_ms) {
                break;
            }

            yield();
        }

        unsigned long elapsed = millis() - start;
        result.duration_ms = elapsed;
        result.bytes_transferred = total_bytes;

        if (elapsed > 0 && total_bytes > 0) {
            // Convert to Mbps: (bytes * 8) / (ms * 1000) = Mbps
            result.download_mbps = (float)(total_bytes * 8) / (float)(elapsed * 1000);
            result.success = true;
            Serial.printf("Speed test: %.2f Mbps (%u bytes in %lu ms)\n",
                           result.download_mbps, total_bytes, elapsed);
        } else {
            result.success = false;
            strlcpy(result.error, "No data received", sizeof(result.error));
        }
    } else {
        result.success = false;
        snprintf(result.error, sizeof(result.error), "HTTP %d", httpCode);
        Serial.printf("Speed test failed: HTTP %d\n", httpCode);
    }

    http.end();
    add_speed_result(result);
}

void monitor_update_outage_state() {
    if (!WiFi.isConnected()) {
        if (!in_outage || current_outage_type != OUTAGE_WIFI) {
            if (in_outage) end_current_outage();
            OutageRecord rec;
            rec.start_time = get_timestamp();
            rec.end_time = 0;
            rec.type = OUTAGE_WIFI;
            add_outage_record(rec);
            in_outage = true;
            current_outage_type = OUTAGE_WIFI;
            current_outage_start = rec.start_time;
            Serial.println("WiFi outage detected");
        }
    } else if (!gateway_was_reachable) {
        if (!in_outage || current_outage_type != OUTAGE_LOCAL_NETWORK) {
            if (in_outage) end_current_outage();
            OutageRecord rec;
            rec.start_time = get_timestamp();
            rec.end_time = 0;
            rec.type = OUTAGE_LOCAL_NETWORK;
            add_outage_record(rec);
            in_outage = true;
            current_outage_type = OUTAGE_LOCAL_NETWORK;
            current_outage_start = rec.start_time;
            Serial.println("Local network outage detected");
        }
    } else if (!internet_was_reachable) {
        if (!in_outage || current_outage_type != OUTAGE_INTERNET) {
            if (in_outage) end_current_outage();
            OutageRecord rec;
            rec.start_time = get_timestamp();
            rec.end_time = 0;
            rec.type = OUTAGE_INTERNET;
            add_outage_record(rec);
            in_outage = true;
            current_outage_type = OUTAGE_INTERNET;
            current_outage_start = rec.start_time;
            Serial.println("Internet outage detected");
        }
    } else {
        if (in_outage) {
            end_current_outage();
        }
    }
}

NetworkStatus monitor_get_status() {
    NetworkStatus status;
    memset(&status, 0, sizeof(status));

    status.wifi_connected = WiFi.isConnected();
    status.wifi_rssi = WiFi.RSSI();
    status.internet_reachable = internet_was_reachable;
    status.gateway_reachable = gateway_was_reachable;
    status.uptime_sec = millis() / 1000;

    if (status.wifi_connected) {
        strlcpy(status.ip_address, WiFi.localIP().toString().c_str(), sizeof(status.ip_address));
        strlcpy(status.gateway, WiFi.gatewayIP().toString().c_str(), sizeof(status.gateway));
    }

    // Find last ping and speed test times
    if (ping_count > 0) {
        uint16_t idx = (ping_head == 0) ? MAX_PING_HISTORY - 1 : ping_head - 1;
        status.last_ping_time = ping_history[idx].timestamp;
    }
    if (speed_count > 0) {
        uint16_t idx = (speed_head == 0) ? MAX_SPEED_HISTORY - 1 : speed_head - 1;
        status.last_speed_test_time = speed_history[idx].timestamp;
        status.last_download_mbps = speed_history[idx].download_mbps;
    }

    return status;
}

void monitor_get_ping_history(JsonDocument& doc, uint32_t since, uint16_t limit) {
    JsonArray arr = doc["pings"].to<JsonArray>();

    uint16_t count_to_return = min(limit, ping_count);
    uint16_t start_idx;

    if (ping_count < MAX_PING_HISTORY) {
        start_idx = (ping_count > count_to_return) ? ping_count - count_to_return : 0;
    } else {
        start_idx = (ping_head >= count_to_return)
                     ? ping_head - count_to_return
                     : MAX_PING_HISTORY - (count_to_return - ping_head);
    }

    for (uint16_t i = 0; i < count_to_return; i++) {
        uint16_t idx = (start_idx + i) % MAX_PING_HISTORY;
        const PingResult& p = ping_history[idx];

        if (since > 0 && p.timestamp < since) continue;

        JsonObject obj = arr.add<JsonObject>();
        obj["ts"] = p.timestamp;
        obj["target"] = p.target;
        obj["ok"] = p.success;
        obj["ms"] = round(p.avg_time_ms * 10.0f) / 10.0f;
        obj["sent"] = p.packets_sent;
        obj["recv"] = p.packets_received;
    }

    doc["total"] = ping_count;
}

void monitor_get_speed_history(JsonDocument& doc, uint32_t since, uint16_t limit) {
    JsonArray arr = doc["speeds"].to<JsonArray>();

    uint16_t count_to_return = min(limit, speed_count);
    uint16_t start_idx;

    if (speed_count < MAX_SPEED_HISTORY) {
        start_idx = (speed_count > count_to_return) ? speed_count - count_to_return : 0;
    } else {
        start_idx = (speed_head >= count_to_return)
                     ? speed_head - count_to_return
                     : MAX_SPEED_HISTORY - (count_to_return - speed_head);
    }

    for (uint16_t i = 0; i < count_to_return; i++) {
        uint16_t idx = (start_idx + i) % MAX_SPEED_HISTORY;
        const SpeedResult& s = speed_history[idx];

        if (since > 0 && s.timestamp < since) continue;

        JsonObject obj = arr.add<JsonObject>();
        obj["ts"] = s.timestamp;
        obj["mbps"] = round(s.download_mbps * 100.0f) / 100.0f;
        obj["bytes"] = s.bytes_transferred;
        obj["ms"] = s.duration_ms;
        obj["ok"] = s.success;
        if (!s.success && strlen(s.error) > 0) {
            obj["error"] = s.error;
        }
    }

    doc["total"] = speed_count;
}

void monitor_get_outage_history(JsonDocument& doc) {
    JsonArray arr = doc["outages"].to<JsonArray>();

    static const char* outage_type_names[] = {"internet", "local_network", "wifi"};

    uint16_t start_idx;
    if (outage_count < MAX_OUTAGE_HISTORY) {
        start_idx = 0;
    } else {
        start_idx = outage_head;
    }

    uint16_t n = min(outage_count, (uint16_t)MAX_OUTAGE_HISTORY);
    for (uint16_t i = 0; i < n; i++) {
        uint16_t idx = (start_idx + i) % MAX_OUTAGE_HISTORY;
        const OutageRecord& o = outage_history[idx];

        JsonObject obj = arr.add<JsonObject>();
        obj["start"] = o.start_time;
        obj["end"] = o.end_time;
        obj["type"] = outage_type_names[o.type];
        obj["ongoing"] = (o.end_time == 0);
        if (o.end_time > 0) {
            obj["duration_sec"] = o.end_time - o.start_time;
        } else if (o.end_time == 0) {
            obj["duration_sec"] = get_timestamp() - o.start_time;
        }
    }

    doc["total"] = outage_count;
    doc["in_outage"] = in_outage;
}

void monitor_get_target_pings(const char* target, JsonDocument& doc, uint16_t limit) {
    JsonArray arr = doc["pings"].to<JsonArray>();
    uint16_t found = 0;

    // Walk backwards through history to find most recent for this target
    for (uint16_t i = 0; i < ping_count && found < limit; i++) {
        uint16_t idx;
        if (ping_head == 0) {
            idx = MAX_PING_HISTORY - 1 - i;
        } else {
            idx = (ping_head - 1 - i + MAX_PING_HISTORY) % MAX_PING_HISTORY;
        }

        if (strcmp(ping_history[idx].target, target) == 0) {
            const PingResult& p = ping_history[idx];
            JsonObject obj = arr.add<JsonObject>();
            obj["ts"] = p.timestamp;
            obj["ok"] = p.success;
            obj["ms"] = round(p.avg_time_ms * 10.0f) / 10.0f;
            found++;
        }
    }
}

void monitor_save_data() {
    Serial.println("Saving monitor data to flash...");

    // Save ping data
    File f = LittleFS.open(PING_DATA_FILE, "w");
    if (f) {
        f.write((uint8_t*)&ping_head, sizeof(ping_head));
        f.write((uint8_t*)&ping_count, sizeof(ping_count));
        f.write((uint8_t*)ping_history, sizeof(ping_history));
        f.close();
    }

    // Save speed data
    f = LittleFS.open(SPEED_DATA_FILE, "w");
    if (f) {
        f.write((uint8_t*)&speed_head, sizeof(speed_head));
        f.write((uint8_t*)&speed_count, sizeof(speed_count));
        f.write((uint8_t*)speed_history, sizeof(speed_history));
        f.close();
    }

    // Save outage data
    f = LittleFS.open(OUTAGE_DATA_FILE, "w");
    if (f) {
        f.write((uint8_t*)&outage_head, sizeof(outage_head));
        f.write((uint8_t*)&outage_count, sizeof(outage_count));
        f.write((uint8_t*)&in_outage, sizeof(in_outage));
        f.write((uint8_t*)&current_outage_type, sizeof(current_outage_type));
        f.write((uint8_t*)outage_history, sizeof(outage_history));
        f.close();
    }

    Serial.println("Monitor data saved");
}

void monitor_load_data() {
    Serial.println("Loading monitor data from flash...");

    // Load ping data
    if (LittleFS.exists(PING_DATA_FILE)) {
        File f = LittleFS.open(PING_DATA_FILE, "r");
        if (f && f.size() == sizeof(ping_head) + sizeof(ping_count) + sizeof(ping_history)) {
            f.read((uint8_t*)&ping_head, sizeof(ping_head));
            f.read((uint8_t*)&ping_count, sizeof(ping_count));
            f.read((uint8_t*)ping_history, sizeof(ping_history));
            Serial.printf("Loaded %d ping records\n", ping_count);
        }
        if (f) f.close();
    }

    // Load speed data
    if (LittleFS.exists(SPEED_DATA_FILE)) {
        File f = LittleFS.open(SPEED_DATA_FILE, "r");
        if (f && f.size() == sizeof(speed_head) + sizeof(speed_count) + sizeof(speed_history)) {
            f.read((uint8_t*)&speed_head, sizeof(speed_head));
            f.read((uint8_t*)&speed_count, sizeof(speed_count));
            f.read((uint8_t*)speed_history, sizeof(speed_history));
            Serial.printf("Loaded %d speed records\n", speed_count);
        }
        if (f) f.close();
    }

    // Load outage data
    if (LittleFS.exists(OUTAGE_DATA_FILE)) {
        File f = LittleFS.open(OUTAGE_DATA_FILE, "r");
        if (f) {
            f.read((uint8_t*)&outage_head, sizeof(outage_head));
            f.read((uint8_t*)&outage_count, sizeof(outage_count));
            f.read((uint8_t*)&in_outage, sizeof(in_outage));
            f.read((uint8_t*)&current_outage_type, sizeof(current_outage_type));
            f.read((uint8_t*)outage_history, sizeof(outage_history));
            Serial.printf("Loaded %d outage records\n", outage_count);
            f.close();
        }
    }

    // Purge old data based on retention policy
    uint32_t cutoff = get_timestamp() - (g_config.data_retention_days * 86400);
    // For simplicity, we just let old data naturally rotate out via ring buffer
    (void)cutoff;
}
