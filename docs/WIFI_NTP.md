# WiFi & NTP Module Documentation

WiFi connectivity, NTP time synchronization, and provisioning for the fermentation controller.

## Overview

The WiFi system consists of four main components:

| Module | File | Description |
|--------|------|-------------|
| WiFi HAL | `hal/esp32/esp32_wifi.h` | ESP-IDF WiFi implementation |
| WiFi Module | `modules/wifi_module.h` | Connection manager with auto-reconnect |
| NTP Module | `modules/ntp_module.h` | Time sync with timezone support |
| Provisioning | `modules/wifi_provisioning.h` | SmartConfig + Captive Portal |

## Build Configuration

### Enable WiFi

In `platformio.ini`:

```ini
[env:esp32_wifi]
build_flags =
    -DESP32_BUILD
    -DHAL_ESP32
    -DDEBUG_CONSOLE_ENABLED
    -DWIFI_NTP_ENABLED
```

Or use the pre-configured environment:

```bash
pio run -e esp32_wifi
```

## WiFi Provisioning

The provisioning system allows users to configure WiFi without hardcoding credentials.

### Boot Sequence

```
┌─────────────┐
│    Boot     │
└──────┬──────┘
       │
┌──────▼──────┐
│  Check NVS  │──── Credentials found? ────┐
│ for WiFi    │                            │
└──────┬──────┘                            │
       │ No                                │ Yes
       │                                   │
┌──────▼──────┐                     ┌──────▼──────┐
│   Start     │                     │  Connect    │
│ Provisioning│                     │  to WiFi    │
└──────┬──────┘                     └──────┬──────┘
       │                                   │
       │                            ┌──────┴──────┐
       │                            │ Success?    │
       │                            └──────┬──────┘
       │                                   │ No
       │◄──────────────────────────────────┘
       │
┌──────▼──────────────────────┐
│  AP Mode + SmartConfig      │
│                             │
│  • AP: "Fermenter-Setup"    │
│  • IP: 192.168.4.1          │
│  • SmartConfig listening    │
└─────────────────────────────┘
```

### Provisioning Methods

#### 1. SmartConfig (ESP-Touch) - Recommended

No typing required! The phone app broadcasts WiFi credentials.

**Steps:**
1. Install ESP-Touch app on your phone
   - Android: [ESP-Touch](https://play.google.com/store/apps/details?id=com.espressif.iot.esptouch)
   - iOS: [Espressif Esptouch](https://apps.apple.com/app/espressif-esptouch/id1071176700)
2. Connect phone to your home WiFi
3. Open app and enter WiFi password
4. Press "Confirm" - app broadcasts credentials
5. ESP32 receives and connects automatically

**How it works:**
- Phone encodes SSID/password into WiFi packet lengths
- ESP32 in promiscuous mode captures and decodes packets
- No direct connection between phone and ESP32 needed

#### 2. Captive Portal

Web interface for manual credential entry.

**Steps:**
1. Connect phone/laptop to "Fermenter-Setup" AP
2. Browser should auto-open captive portal (or go to http://192.168.4.1)
3. Enter WiFi SSID and password
4. Click "Connect"
5. ESP32 saves credentials and reboots

### Usage Example

```cpp
#include "modules/wifi_provisioning.h"
#include "hal/esp32/esp32_time.h"

hal::esp32::ESP32Time time_hal;
modules::WifiProvisioning prov(&time_hal);

// Optional: customize AP name
modules::WifiProvisioning::Config cfg;
strncpy(cfg.ap_ssid, "Fermenter-LivingRoom", sizeof(cfg.ap_ssid));
cfg.enable_smartconfig = true;
cfg.enable_captive_portal = true;
prov.configure(cfg);

// Start provisioning (tries stored creds first)
if (prov.start()) {
    printf("Connected! IP: %s\n", prov.get_ip_address());
} else {
    printf("Provisioning mode active\n");
    printf("Connect to AP: %s\n", cfg.ap_ssid);
}
```

### Debug Console Commands

```bash
# Show WiFi status
> wifi
WiFi Status:
  State: CONNECTED
  IP: 192.168.1.100
  SSID: MyNetwork
  Method: STORED

# Start provisioning mode
> wifi provision

# Set credentials manually
> wifi set MyNetwork password123

# Clear stored credentials
> wifi clear

# Connect with stored credentials
> wifi connect
```

## NTP Time Synchronization

### Features

- SNTP client with configurable server
- POSIX timezone support
- Automatic resync
- ISO 8601 time formatting

### Timezone Examples

| Location | POSIX String |
|----------|-------------|
| UTC | `UTC0` |
| US Eastern | `EST5EDT,M3.2.0,M11.1.0` |
| US Pacific | `PST8PDT,M3.2.0,M11.1.0` |
| Central Europe | `CET-1CEST,M3.5.0,M10.5.0/3` |
| UK | `GMT0BST,M3.5.0/1,M10.5.0` |
| Japan | `JST-9` |

### Usage Example

```cpp
#include "modules/ntp_module.h"
#include "hal/esp32/esp32_time.h"

hal::esp32::ESP32Time time_hal;
modules::NtpModule ntp(&time_hal);

// Configure
modules::NtpModule::Config cfg;
cfg.server = "pool.ntp.org";
cfg.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
cfg.sync_interval_ms = 3600000;  // Resync every hour
ntp.configure(cfg);

// Initialize and wait for sync
ntp.init();
if (ntp.wait_for_sync(10000)) {
    printf("Time synced!\n");

    // Get formatted time
    char buf[30];
    ntp.format_iso8601(buf, sizeof(buf));
    printf("Current time: %s\n", buf);

    // Get local time struct
    struct tm local = ntp.get_local_time();
    printf("Hour: %d, Minute: %d\n", local.tm_hour, local.tm_min);
}
```

### Sync Status

```cpp
switch (ntp.get_status()) {
    case NtpModule::SyncStatus::NOT_STARTED:
        // NTP not initialized
        break;
    case NtpModule::SyncStatus::IN_PROGRESS:
        // Waiting for first sync
        break;
    case NtpModule::SyncStatus::SYNCED:
        // Time is valid
        break;
    case NtpModule::SyncStatus::FAILED:
        // Sync timeout
        break;
}
```

## WiFi Module

Lower-level connection manager (used internally by provisioning).

### Features

- Connection state machine
- Auto-reconnect with configurable interval
- Signal quality percentage
- Connection statistics

### Usage Example

```cpp
#include "modules/wifi_module.h"
#include "hal/esp32/esp32_wifi.h"

hal::esp32::ESP32WiFi wifi_hal;
hal::esp32::ESP32Time time_hal;

wifi_hal.init();
modules::WifiModule wifi(&wifi_hal, &time_hal);

// Configure
wifi.configure("MyNetwork", "password123", "fermenter");
wifi.set_auto_reconnect(true);
wifi.set_reconnect_interval(30000);

// Connect
if (wifi.connect()) {
    printf("Connected to %s\n", wifi.get_ip_address());
    printf("Signal: %d%%\n", wifi.get_signal_quality());
}

// Call periodically for auto-reconnect
void loop() {
    wifi.update();
}
```

## NVS Storage

WiFi credentials are stored in Non-Volatile Storage (NVS).

### Namespaces

| Namespace | Keys | Description |
|-----------|------|-------------|
| `wifi` | `ssid`, `password` | WiFi credentials |
| `config` | Various | System configuration |
| `fermenter` | Plans, PID | Fermentation data |
| `calibration` | Sensor offsets | Calibration data |

### Debug Console Commands

```bash
# List namespaces
> nvs list

# Get value
> nvs get wifi:ssid
wifi:ssid = "MyNetwork"

# Set value
> nvs set config:hostname fermenter-1

# Erase key
> nvs erase wifi:password

# Factory reset (erase all)
> factory
> factory confirm
```

## Integration Example

Complete startup sequence:

```cpp
#include "hal/esp32/esp32_wifi.h"
#include "hal/esp32/esp32_time.h"
#include "modules/wifi_provisioning.h"
#include "modules/ntp_module.h"

// HAL instances
hal::esp32::ESP32Time g_time;
modules::WifiProvisioning g_wifi_prov(&g_time);
modules::NtpModule g_ntp(&g_time);

void app_main() {
    // Initialize WiFi provisioning
    g_wifi_prov.init();

    // Try to connect (uses stored creds or starts provisioning)
    if (g_wifi_prov.start()) {
        printf("WiFi connected: %s\n", g_wifi_prov.get_ip_address());

        // Initialize NTP
        g_ntp.set_timezone("CET-1CEST,M3.5.0,M10.5.0/3");
        g_ntp.init();

        if (g_ntp.wait_for_sync(10000)) {
            printf("Time synchronized\n");
        }
    } else {
        printf("WiFi provisioning active\n");
        printf("Connect to: Fermenter-Setup\n");
    }

    // Main loop
    while (true) {
        // ... control loop
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

## Troubleshooting

### SmartConfig not working

1. Ensure phone is on 2.4GHz WiFi (not 5GHz)
2. Some routers block broadcast packets - try captive portal instead
3. Check ESP32 is in provisioning mode (`wifi provision`)

### Connection drops

1. Check signal strength (`wifi` command shows RSSI)
2. Increase reconnect interval if network is unstable
3. Check for IP conflicts

### Time not syncing

1. Verify WiFi is connected
2. Check firewall allows NTP (UDP port 123)
3. Try different NTP server (e.g., `time.google.com`)

### Factory reset

If device is in bad state:

```bash
> factory confirm
```

This erases all NVS data including WiFi credentials, forcing reprovisioning on next boot.

## Files

| File | Description |
|------|-------------|
| `include/hal/esp32/esp32_wifi.h` | ESP32 WiFi HAL implementation |
| `include/modules/wifi_module.h` | WiFi connection manager |
| `include/modules/ntp_module.h` | NTP time synchronization |
| `include/modules/wifi_provisioning.h` | SmartConfig + Captive Portal |
| `test/test_network/test_network.cpp` | Unit tests (20 tests) |
