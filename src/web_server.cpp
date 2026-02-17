#include "web_server.h"
#include "config.h"
#include "network_monitor.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

extern Config g_config;
extern volatile bool g_trigger_ping;
extern volatile bool g_trigger_speed_test;

// --- Self-signed certificate for HTTPS ---
// In production, generate a proper self-signed cert on first boot.
// For simplicity, we use HTTP here with an option to enable HTTPS.
// ESP32 AsyncWebServer HTTPS support is limited; we'll use HTTP
// and rely on network-level security (local network only).
static AsyncWebServer server(80);

// --- Rate limiting ---
#define MAX_AUTH_FAILURES 5
#define LOCKOUT_DURATION_MS 60000
static uint8_t auth_failures = 0;
static unsigned long lockout_until = 0;

// --- Authentication check ---
static bool check_auth(AsyncWebServerRequest* request) {
    // Check lockout
    if (auth_failures >= MAX_AUTH_FAILURES && millis() < lockout_until) {
        request->send(429, "application/json", "{\"error\":\"Too many attempts. Try again later.\"}");
        return false;
    }

    if (!request->authenticate(g_config.admin_user, g_config.admin_pass)) {
        auth_failures++;
        if (auth_failures >= MAX_AUTH_FAILURES) {
            lockout_until = millis() + LOCKOUT_DURATION_MS;
        }
        request->requestAuthentication();
        return false;
    }

    auth_failures = 0;
    return true;
}

// --- CORS headers ---
static void add_cors_headers(AsyncWebServerResponse* response) {
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

// --- API Handlers ---

static void handle_status(AsyncWebServerRequest* request) {
    if (!check_auth(request)) return;

    NetworkStatus status = monitor_get_status();

    JsonDocument doc;
    doc["wifi_connected"] = status.wifi_connected;
    doc["wifi_rssi"] = status.wifi_rssi;
    doc["internet_reachable"] = status.internet_reachable;
    doc["gateway_reachable"] = status.gateway_reachable;
    doc["last_ping_time"] = status.last_ping_time;
    doc["last_speed_test_time"] = status.last_speed_test_time;
    doc["uptime_sec"] = status.uptime_sec;
    doc["last_download_mbps"] = status.last_download_mbps;
    doc["ip_address"] = status.ip_address;
    doc["gateway"] = status.gateway;
    doc["device_name"] = g_config.device_name;
    doc["password_changed"] = g_config.password_changed;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["wifi_ssid"] = g_config.wifi_ssid;

    // WiFi quality label
    int rssi = status.wifi_rssi;
    if (rssi > -50) doc["wifi_quality"] = "Excellent";
    else if (rssi > -60) doc["wifi_quality"] = "Good";
    else if (rssi > -70) doc["wifi_quality"] = "Fair";
    else doc["wifi_quality"] = "Weak";

    String response;
    serializeJson(doc, response);
    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", response);
    add_cors_headers(resp);
    request->send(resp);
}

static void handle_ping_history(AsyncWebServerRequest* request) {
    if (!check_auth(request)) return;

    uint32_t since = 0;
    uint16_t limit = 200;
    if (request->hasParam("since")) {
        since = request->getParam("since")->value().toInt();
    }
    if (request->hasParam("limit")) {
        limit = request->getParam("limit")->value().toInt();
    }

    JsonDocument doc;
    monitor_get_ping_history(doc, since, limit);

    String response;
    serializeJson(doc, response);
    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", response);
    add_cors_headers(resp);
    request->send(resp);
}

static void handle_speed_history(AsyncWebServerRequest* request) {
    if (!check_auth(request)) return;

    uint32_t since = 0;
    uint16_t limit = 100;
    if (request->hasParam("since")) {
        since = request->getParam("since")->value().toInt();
    }
    if (request->hasParam("limit")) {
        limit = request->getParam("limit")->value().toInt();
    }

    JsonDocument doc;
    monitor_get_speed_history(doc, since, limit);

    String response;
    serializeJson(doc, response);
    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", response);
    add_cors_headers(resp);
    request->send(resp);
}

static void handle_outages(AsyncWebServerRequest* request) {
    if (!check_auth(request)) return;

    JsonDocument doc;
    monitor_get_outage_history(doc);

    String response;
    serializeJson(doc, response);
    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", response);
    add_cors_headers(resp);
    request->send(resp);
}

static void handle_get_config(AsyncWebServerRequest* request) {
    if (!check_auth(request)) return;

    JsonDocument doc;
    config_to_json(g_config, doc, false);  // Don't include passwords in response

    String response;
    serializeJson(doc, response);
    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", response);
    add_cors_headers(resp);
    request->send(resp);
}

static void handle_post_config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!check_auth(request)) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, (const char*)data, len);

    if (err) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    bool changed = config_from_json(g_config, doc);

    if (changed) {
        config_save(g_config);
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Configuration saved\"}");
    } else {
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"No changes\"}");
    }
}

static void handle_ping_now(AsyncWebServerRequest* request) {
    if (!check_auth(request)) return;
    g_trigger_ping = true;
    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Ping triggered\"}");
}

static void handle_speed_now(AsyncWebServerRequest* request) {
    if (!check_auth(request)) return;
    g_trigger_speed_test = true;
    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Speed test triggered\"}");
}

static void handle_restart(AsyncWebServerRequest* request) {
    if (!check_auth(request)) return;
    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Restarting...\"}");
    delay(500);
    ESP.restart();
}

// --- Serve static files from LittleFS ---
static void serve_index(AsyncWebServerRequest* request) {
    if (!check_auth(request)) return;
    request->send(LittleFS, "/www/index.html", "text/html");
}

void webserver_init() {
    // Serve the web UI
    server.on("/", HTTP_GET, serve_index);

    // Serve static assets from LittleFS /www/
    server.serveStatic("/css/", LittleFS, "/www/css/");
    server.serveStatic("/js/", LittleFS, "/www/js/");
    server.serveStatic("/favicon.ico", LittleFS, "/www/favicon.ico");

    // API endpoints
    server.on("/api/status", HTTP_GET, handle_status);
    server.on("/api/ping/history", HTTP_GET, handle_ping_history);
    server.on("/api/speed/history", HTTP_GET, handle_speed_history);
    server.on("/api/outages", HTTP_GET, handle_outages);
    server.on("/api/config", HTTP_GET, handle_get_config);
    server.on("/api/ping/now", HTTP_POST, handle_ping_now);
    server.on("/api/speed/now", HTTP_POST, handle_speed_now);
    server.on("/api/restart", HTTP_POST, handle_restart);

    // POST config with body
    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},  // Main handler (unused for body)
        NULL,  // Upload handler
        handle_post_config  // Body handler
    );

    // CORS preflight
    server.on("/api/*", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response = request->beginResponse(204);
        add_cors_headers(response);
        request->send(response);
    });

    // 404 handler
    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "application/json", "{\"error\":\"Not found\"}");
    });

    server.begin();
    Serial.println("Web server started on port 80");
}

void webserver_loop() {
    // Reset lockout after duration expires
    if (auth_failures >= MAX_AUTH_FAILURES && millis() >= lockout_until) {
        auth_failures = 0;
        Serial.println("Auth lockout expired");
    }
}
