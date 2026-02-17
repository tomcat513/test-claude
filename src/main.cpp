#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <time.h>
#include "config.h"
#include "network_monitor.h"
#include "web_server.h"
#include "led_status.h"

// --- Global State ---
Config g_config;
volatile bool g_trigger_ping = false;
volatile bool g_trigger_speed_test = false;

// --- Timing ---
static unsigned long last_ping_time = 0;
static unsigned long last_speed_test_time = 0;
static unsigned long last_data_save_time = 0;
static unsigned long last_ntp_sync_time = 0;
static unsigned long last_wifi_check_time = 0;

static const unsigned long DATA_SAVE_INTERVAL = 300000;   // Save data every 5 minutes
static const unsigned long NTP_SYNC_INTERVAL = 3600000;   // Re-sync NTP every hour
static const unsigned long WIFI_CHECK_INTERVAL = 10000;   // Check WiFi every 10 seconds

// --- WiFi Connection ---
static bool wifi_connecting = false;
static uint8_t wifi_retry_count = 0;
static const uint8_t MAX_WIFI_RETRIES = 20;

static void wifi_connect() {
    if (strlen(g_config.wifi_ssid) == 0) {
        Serial.println("ERROR: No WiFi SSID configured!");
        Serial.println("Configure via serial or flash a config.json to LittleFS");
        return;
    }

    Serial.printf("Connecting to WiFi: %s\n", g_config.wifi_ssid);
    led_set_state(LED_BLINK_FAST);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_config.wifi_ssid, g_config.wifi_password);

    wifi_connecting = true;
    wifi_retry_count = 0;
}

static void wifi_check() {
    if (WiFi.isConnected()) {
        if (wifi_connecting) {
            wifi_connecting = false;
            Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
            Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
        }
        return;
    }

    // Not connected
    if (!wifi_connecting) {
        Serial.println("WiFi disconnected, reconnecting...");
        wifi_connect();
        return;
    }

    // Still trying to connect
    wifi_retry_count++;
    if (wifi_retry_count > MAX_WIFI_RETRIES) {
        Serial.println("WiFi connection failed after max retries, retrying...");
        WiFi.disconnect();
        delay(1000);
        wifi_connect();
    }
}

// --- NTP Sync ---
static void ntp_sync() {
    Serial.println("Syncing NTP time...");
    configTime(g_config.timezone_offset_hours * 3600, 0,
               "pool.ntp.org", "time.nist.gov", "time.google.com");

    // Wait up to 10 seconds for time to be set
    time_t now = 0;
    int attempts = 0;
    while (now < 1000000 && attempts < 20) {
        delay(500);
        time(&now);
        attempts++;
    }

    if (now > 1000000) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        Serial.printf("NTP synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                       timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.println("NTP sync failed - will use uptime timestamps");
    }
}

// --- Serial Configuration (fallback when no WiFi configured) ---
static void check_serial_config() {
    if (Serial.available() <= 0) return;

    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("ssid=")) {
        strlcpy(g_config.wifi_ssid, line.substring(5).c_str(), sizeof(g_config.wifi_ssid));
        Serial.printf("SSID set to: %s\n", g_config.wifi_ssid);
    } else if (line.startsWith("pass=")) {
        strlcpy(g_config.wifi_password, line.substring(5).c_str(), sizeof(g_config.wifi_password));
        Serial.println("WiFi password set");
    } else if (line == "save") {
        config_save(g_config);
        Serial.println("Config saved. Restarting...");
        delay(500);
        ESP.restart();
    } else if (line == "status") {
        Serial.printf("WiFi: %s (%s)\n",
                       WiFi.isConnected() ? "Connected" : "Disconnected",
                       g_config.wifi_ssid);
        if (WiFi.isConnected()) {
            Serial.printf("IP: %s  RSSI: %d dBm\n",
                           WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
        Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    } else if (line == "help") {
        Serial.println("Commands:");
        Serial.println("  ssid=<name>  - Set WiFi SSID");
        Serial.println("  pass=<pwd>   - Set WiFi password");
        Serial.println("  save         - Save config and restart");
        Serial.println("  status       - Show current status");
        Serial.println("  help         - Show this help");
    }
}

// --- Watchdog ---
static hw_timer_t* watchdog_timer = NULL;

static void IRAM_ATTR watchdog_isr() {
    ESP.restart();
}

static void watchdog_init() {
    watchdog_timer = timerBegin(0, 80, true);  // 1 MHz
    timerAttachInterrupt(watchdog_timer, &watchdog_isr, true);
    timerAlarmWrite(watchdog_timer, 60000000, false);  // 60 second timeout
    timerAlarmEnable(watchdog_timer);
}

static void watchdog_feed() {
    if (watchdog_timer) {
        timerWrite(watchdog_timer, 0);
    }
}

// --- Setup ---
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("================================");
    Serial.println("  WiFi Network Monitor v1.0");
    Serial.println("================================");
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());

    // Initialize LED
    led_init();
    led_set_state(LED_BLINK_FAST);

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {  // true = format on fail
        Serial.println("ERROR: LittleFS mount failed!");
        led_set_state(LED_OFF);
        return;
    }
    Serial.println("LittleFS mounted");

    // Load or initialize config
    if (!config_load(g_config)) {
        Serial.println("No saved config, using defaults");
        config_set_defaults(g_config);
        config_save(g_config);
    } else {
        Serial.println("Config loaded from flash");
    }

    // Print config summary
    Serial.printf("Device: %s\n", g_config.device_name);
    Serial.printf("WiFi SSID: %s\n", g_config.wifi_ssid);
    Serial.printf("Ping interval: %u sec\n", g_config.ping_interval_sec);
    Serial.printf("Speed test interval: %u sec\n", g_config.speed_test_interval_sec);

    // Initialize network monitor (load saved data)
    monitor_init();

    // Connect to WiFi
    wifi_connect();

    // Wait for WiFi connection (up to 15 seconds)
    int wait = 0;
    while (!WiFi.isConnected() && wait < 30) {
        delay(500);
        Serial.print(".");
        wait++;
    }
    Serial.println();

    if (WiFi.isConnected()) {
        wifi_connecting = false;
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());

        // Sync NTP
        ntp_sync();
        last_ntp_sync_time = millis();

        // Start web server
        webserver_init();

        led_set_state(LED_SOLID);
    } else {
        Serial.println("WiFi not connected yet - will keep trying");
        Serial.println("Use serial commands to configure: type 'help'");
    }

    // Initialize watchdog
    watchdog_init();

    Serial.println("Setup complete");
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
}

// --- Main Loop ---
void loop() {
    unsigned long now = millis();

    // Feed watchdog
    watchdog_feed();

    // LED animation
    led_loop();

    // Check serial for configuration commands
    check_serial_config();

    // Check WiFi status
    if (now - last_wifi_check_time >= WIFI_CHECK_INTERVAL) {
        last_wifi_check_time = now;
        wifi_check();
    }

    // Only do network operations if WiFi is connected
    if (WiFi.isConnected()) {
        // Re-sync NTP periodically
        if (now - last_ntp_sync_time >= NTP_SYNC_INTERVAL) {
            last_ntp_sync_time = now;
            ntp_sync();
        }

        // Run ping round
        bool do_ping = g_trigger_ping ||
                        (now - last_ping_time >= g_config.ping_interval_sec * 1000UL);
        if (do_ping) {
            last_ping_time = now;
            g_trigger_ping = false;
            monitor_run_ping();
            monitor_update_outage_state();

            // Update LED based on outage state
            NetworkStatus status = monitor_get_status();
            if (!status.internet_reachable || !status.gateway_reachable) {
                led_set_state(LED_BLINK_SLOW);
            } else {
                led_set_state(LED_SOLID);
            }
        }

        // Run speed test
        bool do_speed = g_trigger_speed_test ||
                         (now - last_speed_test_time >= g_config.speed_test_interval_sec * 1000UL);
        if (do_speed) {
            last_speed_test_time = now;
            g_trigger_speed_test = false;
            monitor_run_speed_test();
        }
    } else {
        led_set_state(LED_BLINK_FAST);
    }

    // Periodically save data to flash
    if (now - last_data_save_time >= DATA_SAVE_INTERVAL) {
        last_data_save_time = now;
        monitor_save_data();
    }

    // Web server housekeeping
    webserver_loop();

    // Small delay to prevent tight loop
    delay(10);
}
