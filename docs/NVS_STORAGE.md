# NVS Storage Reference

Non-Volatile Storage (NVS) key-value pairs used by the fermentation controller.

## Overview

NVS stores persistent data that survives reboots. ESP-IDF NVS uses namespaces to organize keys.

## Namespaces and Keys

### Namespace: `wifi`

WiFi credentials and connection settings.

| Key | Type | Max Size | Description | Module |
|-----|------|----------|-------------|--------|
| `ssid` | string | 32 | WiFi network name | wifi_provisioning.h |
| `password` | string | 64 | WiFi password | wifi_provisioning.h |
| `auto_connect` | u8 | 1 | Auto-connect on boot (0/1) | wifi_provisioning.h |

**Debug console access:**
```
nvs get wifi:ssid
nvs get wifi:password
nvs get wifi:auto_connect
nvs set wifi:auto_connect 1
```

### Namespace: `fermenter`

Main application data storage (used via `ESP32Storage` interface).

| Key | Type | Max Size | Description | Module |
|-----|------|----------|-------------|--------|
| `http:prov` | string | 2 | HTTP provisioned flag ("0"/"1") | http_server.h |
| `http:pw_hash` | string | 65 | Admin password SHA-256 hash | http_server.h |
| `ssl:cert` | string | 2048 | SSL certificate (PEM format) | http_server.h |
| `ssl:key` | string | 2048 | SSL private key (PEM format) | http_server.h |
| `plan_1` | blob | ~256 | Fermentation plan for fermenter 1 | fermentation_plan.h |
| `plan_2` | blob | ~256 | Fermentation plan for fermenter 2 | fermentation_plan.h |
| `plan_3` | blob | ~256 | Fermentation plan for fermenter 3 | fermentation_plan.h |
| `plan_4` | blob | ~256 | Fermentation plan for fermenter 4 | fermentation_plan.h |

**Debug console access:**
```
ssl status          # Check cert/key storage
ssl clear           # Erase certificate
nvs get fermenter:http:prov
```

Note: SSL cert/key are too large for the generic `nvs get` command (128-byte buffer). Use `ssl status` instead.

## Debug Console NVS Commands

| Command | Description |
|---------|-------------|
| `nvs list` | Show available namespaces |
| `nvs get <ns>:<key>` | Read a value |
| `nvs set <ns>:<key> <value>` | Write a value |
| `nvs erase <ns>:<key>` | Delete a key |

### Examples

```
> nvs get wifi:ssid
wifi:ssid = "Braustube"

> nvs set config:brightness 50
Set config:brightness = 50

> nvs erase wifi:password
Erased wifi:password
```

## WiFi Commands

Shortcut commands for WiFi credential management:

| Command | Description |
|---------|-------------|
| `wifi` | Show WiFi status (SSID, IP, RSSI) |
| `wifi set <ssid> <password>` | Set credentials |
| `wifi clear` | Clear stored credentials |
| `wifi scan` | Scan for networks |

## SSL Commands

| Command | Description |
|---------|-------------|
| `ssl status` | Show cert/key sizes |
| `ssl clear` | Delete certificate (regenerates on reboot) |

## Factory Reset

The `factory` command erases all NVS data:
- WiFi credentials
- Admin password hash
- SSL certificate
- Fermentation plans

```
> factory
WARNING: This will erase all settings!
Type 'YES' to confirm: YES
Erasing NVS...
Factory reset complete. Rebooting...
```

## Storage Limits

- NVS partition size: 20 KB (0x5000)
- Maximum key length: 15 characters
- Maximum string value: ~4000 bytes (practical limit)
- Maximum blob: ~4000 bytes (practical limit)

## Module Dependencies

```
ESP32Storage (esp32_storage.h)
├── Uses namespace "fermenter"
├── Provides: read/write_string, read/write_blob, read/write_int
└── Used by: http_server.h, fermentation_plan.h

WiFi Provisioning (wifi_provisioning.h)
├── Uses namespace "wifi"
└── Direct nvs_* calls for ssid, password, auto_connect

Debug Console (debug_console.h)
├── Generic nvs_* access to any namespace
└── Uses <namespace>:<key> format
```

## Consistency Notes

All modules use consistent namespace/key naming:
- `wifi:*` - WiFi credentials (wifi_provisioning.h)
- `fermenter:http:*` - HTTP settings (http_server.h)
- `fermenter:ssl:*` - SSL certificate (http_server.h)
- `fermenter:plan_*` - Fermentation plans (fermentation_plan.h)
