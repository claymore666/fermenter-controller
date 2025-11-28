# HTTP Admin Interface

Web-based administration interface for the Fermenter Controller, providing full system monitoring and configuration through a Bootstrap 5 dark theme UI.

## Overview

The HTTP Admin interface provides a complete web-based replacement for the debug console, accessible from any device with a web browser. It includes real-time monitoring, configuration, and control capabilities.

## Features

### Status Dashboard
- **System Information**
  - Firmware version and build number
  - Build timestamp and hostname link (mDNS FQDN)
  - Flash usage with progress bar (color-coded: green <70%, yellow <85%, red ≥85%)
  - CPU usage with history graph (30 minutes)
  - Uptime, free heap, sensor/fermenter counts

- **Network Interfaces**
  - **WiFi**: SSID, IP, netmask, gateway, RSSI (color-coded)
  - **Ethernet**: IP, netmask, gateway, link speed
  - Hot Standby badge (cyan) when WiFi is dormant during Ethernet failover
  - Network bandwidth history graph (30 minutes, samples active interface)

- **Connectivity**
  - NTP sync status
  - System time (live)
  - Timezone

- **Bus Status**
  - MODBUS RTU: Transactions, errors, error rate (color-coded)
  - CAN Bus: TX/RX counts, errors, bus state badge

- **Digital Inputs**
  - DI1-DI8 status (HIGH/LOW badges)

- **Compiled Modules**
  - WiFi, NTP, HTTP, MQTT, CAN, Debug Console (ON/OFF status)

- **Relay Control**
  - All relays with live toggle buttons
  - Instant on/off control

- **Alarms**
  - Active alarm display
  - Temperature/pressure/sensor failure indicators

### Sensor Configuration
Table-based interface with:
- Sensor name
- MODBUS address (device:register format)
- Current value with unit and quality badge
- Type selector dropdown:
  - Pressure 0-1.6 bar
  - Pressure 0-4 bar
  - PT1000 Temperature
  - PT100 Temperature
  - Voltage 0-10V
  - Current 4-20mA
- Edit/Save/Cancel actions per sensor

### Configuration View
- JSON display of system configuration
- Timing parameters
- Hardware settings

### Authentication & Security
- **First-boot password setup**: Device requires password creation on initial setup (no default password)
- **Password requirements**: 8+ characters with 2 of: uppercase, lowercase, digit
- **Session-based authentication** with 1-hour timeout and cryptographic tokens
- **Rate limiting**: Exponential backoff after failed attempts (1s, 2s, 4s...), lockout after 10 failures (5 min)
- **Constant-time password comparison**: Prevents timing attacks
- **Password hashing**: SHA-256 with salt (plaintext never stored)
- **HTTPS support**: Self-signed certificates (default shared cert, optional per-device generation)

## Architecture

```
┌─────────────────────────────────────────┐
│         Web Browser (Client)            │
│     Bootstrap 5 Dark Theme UI           │
└──────────────┬──────────────────────────┘
               │ HTTP/REST API
               │ (2-second polling)
┌──────────────▼──────────────────────────┐
│         ESP32 HTTP Server               │
│   include/modules/http_server.h         │
├─────────────────────────────────────────┤
│  Routes:                                │
│  GET  /api/health         (no auth)     │
│  POST /api/setup          (first boot)  │
│  POST /api/login                        │
│  POST /api/logout                       │
│  GET  /api/dashboard      (consolidated)│
│  GET  /api/cpu/history                  │
│  GET  /api/network/history              │
│  GET  /api/status                       │
│  GET  /api/sensors                      │
│  POST /api/sensor/{name}/config         │
│  GET  /api/relays                       │
│  POST /api/relay/{name}                 │
│  GET  /api/inputs                       │
│  GET  /api/outputs                      │
│  POST /api/output/{id}                  │
│  GET  /api/modbus/stats                 │
│  GET  /api/can/status                   │
│  GET  /api/config                       │
│  GET  /api/modules                      │
│  POST /api/password                     │
│  POST /api/factory_reset                │
│  POST /api/reboot                       │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│         Core System                     │
│  StateManager, EventBus, Config         │
│  MODBUS, PID, Safety, etc.              │
└─────────────────────────────────────────┘
```

## Files

### Web UI
- `web/admin/index.html` - Single-page application (SPA)
  - Bootstrap 5.3.2 dark theme
  - Bootstrap Icons 1.11.1
  - Vanilla JavaScript (no frameworks)
  - ~200KB total (HTML + inline CSS/JS)

### Backend
- `include/modules/http_server.h` - HTTP server module
  - REST API handlers
  - Session management
  - JSON response builders

### Testing
- `test/test_web_server/test_web_server.cpp` - Mock server for UI development
- `test/test_web_server/httplib.h` - cpp-httplib (header-only HTTP library)
- `test/test_web_server/build.sh` - Build script

## Mock Server for Testing

The mock server allows UI development and testing without ESP32 hardware.

### Features
- Simulates 8 fermenters with realistic data
- Temperature drift toward setpoint
- Live sensor noise simulation
- All REST API endpoints functional
- Runs on localhost:8080

### Building
```bash
cd test/test_web_server
./build.sh
```

### Running
```bash
./web_server
```
Then open: http://localhost:8080/admin/index.html
Password: `admin`

### Mock Data
```cpp
SimState {
    fermenters[8][4]      // temp, setpoint, pressure, pid_output
    relays[16]            // relay states
    inputs[8]             // digital input states
    outputs[8]            // digital output states
    modbus_transactions   // incrementing counter
    modbus_errors         // error count
}
```

### Simulated Behavior
- Temperature drifts toward setpoint at 1% per cycle
- Pressure changes slowly with noise
- PID output calculated from error
- Inputs toggle randomly for visual feedback
- MODBUS transaction counter increments

## REST API Reference

### Authentication

#### POST /api/login
Login and obtain session token.

**Request:**
```json
{
  "password": "admin"
}
```

**Response (200):**
```json
{
  "success": true,
  "token": "sess_12345678_abcdef"
}
```

**Response (401):**
```json
{
  "error": "Invalid password"
}
```

#### POST /api/logout
Invalidate current session.

**Headers:** `Authorization: Bearer <token>`

**Response (200):**
```json
{
  "success": true
}
```

### System Status

#### GET /api/health
Check device provisioning status (no authentication required).

**Response (200):**
```json
{
  "status": "ok",
  "provisioned": true
}
```

#### GET /api/dashboard
Get all dashboard data in a single request (consolidated endpoint for efficiency).

**Headers:** `Authorization: Bearer <token>`

**Response (200):**
```json
{
  "status": {
    "version": "0.1.0",
    "build": "o1zevz",
    "built": "Nov 26 2025 14:30:00",
    "hostname": "fermenter-230778.local",
    "uptime": "2h 15m",
    "uptime_seconds": 8100,
    "free_heap": 245000,
    "ntp_synced": true,
    "sensor_count": 2,
    "fermenter_count": 1,
    "system_time": "2025-11-26 17:30:45",
    "timezone": "CET-1CEST",
    "cpu_usage": 0.2,
    "cpu_freq_mhz": 80,
    "cpu_freq_max_mhz": 240,
    "flash_used": 1015808,
    "flash_total": 16777216
  },
  "network": {
    "wifi": {
      "connected": false,
      "standby": true,
      "ip": "192.168.0.138",
      "netmask": "255.255.255.0",
      "gateway": "192.168.0.1",
      "ssid": "Braustube",
      "rssi": -65
    },
    "ethernet": {
      "connected": true,
      "ip": "192.168.0.139",
      "netmask": "255.255.255.0",
      "gateway": "192.168.0.1",
      "speed": 100
    }
  },
  "modbus": {
    "transactions": 1250,
    "errors": 3,
    "error_rate": 0.24
  },
  "can": {
    "tx": 15,
    "rx": 42,
    "errors": 0,
    "state": "OK"
  },
  "sensors": [...],
  "relays": [...],
  "inputs": [...],
  "outputs": [...],
  "alarms": [],
  "config": {...},
  "modules": {...}
}
```

#### GET /api/cpu/history
Get CPU usage history for graphing (30 minutes, 15-second intervals).

**Headers:** `Authorization: Bearer <token>`

**Response (200):**
```json
{
  "samples": [0, 1, 0, 2, 1, 0, 0, 1, ...],
  "count": 120,
  "interval_ms": 15000
}
```

#### GET /api/network/history
Get network bandwidth utilization history for graphing.

**Headers:** `Authorization: Bearer <token>`

**Response (200):**
```json
{
  "samples": [0, 1, 2, 1, 0, ...],
  "count": 120,
  "interval_ms": 15000,
  "link_speed_mbps": 100,
  "channel": 0
}
```

Note: `channel` is 0 for Ethernet, 1-13 for WiFi.

#### GET /api/status
Get complete system status.

**Headers:** `Authorization: Bearer <token>`

**Response (200):**
```json
{
  "version": "1.0.0",
  "build": "251124",
  "built": "Nov 24 2025 14:30:00",
  "uptime": "2h 15m 30s",
  "uptime_seconds": 8130,
  "free_heap": 245000,
  "wifi_rssi": -65,
  "ntp_synced": true,
  "sensor_count": 18,
  "fermenter_count": 8,
  "modbus_transactions": 1250,
  "modbus_errors": 3,
  "system_time": "2025-11-24 17:30:45",
  "timezone": "UTC",
  "flash_used": 1015808,
  "flash_total": 4194304
}
```

### Sensors

#### GET /api/sensors
Get all sensors with configuration.

**Response (200):**
```json
{
  "sensors": [
    {
      "name": "fermenter_1_temp",
      "modbus_addr": "2:0",
      "value": 18.2,
      "unit": "C",
      "quality": "GOOD",
      "type": "pt1000"
    }
  ]
}
```

#### GET /api/sensor/{name}
Get detailed sensor information.

**Response (200):**
```json
{
  "name": "fermenter_1_temp",
  "raw_value": 18.15,
  "filtered_value": 18.2,
  "display_value": 18.2,
  "unit": "C",
  "quality": "GOOD",
  "filter_type": 2,
  "alpha": 0.30,
  "scale": 0.1,
  "timestamp": 5000
}
```

#### POST /api/sensor/{name}/config
Update sensor configuration.

**Request:**
```json
{
  "type": "pt1000"
}
```

**Response (200):**
```json
{
  "success": true,
  "sensor": "fermenter_1_temp",
  "message": "Configuration saved"
}
```

### Relays

#### GET /api/relays
Get all relay states.

**Response (200):**
```json
{
  "relays": [
    {
      "name": "f1_cooling",
      "state": true,
      "last_change": 1000
    }
  ]
}
```

#### POST /api/relay/{name}
Set relay state.

**Request:**
```json
{
  "state": true
}
```

**Response (200):**
```json
{
  "success": true,
  "relay": "f1_cooling",
  "state": true
}
```

### GPIO

#### GET /api/inputs
Get digital input states.

**Response (200):**
```json
{
  "inputs": [
    {"id": 1, "state": false},
    {"id": 2, "state": true}
  ]
}
```

#### GET /api/outputs
Get digital output states.

**Response (200):**
```json
{
  "outputs": [
    {"id": 1, "state": false},
    {"id": 2, "state": true}
  ]
}
```

#### POST /api/output/{id}
Set digital output state.

**Request:**
```json
{
  "state": true
}
```

**Response (200):**
```json
{
  "success": true,
  "output": 1,
  "state": true
}
```

### WebSocket

#### GET /ws
Establish a WebSocket connection for real-time updates. Requires authentication via session token.

**Connection:**
```
wss://192.168.0.142/ws
```

**Authentication (required within 5 seconds):**
```json
{"type": "auth", "token": "your_session_token"}
```

**Response (success):**
```json
{"type": "auth_ok"}
```

**Response (failure):**
```json
{"type": "error", "message": "Invalid token"}
```

**Message Types (Server → Client):**

| Type | Description |
|------|-------------|
| `auth_ok` | Authentication successful |
| `full` | Full state snapshot (sent after auth and periodically) |
| `sensor` | Sensor value change (significant change only) |
| `relay` | Relay state change |
| `alarm` | Alarm notification |
| `ping` | Keep-alive (30s interval) |
| `error` | Error message |

**Full State Message:**
```json
{
  "type": "full",
  "sensors": [
    {"id": 0, "value": 18.5, "quality": "GOOD"}
  ],
  "relays": [
    {"id": 0, "state": true}
  ]
}
```

**Sensor Update Message:**
```json
{
  "type": "sensor",
  "id": 0,
  "value": 18.7,
  "quality": "GOOD"
}
```

**Relay Update Message:**
```json
{
  "type": "relay",
  "id": 0,
  "state": false
}
```

**Alarm Message:**
```json
{
  "type": "alarm",
  "fermenter": 1,
  "active": true
}
```

**Connection Limits:**
- Maximum concurrent clients: 4
- Authentication timeout: 5 seconds
- Idle timeout: 60 seconds (with 30s ping keep-alive)
- Maximum frame size: 2048 bytes

**Memory Usage:**
- ~20KB per client (includes TLS buffers)
- ~80KB total for 4 concurrent clients

**Example (JavaScript):**
```javascript
const ws = new WebSocket('wss://192.168.0.142/ws');
ws.onopen = () => {
    ws.send(JSON.stringify({type: 'auth', token: sessionToken}));
};
ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    if (msg.type === 'sensor') {
        console.log(`Sensor ${msg.id}: ${msg.value}`);
    }
};
```

### Bus Status

#### GET /api/modbus/stats
Get MODBUS statistics.

**Response (200):**
```json
{
  "transactions": 1250,
  "errors": 3,
  "error_rate": 0.24
}
```

#### GET /api/can/status
Get CAN bus status.

**Response (200):**
```json
{
  "tx": 15,
  "rx": 42,
  "errors": 0,
  "state": "OK",
  "bitrate": 500000
}
```

### System

#### GET /api/config
Get system configuration.

**Response (200):**
```json
{
  "fermenter_count": 8,
  "modbus_device_count": 4,
  "gpio_relay_count": 8,
  "timing": {
    "modbus_poll_ms": 1000,
    "pid_interval_ms": 5000,
    "safety_check_ms": 1000
  }
}
```

#### GET /api/modules
Get compiled module status.

**Response (200):**
```json
{
  "modules": {
    "wifi": true,
    "ntp": true,
    "http": true,
    "mqtt": false,
    "can": true,
    "debug_console": true
  }
}
```

#### POST /api/reboot
Reboot the controller.

**Response (200):**
```json
{
  "success": true,
  "message": "Rebooting..."
}
```

## ESP32 Integration

### Requirements
- `HTTP_ENABLED` build flag in platformio.ini
- ESP-IDF HTTP server component
- Minimum 100KB free heap recommended
- WiFi or Ethernet connectivity

### Initialization
```cpp
#include "modules/http_server.h"

modules::HttpServer http_server(
    &time, &state_manager, &event_bus,
    &config, &safety, &plans, &modbus,
    &storage, &gpio
);

#ifdef WIFI_NTP_ENABLED
http_server.set_wifi_provisioning(&wifi_prov);
#endif

#ifdef CAN_ENABLED
http_server.set_can_module(&can);
#endif

// Set custom admin password from NVS
if (storage.exists("admin:password")) {
    http_server.set_admin_password(storage.get("admin:password"));
}

// Start server (implementation needed)
// http_server.start(80);
```

### Password Management
```bash
# Via debug console
nvs set admin:password mynewpassword
nvs get admin:password
reboot
```

### Static File Serving
Options for hosting the HTML/CSS/JS:

1. **SPIFFS/LittleFS** (Internal Flash)
   - 3MB available
   - Faster access
   - Requires reflashing to update UI

2. **SD Card** (External)
   - GB available
   - Easy UI updates (swap card)
   - Slower access
   - User can remove card

Recommended: Use SD card for easy UI iteration during development.

## Security Considerations

### Current Implementation
- [x] Session-based authentication with 1-hour timeout (cryptographic tokens)
- [x] First-boot password setup (no hardcoded defaults)
- [x] Password requirements: 8+ chars, 2 character categories
- [x] HTTPS with self-signed certificates
- [x] Rate limiting with exponential backoff (prevents brute force)
- [x] Constant-time password comparison (prevents timing attacks)
- [x] Password hashing with SHA-256 + salt (not plaintext)
- [x] Path traversal protection (rejects `..` in URLs)
- [x] CORS restricted (no wildcard `*`)
- [x] Single concurrent session
- **Security Headers** (Implemented):
  - Content-Security-Policy (CSP) to prevent XSS attacks
  - X-Content-Type-Options: nosniff to prevent MIME sniffing
  - X-Frame-Options: DENY to prevent clickjacking
  - X-XSS-Protection: 1; mode=block for legacy browsers
  - crossorigin="anonymous" on external resources
- **Input Sanitization** (Implemented):
  - HTML escaping utility function for all user-controlled strings
  - Validation of sensor types against whitelist
  - URL encoding for API endpoint parameters
  - Numeric validation for output IDs and values
  - Boolean coercion for relay/output states
- **Output Encoding** (Implemented):
  - All dynamic content uses escapeHtml() before rendering
  - textContent used for automatic escaping where possible
  - Sensor names, relay names, and module names sanitized
  - Error messages properly escaped before display

### Production Recommendations
- [x] Enable HTTPS with self-signed certificate
- [x] Add brute-force protection (rate limiting on login)
- [x] Store password hash (not plaintext)
- [x] Add rate limiting on API endpoints
- [ ] Add role-based access control (RBAC) for multi-user access
- [ ] Add CSRF protection tokens for state-changing operations
- [ ] Implement WebSocket authentication token rotation
- [ ] Implement audit logging for security events
- [ ] Add session management (force logout, list active sessions)
- [ ] Per-device certificate generation (requires enabling CONFIG_MBEDTLS_X509_CRT_WRITE_C)

## Performance

### Resource Usage
- Web UI size: ~200KB (HTML + inline JS/CSS)
- HTTP server: ~15KB RAM overhead
- Session storage: ~100 bytes per session
- Request handling: ~2KB stack per request

### Network Traffic
- Initial page load: ~200KB
- Polling interval: 2 seconds (adaptive with backoff)
- Per-poll request: ~50 bytes
- Per-poll response: ~2KB (consolidated dashboard endpoint)
- Bandwidth: ~1KB/s continuous

## Connection Resilience

The Admin UI includes automatic reconnection and failover recovery.

### Exponential Backoff

When API requests fail, the UI progressively slows down retries to avoid flooding the device:

| Failure Count | Retry Interval | Cumulative Time |
|---------------|----------------|-----------------|
| 0 (success) | 2s | - |
| 1 | 2s | 2s |
| 2 | 3s | 5s |
| 3 | 4.5s | 9.5s |
| 4 | 6.75s | 16.25s |
| 5+ | 10s (max) | 26.25s → reload |

### Auto-Reload for Failover

After 5 consecutive failures (~26 seconds), the page automatically reloads. This handles:

- **Network failover** (WiFi ↔ Ethernet) where the device IP changes
- **mDNS hostname resolution** which browsers may cache
- **TLS session expiration** after extended disconnection

The status badge shows reconnection progress:
- 🟢 **Connected** - Normal operation
- 🟡 **Reconnecting (N)...** - Attempting to reconnect (N = failure count)
- 🔴 **Reloading...** - Page will reload to refresh DNS

### TLS Session Management

The ESP32 has limited TLS session capacity. The Admin UI:

1. Uses a **consolidated `/api/dashboard` endpoint** to minimize requests
2. Staggers initial requests on page load (1s delay between graph fetches)
3. Maintains a single polling loop to avoid concurrent TLS handshakes

### Recommendations
- Limit to 2-3 concurrent clients maximum
- Consider WebSocket upgrade for real-time updates (reduces polling overhead)
- Implement gzip compression for responses
- Cache static files in browser

## Troubleshooting

### UI Not Loading
1. Check WiFi connection
2. Verify HTTP server is running: `curl http://<ip>/api/health`
3. Check browser console for errors
4. Verify static files are accessible

### Authentication Failing
1. Check password: `nvs get admin:password` (debug console)
2. Verify session timeout hasn't expired
3. Clear browser localStorage
4. Check for CORS issues (if accessing from different origin)

### Data Not Updating
1. Verify auto-refresh is enabled (2-second polling)
2. Check API endpoints return valid JSON
3. Verify state manager is being updated
4. Check browser network tab for failed requests

### High CPU/Memory Usage
1. Reduce polling frequency (increase from 2s to 5s)
2. Limit number of concurrent clients
3. Disable debug logging
4. Check for memory leaks in request handlers

## Future Enhancements

### Phase 2
- [x] WebSocket support for push updates (implemented)
- [ ] Historical data graphs (Chart.js integration)
- [ ] Data export (CSV download)
- [ ] Firmware update via web interface
- [ ] Mobile app support (responsive design already included)

### Phase 3
- [ ] Recipe database and management
- [ ] Brewfather API integration
- [ ] Email/SMS alarm notifications
- [ ] Multi-language support
- [ ] Camera integration for visual monitoring

## Related Documentation

- [BUILD_FLAGS.md](BUILD_FLAGS.md) - HTTP_ENABLED flag configuration
- [DEBUG_CONSOLE.md](DEBUG_CONSOLE.md) - Serial console (mirrored functionality)
- [WIFI_NTP.md](WIFI_NTP.md) - Network connectivity requirements
- [WEB_GUI.md](WEB_GUI.md) - Original GUI implementation plan (Preact/React option)
