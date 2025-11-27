# Build Configuration Flags

Compile-time flags to enable/disable features and reduce firmware size.

## Available Flags

| Flag | Description | Dependencies |
|------|-------------|--------------|
| `WIFI_NTP_ENABLED` | WiFi connectivity + NTP time sync + Provisioning | - |
| `ETHERNET_ENABLED` | Ethernet connectivity (W5500 SPI) | - |
| `HTTP_ENABLED` | HTTP server + REST API | `WIFI_NTP_ENABLED` |
| `WEBSOCKET_ENABLED` | WebSocket real-time push updates | `HTTP_ENABLED` |
| `CERT_GENERATION_ENABLED` | Per-device SSL certificate generation | `HTTP_ENABLED` |
| `OTA_ENABLED` | Over-the-air firmware updates | `HTTP_ENABLED` |
| `MQTT_ENABLED` | MQTT client for telemetry/control | `WIFI_NTP_ENABLED` |
| `CAN_ENABLED` | CAN bus communication (TWAI) | - |
| `DEBUG_CONSOLE_ENABLED` | USB serial debug console | - |

## Configuration

### platformio.ini

```ini
[env:esp32]
build_flags =
    -DESP32_BUILD
    -DDEBUG_CONSOLE_ENABLED
    ; -DWIFI_NTP_ENABLED
    ; -DHTTP_ENABLED
    ; -DMQTT_ENABLED
```

### Minimal Build (No Network)

```ini
build_flags =
    -DESP32_BUILD
    -DDEBUG_CONSOLE_ENABLED
```

Features: MODBUS sensors, PID control, fermentation plans, debug console.

### WiFi + NTP Only (No HTTP Server)

```ini
build_flags =
    -DESP32_BUILD
    -DDEBUG_CONSOLE_ENABLED
    -DWIFI_NTP_ENABLED
```

Features: All above + WiFi connectivity, NTP time sync, provisioning.
Use this for basic network connectivity without running an HTTP server.

### Full Build (Network + HTTP + MQTT)

```ini
build_flags =
    -DESP32_BUILD
    -DDEBUG_CONSOLE_ENABLED
    -DWIFI_NTP_ENABLED
    -DHTTP_ENABLED
    -DMQTT_ENABLED
```

Features: All above + HTTP server, REST API, MQTT telemetry.

## Module Dependencies

```
WIFI_NTP_ENABLED
├── WiFi connectivity
├── WiFi provisioning (SmartConfig + Captive Portal)
├── NTP time sync
└── Credentials storage (NVS)

HTTP_ENABLED (requires WIFI_NTP_ENABLED)
├── HTTP server (ESP-IDF httpd)
├── REST API endpoints
├── Admin web interface (SPIFFS)
├── First-boot password provisioning
├── Rate limiting and session management
└── HTTPS with self-signed certificates

WEBSOCKET_ENABLED (requires HTTP_ENABLED)
├── WebSocket server on /ws endpoint
├── Session token authentication
├── Real-time push updates (sensors, relays, alarms)
├── Up to 4 concurrent clients
└── ~20KB RAM per client (includes TLS buffers)

OTA_ENABLED (requires HTTP_ENABLED)
├── Dual OTA partitions (app0/app1, 4MB each)
├── Upload firmware via REST API
├── Download firmware from URL (GitHub pull)
├── Automatic rollback on boot failure
├── System pause during update
└── See docs/OTA_UPDATES.md

MQTT_ENABLED (requires WIFI_NTP_ENABLED)
├── MQTT client
├── Telemetry publishing
└── Remote control subscription

DEBUG_CONSOLE_ENABLED
├── USB Serial/JTAG interface
├── Command parser
├── WiFi/NVS management commands
└── System diagnostics

CAN_ENABLED
├── TWAI driver (CAN 2.0B)
├── CAN module for send/receive
└── 125k/250k/500k/1Mbps bitrates

ETHERNET_ENABLED
├── W5500 SPI driver
├── ESP-NETIF integration
├── DHCP client
└── GPIO12-16, GPIO39
```

## Conditional Compilation

### In Source Code

```cpp
#ifdef WIFI_NTP_ENABLED
#include "modules/wifi_module.h"
#include "modules/ntp_module.h"
#include "modules/wifi_provisioning.h"
// WiFi and NTP code
#endif

#ifdef HTTP_ENABLED
#include "modules/rest_api.h"
#include "modules/http_server.h"
// HTTP server code
#endif

#ifdef WEBSOCKET_ENABLED
#include "modules/websocket_manager.h"
// WebSocket real-time updates
#endif

#ifdef OTA_ENABLED
#include "modules/ota_manager.h"
// OTA firmware updates
#endif

#ifdef MQTT_ENABLED
#include "modules/mqtt_client.h"
// MQTT code
#endif

#ifdef DEBUG_CONSOLE_ENABLED
#include "modules/debug_console.h"
// Debug console code
#endif

#ifdef CAN_ENABLED
#include "modules/can_module.h"
#include "hal/esp32/esp32_can.h"
// CAN bus code
#endif

#ifdef ETHERNET_ENABLED
#include "hal/esp32/esp32_ethernet.h"
// Ethernet code (W5500 SPI)
#endif
```

### REST API

The REST API module is **only compiled when `HTTP_ENABLED` is defined**. This requires `WIFI_NTP_ENABLED` for network connectivity.

## Implementation Status

| Flag | Status |
|------|--------|
| `DEBUG_CONSOLE_ENABLED` | Implemented |
| `WIFI_NTP_ENABLED` | Implemented |
| `ETHERNET_ENABLED` | Implemented (W5500 SPI, DHCP) |
| `CAN_ENABLED` | Implemented |
| `HTTP_ENABLED` | Implemented (REST API + admin web interface) |
| `WEBSOCKET_ENABLED` | Implemented (real-time push updates) |
| `OTA_ENABLED` | Implemented (upload, GitHub pull, rollback) |
| `MQTT_ENABLED` | Not implemented |

### WiFi/NTP Implementation Details

When `WIFI_NTP_ENABLED` is defined, the following modules are available:

- **WiFi Module** (`wifi_module.h`) - Connection manager with auto-reconnect
- **NTP Module** (`ntp_module.h`) - Time synchronization with timezone support
- **WiFi Provisioning** (`wifi_provisioning.h`) - SmartConfig + Captive Portal setup
- **ESP32 WiFi HAL** (`esp32_wifi.h`) - ESP-IDF WiFi implementation

See [WIFI_NTP.md](WIFI_NTP.md) for detailed usage.

## Future Considerations

### Display Driver

Not included as a compile flag. The planned Waveshare ESP32 display is a separate device that will consume the REST API over WiFi, not a directly-driven display peripheral.

## Resource Impact

Estimated flash usage per feature:

| Feature | Flash Impact |
|---------|-------------|
| Base system | ~280 KB |
| WiFi + HTTP | ~150-200 KB |
| MQTT | ~50-80 KB |
| Debug console | ~15-20 KB |

ESP32-S3 has 1 MB flash available, leaving headroom for all features.
