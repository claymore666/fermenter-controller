# OTA Firmware Updates

Over-The-Air (OTA) firmware update support for the fermentation controller.

## Overview

The ESP32-S3 uses a dual OTA partition layout (app0/app1, 4MB each) allowing safe firmware updates with automatic rollback capability. During OTA, the new firmware is written to the inactive partition while the active partition continues running.

## Features

- **Upload-based OTA**: Upload firmware binary via web interface or REST API
- **URL-based OTA**: Download firmware from URL (default: GitHub OTA branch)
- **Automatic rollback**: If new firmware fails to boot, automatically reverts to previous
- **System pause**: MODBUS polling, PID control, and sensor updates pause during OTA
- **Progress tracking**: Real-time upload progress in web interface

## Web Interface

Navigate to **Settings** in the admin interface to access the Firmware Update section.

### Current Firmware Info

Displays:
- Version
- Running partition (app0 or app1)
- Firmware size
- Status (Verified or Pending Verification)

### Upload Firmware

1. Click **Choose File** and select a `.bin` firmware file
2. Click **Upload & Install**
3. Wait for upload to complete (progress shown)
4. Device will automatically reboot

### Download from GitHub

1. Leave URL empty for default OTA branch, or enter custom URL
2. Click **Download & Install**
3. Device will download, verify, and install the firmware
4. Device will automatically reboot

### Confirm / Rollback

After installing new firmware, it remains in "Pending Verification" state:

- **Confirm**: Mark firmware as valid (prevents rollback)
- **Rollback**: Revert to previous firmware and reboot

If the device fails to boot 3 times, it automatically rolls back to the previous firmware.

## REST API

### GET /api/firmware/info

Returns current firmware and partition information.

```json
{
  "version": "0.3.0",
  "running_partition": "app0",
  "next_partition": "app1",
  "firmware_size": 1478445,
  "pending_verify": false
}
```

### GET /api/firmware/status

Returns OTA operation status (during upload/download).

```json
{
  "state": "writing",
  "bytes_written": 524288,
  "total_bytes": 1478445,
  "progress_percent": 35
}
```

### POST /api/firmware/upload

Upload firmware binary directly. Send raw binary data with `Authorization: Bearer <token>` header.

```bash
curl -X POST https://fermenter.local/api/firmware/upload \
  -H "Authorization: Bearer <token>" \
  --data-binary @firmware.bin
```

Response:
```json
{
  "success": true,
  "message": "OTA complete, rebooting..."
}
```

### POST /api/firmware/download

Download and install firmware from URL.

```json
{
  "url": "https://example.com/firmware.bin"
}
```

Omit `url` to use default GitHub OTA branch URL.

Response:
```json
{
  "success": true,
  "message": "OTA complete, rebooting..."
}
```

### POST /api/firmware/confirm

Confirm current firmware as valid (prevents automatic rollback).

```json
{
  "success": true,
  "message": "Firmware confirmed"
}
```

### POST /api/firmware/rollback

Roll back to previous firmware and reboot.

```json
{
  "success": true,
  "message": "Rolling back, rebooting..."
}
```

## GitHub OTA Branch Setup

The default download URL points to:
```
https://raw.githubusercontent.com/claymore666/fermenter-controller/OTA/firmware.bin
```

To set up automated OTA releases:

1. Create an `OTA` branch in your repository
2. Build firmware: `pio run -e esp32_wifi`
3. Copy `.pio/build/esp32_wifi/firmware.bin` to the OTA branch
4. Commit and push to the OTA branch

Devices can then pull updates by clicking **Download & Install** with the default URL.

## System Behavior During OTA

When OTA starts:

1. **System pause callback** triggers
2. **MODBUS polling** stops
3. **PID control** stops
4. **Sensor updates** stop
5. **WebSocket clients** timeout (will reconnect after reboot)

This ensures:
- No interference with flash writes
- Reduced memory pressure
- Clean shutdown of bus operations

## Partition Layout

```
Flash (16MB):
├── bootloader
├── partition table
├── nvs (32KB) - credentials, config
├── phy_init
├── app0 (4MB) - OTA partition 0
├── app1 (4MB) - OTA partition 1
└── spiffs (8MB) - web assets, plans
```

## Error Handling

| Error | Cause | Solution |
|-------|-------|----------|
| `No OTA partition` | Wrong partition table | Check partitions.csv |
| `Not enough space` | Firmware too large | Reduce firmware size |
| `Invalid image` | Corrupt firmware | Re-download and retry |
| `Write failed` | Flash error | Check flash health |
| `Download failed` | Network error | Check URL and connectivity |

## Build Flag

OTA is enabled via build flag in `platformio.ini`:

```ini
-DOTA_ENABLED
```

This flag is included in the `esp32_wifi` and `esp32_full` environments.
