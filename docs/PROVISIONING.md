# Device Provisioning Guide

This document describes how to provision a fresh ESP32 fermenter controller with WiFi credentials and admin password.

## Quick Start (Automated)

Use the provisioning script to set up WiFi and admin password in one command:

```bash
# Install dependencies
pip install pyserial requests

# Run provisioning
python scripts/provision_device.py \
    --port /dev/ttyACM0 \
    --ssid "YourNetworkSSID" \
    --wifi-pass "YourWiFiPassword" \
    --admin-pass "YourAdminPass123" \
    --ip "192.168.0.139"
```

### Script Options

| Option | Short | Default | Description |
|--------|-------|---------|-------------|
| `--port` | `-p` | `/dev/ttyACM0` | Serial port |
| `--baud` | `-b` | `115200` | Baud rate |
| `--ssid` | `-s` | (required) | WiFi network name |
| `--wifi-pass` | `-w` | (required) | WiFi password |
| `--admin-pass` | `-a` | (required) | Admin password |
| `--ip` | `-i` | `192.168.0.139` | Expected device IP |

### Password Requirements

Admin password must:
- Be at least 8 characters long
- Contain at least 2 of: lowercase, uppercase, digit

Valid examples: `Admin123`, `myPass99`, `SECRET42`

## Manual Provisioning

### Step 1: Connect to Serial Console

```bash
# Using screen
screen /dev/ttyACM0 115200

# Using picocom
picocom -b 115200 /dev/ttyACM0

# Using minicom
minicom -D /dev/ttyACM0 -b 115200
```

Press Enter to see the `fermenter>` prompt.

### Step 2: Set WiFi Credentials

```
fermenter> wifi set YourSSID YourPassword
Setting WiFi credentials...
Credentials saved for: YourSSID
```

Check connection status:

```
fermenter> wifi
WiFi Status:
  State: CONNECTED
  IP: 192.168.0.139
  SSID: YourSSID
  Method: STORED
```

### Step 3: Set Admin Password

Once WiFi is connected, open a browser or use curl:

**Via Browser:**
1. Navigate to `http://192.168.0.139/admin/`
2. Fill in the "First-Time Setup" form
3. Enter password twice and click "Create Password"

**Via curl:**
```bash
curl -X POST http://192.168.0.139/api/setup \
    -H "Content-Type: application/json" \
    -d '{"password":"YourAdminPass123"}'
```

Expected response:
```json
{"success":true,"message":"Device provisioned successfully"}
```

### Step 4: Verify

```bash
curl http://192.168.0.139/api/health
```

Expected response:
```json
{"status":"ok","provisioned":true}
```

## Debug Console Commands Reference

### WiFi Commands

| Command | Description |
|---------|-------------|
| `wifi` | Show WiFi status |
| `wifi set <ssid> <pass>` | Set WiFi credentials |
| `wifi connect` | Connect with stored credentials |
| `wifi disconnect` | Disconnect (persistent) |
| `wifi clear` | Clear credentials and start provisioning |

### NVS Commands

| Command | Description |
|---------|-------------|
| `nvs list` | List NVS namespaces |
| `nvs get <ns>:<key>` | Read NVS value |
| `nvs set <ns>:<key> <val>` | Write NVS value |
| `nvs erase <ns>:<key>` | Delete NVS key |

### System Commands

| Command | Description |
|---------|-------------|
| `status` | Show system status |
| `reboot` | Reboot device |
| `factory` | Factory reset (erases all settings) |

## Troubleshooting

### Device not reachable after WiFi setup

1. Check WiFi status via serial: `wifi`
2. Verify SSID and password are correct
3. Check if device got expected IP or use `wifi` to see assigned IP
4. Try `wifi connect` to reconnect

### "Device already provisioned" error

The admin password is already set. Options:
- Login with existing password
- Change password via Settings menu
- Factory reset via debug console: `factory`

### Serial port busy

```bash
# Find process using the port
fuser /dev/ttyACM0

# Kill the process
fuser -k /dev/ttyACM0
```

### Permission denied on serial port

```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER

# Or set permissions temporarily
sudo chmod 666 /dev/ttyACM0
```

## Factory Reset

To completely reset the device (clears WiFi and admin password):

**Via Serial:**
```
fermenter> factory
WARNING: This will erase all NVS data including:
  - WiFi credentials
  - Admin password
  - All stored configuration
Are you sure? Type 'yes' to confirm: yes
Erasing NVS...
NVS erased successfully
Rebooting...
```

**Via Web Interface:**
1. Login to admin panel
2. Go to Settings
3. Click "Factory Reset" in Danger Zone

**Via API (requires authentication):**
```bash
curl -X POST http://192.168.0.139/api/factory_reset \
    -H "Authorization: Bearer <token>"
```
