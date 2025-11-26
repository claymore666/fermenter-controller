#pragma once

#include <cstdint>

namespace core {

// Maximum limits
constexpr uint8_t MAX_FERMENTERS = 8;
constexpr uint8_t MAX_SENSORS = 32;
constexpr uint8_t MAX_RELAYS = 24;
constexpr uint8_t MAX_PLAN_STEPS = 16;
constexpr uint8_t MAX_NAME_LENGTH = 32;

/**
 * Sensor data quality indicator
 */
enum class SensorQuality : uint8_t {
    GOOD = 0,       // Normal operation
    WARMING_UP,     // Filter not yet converged
    SUSPECT,        // Unusual value change
    BAD,            // Communication failure or out of range
    UNKNOWN         // Initial state
};

/**
 * Filter types for sensor smoothing
 */
enum class FilterType : uint8_t {
    NONE = 0,
    MOVING_AVERAGE,
    EMA,            // Exponential Moving Average
    MEDIAN,
    DUAL_RATE       // Separate filters for base and extra samples
};

/**
 * Sensor priority for poll scheduling
 */
enum class SensorPriority : uint8_t {
    LOW = 0,        // Base samples only (3/s)
    NORMAL,         // Few extra samples (1-3/s)
    HIGH,           // Moderate extras (5-10/s)
    CRITICAL        // Maximum extras (15-20/s)
};

/**
 * Relay types
 */
enum class RelayType : uint8_t {
    SOLENOID_NC = 0,    // Normally closed
    SOLENOID_NO,        // Normally open
    CONTACTOR_COIL,
    SSR                 // Solid state relay
};

/**
 * Fermenter operating mode
 */
enum class FermenterMode : uint8_t {
    OFF = 0,
    MANUAL,         // Manual setpoint control
    PLAN,           // Following fermentation plan
    AUTOTUNE        // PID autotuning in progress
};

/**
 * Sensor state - complete state for one sensor
 */
struct SensorState {
    char name[MAX_NAME_LENGTH];

    // Base samples (guaranteed rate)
    float base_samples[8];      // Ring buffer
    uint8_t base_index;
    float base_average;

    // Extra samples (priority-based)
    float extra_samples[16];    // Ring buffer
    uint8_t extra_index;
    float extra_average;

    // Filtered values
    float filtered_value;       // Combined filtered value
    float display_value;        // Extra smoothed for UI
    float raw_value;            // Latest raw reading

    // Metadata
    uint32_t timestamp;         // Last update (millis)
    SensorQuality quality;
    FilterType filter_type;
    float samples_per_second;

    // Filter state
    float ema_state;            // EMA accumulator
    float alpha;                // EMA alpha

    // Unit info
    char unit[8];               // "°C", "bar", etc.
    float scale;                // Raw to engineering unit scale
    float offset;               // Calibration offset

    SensorState()
        : name{}
        , base_samples{}
        , base_index(0)
        , base_average(0.0f)
        , extra_samples{}
        , extra_index(0)
        , extra_average(0.0f)
        , filtered_value(0.0f)
        , display_value(0.0f)
        , raw_value(0.0f)
        , timestamp(0)
        , quality(SensorQuality::UNKNOWN)
        , filter_type(FilterType::EMA)
        , samples_per_second(0.0f)
        , ema_state(0.0f)
        , alpha(0.3f)
        , unit{}
        , scale(0.0f)
        , offset(0.0f) {}
};

/**
 * Relay state
 */
struct RelayState {
    char name[MAX_NAME_LENGTH];
    bool state;                 // Current state (true = ON)
    uint32_t last_change;       // Timestamp of last state change
    float duty_cycle;           // For PWM/time-proportional control (0-100%)
    RelayType type;
    uint8_t gpio_pin;           // For ESP32 GPIO relays
    uint8_t modbus_addr;        // For MODBUS relays (0 = local GPIO)
    uint16_t modbus_reg;        // MODBUS register address

    RelayState()
        : name{}
        , state(false)
        , last_change(0)
        , duty_cycle(0.0f)
        , type(RelayType::SOLENOID_NC)
        , gpio_pin(0)
        , modbus_addr(0)
        , modbus_reg(0) {}
};

/**
 * PID parameters
 */
struct PIDParams {
    float kp;                   // Proportional gain
    float ki;                   // Integral gain
    float kd;                   // Derivative gain
    float output_min;           // Minimum output (0)
    float output_max;           // Maximum output (100)
    uint32_t sample_time_ms;    // PID calculation interval

    PIDParams() : kp(2.0f), ki(0.1f), kd(1.0f),
                  output_min(0.0f), output_max(100.0f),
                  sample_time_ms(5000) {}
};

/**
 * Fermentation plan step
 */
struct PlanStep {
    char name[MAX_NAME_LENGTH];
    uint32_t duration_hours;
    float target_temp;
    float target_pressure;

    PlanStep()
        : name{}
        , duration_hours(0)
        , target_temp(0.0f)
        , target_pressure(0.0f) {}
};

/**
 * Fermentation plan
 */
struct FermentationPlan {
    uint8_t fermenter_id;
    uint32_t start_time;        // Unix timestamp
    uint8_t current_step;
    uint8_t step_count;
    PlanStep steps[MAX_PLAN_STEPS];
    bool active;

    FermentationPlan()
        : fermenter_id(0)
        , start_time(0)
        , current_step(0)
        , step_count(0)
        , steps{}
        , active(false) {}
};

/**
 * Fermenter state
 */
struct FermenterState {
    uint8_t id;
    char name[MAX_NAME_LENGTH];

    // Current values
    float current_temp;
    float current_pressure;
    float target_temp;
    float target_pressure;

    // Operating mode
    FermenterMode mode;

    // Plan tracking
    bool plan_active;
    uint32_t plan_start_time;
    uint8_t current_step;
    float hours_remaining;

    // PID state
    PIDParams pid_params;
    float pid_output;           // Current PID output (0-100%)
    float pid_integral;         // Integral accumulator
    float pid_last_error;       // For derivative calculation

    // Sensor/relay mappings
    uint8_t temp_sensor_id;
    uint8_t pressure_sensor_id;
    uint8_t cooling_relay_id;
    uint8_t spunding_relay_id;

    FermenterState()
        : id(0)
        , name{}
        , current_temp(0.0f)
        , current_pressure(0.0f)
        , target_temp(0.0f)
        , target_pressure(0.0f)
        , mode(FermenterMode::OFF)
        , plan_active(false)
        , plan_start_time(0)
        , current_step(0)
        , hours_remaining(0.0f)
        , pid_params()
        , pid_output(0.0f)
        , pid_integral(0.0f)
        , pid_last_error(0.0f)
        , temp_sensor_id(0)
        , pressure_sensor_id(0)
        , cooling_relay_id(0)
        , spunding_relay_id(0) {}
};

/**
 * System state
 */
struct SystemState {
    uint32_t uptime_seconds;
    uint32_t last_boot;         // Unix timestamp
    bool ntp_synced;
    int wifi_rssi;
    uint32_t free_heap;
    float cpu_usage;            // CPU usage percentage (0-100)
    uint32_t cpu_freq_mhz;      // Current CPU frequency in MHz
    uint32_t cpu_freq_max_mhz;  // Max CPU frequency in MHz

    // Diagnostics
    uint32_t modbus_transactions;
    uint32_t modbus_errors;

    SystemState()
        : uptime_seconds(0)
        , last_boot(0)
        , ntp_synced(false)
        , wifi_rssi(0)
        , free_heap(0)
        , cpu_usage(0.0f)
        , cpu_freq_mhz(0)
        , cpu_freq_max_mhz(0)
        , modbus_transactions(0)
        , modbus_errors(0) {}
};

/**
 * Event types for Event Bus
 */
enum class EventType : uint8_t {
    SENSOR_UPDATE = 0,
    RELAY_CHANGE,
    PLAN_STEP_CHANGE,
    PLAN_COMPLETE,
    ALARM,
    CONFIG_CHANGE,
    SYSTEM_STATUS
};

/**
 * Event data structure
 */
struct Event {
    EventType type;
    uint8_t source_id;          // Sensor/fermenter/relay ID
    uint32_t timestamp;
    union {
        float value;
        bool state;
        uint8_t step;
    } data;

    Event()
        : type(EventType::SENSOR_UPDATE)
        , source_id(0)
        , timestamp(0)
        , data{} {}
};

/**
 * Alarm severity levels
 */
enum class AlarmSeverity : uint8_t {
    INFO = 0,
    WARNING,
    ERROR,
    CRITICAL
};

/**
 * Alarm structure
 */
struct Alarm {
    AlarmSeverity severity;
    char message[64];
    uint8_t source_id;
    uint32_t timestamp;
    bool acknowledged;

    Alarm()
        : severity(AlarmSeverity::INFO)
        , message{}
        , source_id(0)
        , timestamp(0)
        , acknowledged(false) {}
};

} // namespace core
