# Data Model Reference

Core data structures used by the fermentation controller.

## Overview

All data structures are defined in `include/core/types.h`. They represent the runtime state of sensors, relays, fermenters, and fermentation plans.

## Constants

```cpp
constexpr uint8_t MAX_FERMENTERS = 8;
constexpr uint8_t MAX_SENSORS = 32;
constexpr uint8_t MAX_RELAYS = 24;
constexpr uint8_t MAX_PLAN_STEPS = 16;
constexpr uint8_t MAX_NAME_LENGTH = 32;
```

## Enums

### SensorQuality

Indicates data quality for sensor readings.

| Value | Description |
|-------|-------------|
| `GOOD` | Normal operation |
| `WARMING_UP` | Filter not yet converged |
| `SUSPECT` | Unusual value change |
| `BAD` | Communication failure or out of range |
| `UNKNOWN` | Initial state |

### FilterType

Smoothing filter types for sensors.

| Value | Description |
|-------|-------------|
| `NONE` | No filtering |
| `MOVING_AVERAGE` | Simple moving average |
| `EMA` | Exponential Moving Average |
| `MEDIAN` | Median filter |
| `DUAL_RATE` | Separate filters for base/extra samples |

### FermenterMode

Operating mode for fermenters.

| Value | Description |
|-------|-------------|
| `OFF` | Fermenter disabled |
| `MANUAL` | Manual setpoint control |
| `PLAN` | Following fermentation plan |
| `AUTOTUNE` | PID autotuning in progress |

### RelayType

Physical relay types.

| Value | Description |
|-------|-------------|
| `SOLENOID_NC` | Normally closed solenoid |
| `SOLENOID_NO` | Normally open solenoid |
| `CONTACTOR_COIL` | Contactor coil |
| `SSR` | Solid state relay |

### AlarmSeverity

| Value | Description |
|-------|-------------|
| `INFO` | Informational |
| `WARNING` | Non-critical warning |
| `ERROR` | Error condition |
| `CRITICAL` | Critical alarm |

## Structures

### SensorState

Complete state for one sensor.

| Field | Type | Description |
|-------|------|-------------|
| `name` | char[32] | Sensor name |
| `base_samples` | float[8] | Ring buffer for base samples |
| `base_index` | uint8_t | Current position in base buffer |
| `base_average` | float | Average of base samples |
| `extra_samples` | float[16] | Ring buffer for priority samples |
| `extra_index` | uint8_t | Current position in extra buffer |
| `extra_average` | float | Average of extra samples |
| `filtered_value` | float | Combined filtered value |
| `display_value` | float | Extra smoothed for UI |
| `raw_value` | float | Latest raw reading |
| `timestamp` | uint32_t | Last update (millis) |
| `quality` | SensorQuality | Data quality indicator |
| `filter_type` | FilterType | Active filter type |
| `samples_per_second` | float | Actual sample rate |
| `ema_state` | float | EMA accumulator |
| `alpha` | float | EMA alpha (default 0.3) |
| `unit` | char[8] | Unit string ("°C", "bar") |
| `scale` | float | Raw to engineering scale |
| `offset` | float | Calibration offset |

### RelayState

State for one relay.

| Field | Type | Description |
|-------|------|-------------|
| `name` | char[32] | Relay name |
| `state` | bool | Current state (true = ON) |
| `last_change` | uint32_t | Timestamp of last change |
| `duty_cycle` | float | PWM duty cycle (0-100%) |
| `type` | RelayType | Physical relay type |
| `gpio_pin` | uint8_t | GPIO pin (for local relays) |
| `modbus_addr` | uint8_t | MODBUS address (0 = local) |
| `modbus_reg` | uint16_t | MODBUS register address |

### PIDParams

PID controller parameters.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `kp` | float | 2.0 | Proportional gain |
| `ki` | float | 0.1 | Integral gain |
| `kd` | float | 1.0 | Derivative gain |
| `output_min` | float | 0.0 | Minimum output |
| `output_max` | float | 100.0 | Maximum output |
| `sample_time_ms` | uint32_t | 5000 | PID interval (ms) |

### PlanStep

One step in a fermentation plan.

| Field | Type | Description |
|-------|------|-------------|
| `name` | char[32] | Step name (e.g., "Cold Crash") |
| `duration_hours` | uint32_t | Step duration in hours |
| `target_temp` | float | Target temperature (°C) |
| `target_pressure` | float | Target pressure (bar) |

### FermentationPlan

Complete fermentation plan (stored in NVS).

| Field | Type | Description |
|-------|------|-------------|
| `fermenter_id` | uint8_t | Assigned fermenter (1-8) |
| `start_time` | uint32_t | Unix timestamp of start |
| `current_step` | uint8_t | Current step index (0-15) |
| `step_count` | uint8_t | Number of steps |
| `steps` | PlanStep[16] | Array of plan steps |
| `active` | bool | Plan is running |

**NVS Storage:** Stored as blob in `fermenter:plan_1` through `fermenter:plan_4`

### FermenterState

Complete state for one fermenter.

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint8_t | Fermenter ID (1-8) |
| `name` | char[32] | Fermenter name |
| `current_temp` | float | Current temperature (°C) |
| `current_pressure` | float | Current pressure (bar) |
| `target_temp` | float | Target temperature (°C) |
| `target_pressure` | float | Target pressure (bar) |
| `mode` | FermenterMode | Operating mode |
| `plan_active` | bool | Plan is active |
| `plan_start_time` | uint32_t | Plan start timestamp |
| `current_step` | uint8_t | Current plan step |
| `hours_remaining` | float | Hours until step complete |
| `pid_params` | PIDParams | PID tuning parameters |
| `pid_output` | float | Current PID output (0-100%) |
| `pid_integral` | float | Integral accumulator |
| `pid_last_error` | float | Last error (for derivative) |
| `temp_sensor_id` | uint8_t | Temperature sensor ID |
| `pressure_sensor_id` | uint8_t | Pressure sensor ID |
| `cooling_relay_id` | uint8_t | Cooling relay ID |
| `spunding_relay_id` | uint8_t | Spunding valve relay ID |

### SystemState

Global system status.

| Field | Type | Description |
|-------|------|-------------|
| `uptime_seconds` | uint32_t | Time since boot |
| `last_boot` | uint32_t | Boot timestamp (Unix) |
| `ntp_synced` | bool | NTP time synchronized |
| `wifi_rssi` | int | WiFi signal strength (dBm) |
| `free_heap` | uint32_t | Free heap memory (bytes) |
| `cpu_usage` | float | CPU usage (0-100%) |
| `cpu_freq_mhz` | uint32_t | Current CPU frequency |
| `cpu_freq_max_mhz` | uint32_t | Max CPU frequency |
| `modbus_transactions` | uint32_t | Total MODBUS transactions |
| `modbus_errors` | uint32_t | MODBUS error count |

### Event

Event bus message structure.

| Field | Type | Description |
|-------|------|-------------|
| `type` | EventType | Event type |
| `source_id` | uint8_t | Source sensor/fermenter/relay ID |
| `timestamp` | uint32_t | Event timestamp |
| `data` | union | Value (float), state (bool), or step (uint8_t) |

### Alarm

Alarm structure.

| Field | Type | Description |
|-------|------|-------------|
| `severity` | AlarmSeverity | Alarm severity level |
| `message` | char[64] | Alarm message text |
| `source_id` | uint8_t | Source ID |
| `timestamp` | uint32_t | Alarm timestamp |
| `acknowledged` | bool | User acknowledged |

## REST API JSON Representation

### Fermenter (GET /api/fermenter/{id})

```json
{
  "id": 1,
  "name": "Fermenter 1",
  "current_temp": 18.5,
  "target_temp": 18.0,
  "current_pressure": 0.5,
  "target_pressure": 1.0,
  "mode": "manual",
  "pid_output": 45.2,
  "plan_active": false
}
```

### Sensor (GET /api/sensors)

```json
[
  {
    "name": "fermenter_1_temp",
    "value": 18.5,
    "unit": "°C",
    "quality": "good"
  }
]
```

### Relay (GET /api/relays)

```json
[
  {
    "name": "f1_cooling",
    "state": true
  }
]
```

### Plan (GET /api/plan/{id})

```json
{
  "fermenter_id": 1,
  "active": true,
  "current_step": 0,
  "steps": [
    {
      "name": "Primary",
      "duration_hours": 168,
      "target_temp": 18.0,
      "target_pressure": 1.0
    }
  ]
}
```

## File References

| File | Contents |
|------|----------|
| `include/core/types.h` | All struct/enum definitions |
| `include/core/config.h` | Configuration structures |
| `include/core/state_manager.h` | State storage and access |
| `include/modules/rest_api.h` | JSON serialization |
