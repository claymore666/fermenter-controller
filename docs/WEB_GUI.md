# Web GUI Implementation Plan

## Overview

Professional SCADA/HMI-style web interface hosted on ESP32-S3, featuring:
- Brewery process diagram with animated tanks, pipes, and valves
- Real-time sensor monitoring
- Full administrative control mirroring debug console functionality
- Bootstrap 5 dark theme with modern look

## Storage Strategy

| Storage | Contents | Rationale |
|---------|----------|-----------|
| **SD Card** | Web UI files, SVG assets, data logs | Easy updates (swap card), large capacity (GB) |
| **SPIFFS** | Fallback/minimal UI, config | Fast access, survives card removal |

**SD Card Pins (1-bit mode):**
- GPIO45: SD_D0
- GPIO47: SD_CMD
- GPIO48: SD_SCK

## Technology Stack

### Framework: Preact
- **Size:** 4KB gzipped (10x smaller than React)
- **API:** 100% React-compatible (useState, useEffect, JSX)
- **Why:** Same familiar React patterns, much smaller bundle

### UI Components: React-Bootstrap
- **Size:** ~30KB gzipped
- **Components:** Cards, Badges, Forms, ProgressBars, Modals
- **Theme:** Bootstrap 5 dark mode (`data-bs-theme="dark"`)

### CSS: Bootstrap 5
- **Size:** ~25KB gzipped
- **Theme:** Dark mode built-in since v5.3
- **Icons:** Bootstrap Icons (subset)

### Process Diagrams: Custom SVG
- Inline SVG for tanks, pipes, valves
- CSS animations for flowing pipes, pulsing valves
- Dynamic updates via JavaScript

### Real-time: WebSocket
- 1Hz push from ESP32
- JSON payload with all sensor/relay/fermenter state
- Auto-reconnect on disconnect

## Estimated Bundle Size

| Component | Size (gzip) |
|-----------|-------------|
| Preact | 4KB |
| React-Bootstrap | 30KB |
| Bootstrap 5 CSS | 25KB |
| Bootstrap Icons | 10KB |
| Application JS | 50-100KB |
| SVG Assets | 30-50KB |
| **Total** | **~150-220KB** |

Fits easily in available storage with room to spare.

## Features

### 1. Process Diagram View
- SVG brewery schematic showing:
  - 8 fermenters with temperature/pressure displays
  - Glycol chiller and supply/return lines
  - Cooling and spunding valves
  - Pipe flow animations when active
- Color-coded status (green=OK, yellow=warning, red=alarm)
- Click fermenter for detail panel

### 2. Admin Dashboard
- All debug console functionality in web UI
- Password protected (default: "admin")
- Categories:
  - System status, uptime, heap, modules
  - Sensor readings and configuration
  - Relay manual control
  - Fermenter setpoint and mode
  - PID tuning parameters
  - MODBUS diagnostics
  - WiFi configuration
  - NVS management
  - Alarms and safety

### 3. Fermentation Plan Management
- Upload/edit plans (JSON)
- Visual timeline of steps
- Current progress indicator
- Start/stop/pause controls

### 4. Configuration Panel
- Timing parameters
- Filter settings per sensor
- Safety thresholds
- MODBUS device configuration

### 5. Alarm History
- Active alarms with timestamps
- Historical alarm log
- Acknowledge/clear alarms

## Architecture

```
web/
├── src/
│   ├── index.jsx                    # Entry point
│   ├── App.jsx                      # Main layout with routing
│   ├── components/
│   │   ├── ProcessDiagram.jsx       # SVG brewery view
│   │   ├── Fermenter.jsx            # Tank SVG component
│   │   ├── FermenterPanel.jsx       # Detail modal/panel
│   │   ├── AdminDashboard.jsx       # Console-like admin
│   │   ├── PlanManager.jsx          # Plan upload/edit
│   │   ├── ConfigPanel.jsx          # Settings editor
│   │   ├── AlarmBar.jsx             # Active alarm display
│   │   └── Login.jsx                # Admin authentication
│   ├── hooks/
│   │   ├── useWebSocket.js          # Real-time data
│   │   └── useAuth.js               # Session management
│   └── assets/
│       └── brewery.svg              # Process diagram base
├── vite.config.js                   # Build config
├── package.json
└── dist/                            # Built files → SD card
```

## WebSocket Protocol

### Server → Client (1Hz)
```json
{
  "type": "state",
  "timestamp": 1700000000,
  "system": {
    "uptime_seconds": 3600,
    "free_heap": 200000,
    "wifi_rssi": -65,
    "ntp_synced": true
  },
  "sensors": {
    "glycol_supply": {"value": 2.1, "quality": "GOOD"},
    "fermenter_1_temp": {"value": 18.2, "quality": "GOOD"},
    "fermenter_1_pressure": {"value": 1.1, "quality": "GOOD"}
  },
  "relays": {
    "glycol_chiller": true,
    "f1_cooling": true,
    "f1_spunding": false
  },
  "fermenters": {
    "F1": {
      "temp": 18.2,
      "setpoint": 18.0,
      "pressure": 1.1,
      "mode": "PLAN",
      "pid_output": 45.2,
      "plan_active": true,
      "current_step": 2,
      "hours_remaining": 48.5
    }
  },
  "alarms": []
}
```

### Client → Server (Commands)
```json
{
  "type": "command",
  "cmd": "relay",
  "args": ["f1_cooling", "on"]
}
```

### Server → Client (Response)
```json
{
  "type": "response",
  "success": true,
  "message": "Relay f1_cooling set to ON"
}
```

## Authentication

### Admin Session
- Default password: "admin"
- Password change via serial console: `nvs set admin:password <newpass>`
- Session token stored in localStorage
- Token expires after 1 hour of inactivity

### REST Endpoints
```
POST /api/login         # Get session token
POST /api/logout        # Invalidate token
GET  /api/status        # Requires auth
POST /api/command       # Requires auth
```

## Development Workflow

### Setup
```bash
npm create vite@latest brewery-ui -- --template preact
cd brewery-ui
npm install react-bootstrap bootstrap bootstrap-icons
```

### vite.config.js
```javascript
import { defineConfig } from 'vite'
import preact from '@preact/preset-vite'

export default defineConfig({
  plugins: [preact()],
  build: {
    outDir: 'dist',
    assetsInlineLimit: 0
  }
})
```

### Development
```bash
npm run dev    # Hot reload at localhost:5173
```

### Build & Deploy
```bash
npm run build                    # Build to dist/
gzip -r dist/                    # Compress
cp -r dist/* /media/sdcard/www/  # Copy to SD card
```

## ESP32 HTTP Server Integration

### SD Card Initialization
```cpp
#include "SD_MMC.h"

bool initSDCard() {
    if (!SD_MMC.begin("/sdcard", true)) {  // 1-bit mode
        return false;
    }
    return true;
}
```

### Static File Serving
```cpp
// Serve files from /sdcard/www/
esp_err_t static_get_handler(httpd_req_t *req) {
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/sdcard/www%s", req->uri);

    // Serve index.html for SPA routes
    if (/* is directory or not found */) {
        strcpy(filepath, "/sdcard/www/index.html");
    }

    // Set content type, serve file...
}
```

### WebSocket Handler
```cpp
// Push state every 1000ms
void websocket_broadcast_state() {
    char json[2048];
    build_state_json(json, sizeof(json));
    httpd_ws_send_frame_to_all(json);
}
```

## Future Enhancements

### Phase 2
- Historical data graphs (Chart.js)
- Data export (CSV download)
- Multiple language support
- Responsive mobile layout

### Phase 3
- Recipe database integration
- Brewfather API sync
- Push notifications
- Camera integration

## Related Documentation

- [BUILD_FLAGS.md](BUILD_FLAGS.md) - HTTP_ENABLED compile flag
- [DEBUG_CONSOLE.md](DEBUG_CONSOLE.md) - Serial console commands (mirrored in web)
- [WIFI_NTP.md](WIFI_NTP.md) - Network connectivity
