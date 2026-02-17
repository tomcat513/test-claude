# WiFi Network Monitor - Requirements Specification

## Overview

A microcontroller-based device that monitors local network health from a user's
perspective. It connects to WiFi and continuously tests connectivity, latency,
and throughput, logging results and alerting when issues are detected. It is
controlled and monitored through a web browser.

## Hardware

- **Microcontroller**: ESP32 (ESP-WROOM-32 or ESP32-DevKitC)
  - Dual-core Xtensa LX6 @ 240 MHz
  - 520 KB SRAM
  - 4 MB flash (minimum)
  - 802.11 b/g/n WiFi
  - Onboard LED for status indication
- **Power**: USB (5V) or external 5V supply
- **No additional hardware required** (no SD card, no external sensors)

## Functional Requirements

### FR-1: WiFi Connectivity
- Connect to a configured WiFi network (WPA2)
- Reconnect automatically on disconnection
- Report WiFi signal strength (RSSI) periodically

### FR-2: Ping Monitoring
- Ping configurable list of targets at configurable intervals
- Default targets: local gateway, 8.8.8.8 (Google DNS), 1.1.1.1 (Cloudflare DNS)
- Record: success/failure, round-trip time (ms), packet loss percentage
- Default interval: 30 seconds
- Configurable interval: 10s - 300s

### FR-3: Internet Outage Detection
- Detect internet outage when all external ping targets fail
- Detect local network outage when gateway ping fails
- Log outage start/end times with duration
- Distinguish between: internet down, local network down, WiFi down

### FR-4: Speed Testing
- Perform HTTP download speed test at configurable intervals
- Default interval: every 15 minutes
- Download a configurable test file URL (default: a small file from a CDN)
- Record: download speed (Mbps), test duration, bytes transferred
- Optional upload test via HTTP POST to a configurable endpoint
- Configurable interval: 5min - 24hr

### FR-5: Data Logging
- Store results in flash filesystem (LittleFS)
- Retain up to 7 days of data (auto-purge oldest)
- Log entries: timestamp, event type, measurements
- Survive device reboots (persistent storage)

### FR-6: Web Interface
- Serve a responsive web dashboard over HTTPS
- Display current status: WiFi strength, last ping results, connection state
- Display charts: latency over time, speed test results over time
- Display outage log with timestamps and durations
- Configuration page for all settings

### FR-7: Remote Control / API
- RESTful JSON API for all operations
- Endpoints:
  - GET /api/status - current device and network status
  - GET /api/ping/history - ping history with pagination
  - GET /api/speed/history - speed test history
  - GET /api/outages - outage log
  - GET /api/config - current configuration
  - POST /api/config - update configuration
  - POST /api/ping/now - trigger immediate ping
  - POST /api/speed/now - trigger immediate speed test
  - POST /api/restart - reboot device
- All endpoints require authentication

### FR-8: Security
- HTTPS with self-signed certificate (generated on first boot)
- Basic HTTP authentication (username + password)
- Default credentials: admin / admin (force change on first login)
- Rate limiting on authentication attempts (5 failures = 60s lockout)

### FR-9: NTP Time Synchronization
- Sync with NTP servers on boot and periodically
- Use UTC internally, display in configurable timezone
- Fallback to millis() uptime if NTP unavailable

## Non-Functional Requirements

### NFR-1: Reliability
- Watchdog timer to recover from hangs
- Automatic WiFi reconnection
- Graceful handling of flash storage full (purge oldest data)

### NFR-2: Performance
- Web UI response time < 500ms
- Ping operations must not block web server
- Use async/non-blocking I/O where possible

### NFR-3: Resource Constraints
- Flash usage: < 2 MB for firmware + web assets
- RAM usage: < 200 KB at peak (leave headroom for WiFi stack)
- LittleFS partition: ~1.5 MB for data storage

## Configuration Defaults

| Setting              | Default Value                                    |
|----------------------|--------------------------------------------------|
| WiFi SSID            | (must be configured)                             |
| WiFi Password        | (must be configured)                             |
| Admin Username       | admin                                            |
| Admin Password       | admin                                            |
| Ping Targets         | gateway, 8.8.8.8, 1.1.1.1                       |
| Ping Interval        | 30 seconds                                       |
| Speed Test URL       | http://speedtest.tele2.net/1MB.zip               |
| Speed Test Interval  | 900 seconds (15 min)                             |
| Timezone Offset      | 0 (UTC)                                          |
| Data Retention       | 7 days                                           |

## LED Status Indicators

- **Solid**: Connected, all systems normal
- **Slow blink (1Hz)**: Connected, internet outage detected
- **Fast blink (4Hz)**: Connecting to WiFi
- **Off**: Device in error state or booting
