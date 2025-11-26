#pragma once

#include "hal/interfaces.h"
#include "core/types.h"
#include "core/config.h"
#include "core/state_manager.h"
#include "core/event_bus.h"
#include "core/utils.h"

namespace modules {

/**
 * Alarm state for a fermenter
 */
struct FermenterAlarmState {
    bool temp_high_alarm;
    bool temp_low_alarm;
    bool pressure_high_alarm;
    bool sensor_failure_alarm;
    uint32_t temp_deviation_start;
    uint32_t last_alarm_time;

    FermenterAlarmState()
        : temp_high_alarm(false)
        , temp_low_alarm(false)
        , pressure_high_alarm(false)
        , sensor_failure_alarm(false)
        , temp_deviation_start(0)
        , last_alarm_time(0) {}
};

/**
 * Safety Controller
 * Monitors system for dangerous conditions and takes protective action
 */
class SafetyController {
public:
    SafetyController(hal::ITimeInterface* time,
                     hal::IGPIOInterface* gpio,
                     core::StateManager* state,
                     core::EventBus* events)
        : time_(time)
        , gpio_(gpio)
        , state_(state)
        , events_(events)
        , override_active_(false)
        , override_timeout_(0) {

        // Set default limits
        max_temp_deviation_ = 3.0f;
        max_pressure_bar_ = 2.5f;
        temp_deviation_timeout_ms_ = 900000;  // 15 minutes
        alarm_cooldown_ms_ = 60000;           // 1 minute
    }

    /**
     * Configure safety limits from system config
     */
    void configure(const core::SafetyTimingConfig& config) {
        max_temp_deviation_ = config.max_temp_deviation;
        max_pressure_bar_ = config.max_pressure_bar;
        temp_deviation_timeout_ms_ = config.temp_deviation_timeout_ms;
        alarm_cooldown_ms_ = config.alarm_cooldown_ms;
    }

    /**
     * Check all safety conditions
     * Call this periodically (e.g., every second)
     */
    void check() {
        if (override_active_) {
            // Check if override has timed out
            if (time_->millis() > override_timeout_) {
                override_active_ = false;
            } else {
                return;  // Skip checks during override
            }
        }

        uint32_t now = time_->millis();

        // Check each fermenter by iterating through all possible IDs
        for (uint8_t id = 1; id <= core::MAX_FERMENTERS; id++) {
            auto* ferm = state_->get_fermenter(id);
            if (ferm) {
                check_fermenter(id, ferm, now);
            }
        }
    }

    /**
     * Enable safety override (use with caution!)
     * @param duration_ms Duration of override in milliseconds
     */
    void enable_override(uint32_t duration_ms) {
        override_active_ = true;
        override_timeout_ = time_->millis() + duration_ms;
    }

    /**
     * Disable safety override
     */
    void disable_override() {
        override_active_ = false;
    }

    /**
     * Check if any alarms are active
     */
    bool has_active_alarms() const {
        for (uint8_t i = 0; i < core::MAX_FERMENTERS; i++) {
            if (alarms_[i].temp_high_alarm ||
                alarms_[i].temp_low_alarm ||
                alarms_[i].pressure_high_alarm ||
                alarms_[i].sensor_failure_alarm) {
                return true;
            }
        }
        return false;
    }

    /**
     * Check if any critical errors are active (sensor failures)
     */
    bool has_active_errors() const {
        for (uint8_t i = 0; i < core::MAX_FERMENTERS; i++) {
            if (alarms_[i].sensor_failure_alarm) {
                return true;
            }
        }
        return false;
    }

    /**
     * Check if any warnings are active (temp/pressure alarms, not failures)
     */
    bool has_active_warnings() const {
        for (uint8_t i = 0; i < core::MAX_FERMENTERS; i++) {
            if (alarms_[i].temp_high_alarm ||
                alarms_[i].temp_low_alarm ||
                alarms_[i].pressure_high_alarm) {
                return true;
            }
        }
        return false;
    }

    /**
     * Get alarm state for a fermenter
     */
    const FermenterAlarmState* get_alarm_state(uint8_t fermenter_id) const {
        if (!core::is_valid_fermenter_id(fermenter_id)) {
            return nullptr;
        }
        return &alarms_[core::fermenter_id_to_index(fermenter_id)];
    }

    /**
     * Clear alarms for a fermenter
     */
    void clear_alarms(uint8_t fermenter_id) {
        if (!core::is_valid_fermenter_id(fermenter_id)) {
            return;
        }
        alarms_[core::fermenter_id_to_index(fermenter_id)] = FermenterAlarmState();
    }

    /**
     * Execute emergency shutdown
     * Turns off all heating, opens spunding valves
     */
    void emergency_shutdown() {
        // Turn off glycol chiller (relay 0 typically)
        // Open all spunding valves
        // This would be implemented based on actual relay mappings

        // Publish alarm event
        if (events_) {
            core::Event event;
            event.type = core::EventType::ALARM;
            event.source_id = 0;  // System alarm
            event.timestamp = time_->millis();
            events_->publish(event);
        }
    }

private:
    hal::ITimeInterface* time_;
    hal::IGPIOInterface* gpio_;
    core::StateManager* state_;
    core::EventBus* events_;

    float max_temp_deviation_;
    float max_pressure_bar_;
    uint32_t temp_deviation_timeout_ms_;
    uint32_t alarm_cooldown_ms_;

    bool override_active_;
    uint32_t override_timeout_;

    FermenterAlarmState alarms_[core::MAX_FERMENTERS];

    void check_fermenter(uint8_t id, core::FermenterState* ferm, uint32_t now) {
        FermenterAlarmState& alarm = alarms_[core::fermenter_id_to_index(id)];

        // Skip if in cooldown
        if (now - alarm.last_alarm_time < alarm_cooldown_ms_ &&
            alarm.last_alarm_time > 0) {
            return;
        }

        // Get sensor states
        auto* temp_sensor = state_->get_sensor_by_id(ferm->temp_sensor_id);
        auto* pressure_sensor = state_->get_sensor_by_id(ferm->pressure_sensor_id);

        // Check sensor failures
        bool temp_sensor_ok = temp_sensor &&
                              temp_sensor->quality == core::SensorQuality::GOOD;
        bool pressure_sensor_ok = pressure_sensor &&
                                  pressure_sensor->quality == core::SensorQuality::GOOD;

        if (!temp_sensor_ok || !pressure_sensor_ok) {
            if (!alarm.sensor_failure_alarm) {
                alarm.sensor_failure_alarm = true;
                alarm.last_alarm_time = now;
                publish_alarm(id, core::AlarmSeverity::ERROR, "Sensor failure");
            }
            return;  // Can't check other limits without valid sensors
        } else {
            alarm.sensor_failure_alarm = false;
        }

        // Check temperature deviation
        if (ferm->mode == core::FermenterMode::PLAN ||
            ferm->mode == core::FermenterMode::MANUAL) {

            float deviation = ferm->current_temp - ferm->target_temp;

            if (deviation > max_temp_deviation_) {
                // Temperature too high
                if (alarm.temp_deviation_start == 0) {
                    alarm.temp_deviation_start = now;
                } else if (now - alarm.temp_deviation_start > temp_deviation_timeout_ms_) {
                    if (!alarm.temp_high_alarm) {
                        alarm.temp_high_alarm = true;
                        alarm.last_alarm_time = now;
                        publish_alarm(id, core::AlarmSeverity::WARNING,
                                    "Temperature too high");
                    }
                }
            } else if (deviation < -max_temp_deviation_) {
                // Temperature too low
                if (alarm.temp_deviation_start == 0) {
                    alarm.temp_deviation_start = now;
                } else if (now - alarm.temp_deviation_start > temp_deviation_timeout_ms_) {
                    if (!alarm.temp_low_alarm) {
                        alarm.temp_low_alarm = true;
                        alarm.last_alarm_time = now;
                        publish_alarm(id, core::AlarmSeverity::WARNING,
                                    "Temperature too low");
                    }
                }
            } else {
                // Temperature OK, reset deviation timer
                alarm.temp_deviation_start = 0;
                alarm.temp_high_alarm = false;
                alarm.temp_low_alarm = false;
            }
        }

        // Check pressure limit
        if (ferm->current_pressure > max_pressure_bar_) {
            if (!alarm.pressure_high_alarm) {
                alarm.pressure_high_alarm = true;
                alarm.last_alarm_time = now;
                publish_alarm(id, core::AlarmSeverity::CRITICAL,
                            "Pressure too high - opening spunding valve");

                // Emergency action: open spunding valve
                uint8_t spunding_id = ferm->spunding_relay_id;
                if (spunding_id != 0xFF) {
                    state_->set_relay_state(spunding_id, true, now);
                    if (events_) {
                        events_->publish_relay_change(spunding_id, true, now);
                    }
                }
            }
        } else if (ferm->current_pressure < max_pressure_bar_ - 0.2f) {
            // Hysteresis: clear alarm when 0.2 bar below limit
            alarm.pressure_high_alarm = false;
        }
    }

    void publish_alarm(uint8_t fermenter_id, core::AlarmSeverity severity,
                      const char* message) {
        if (!events_) return;

        // Create and publish alarm
        core::Alarm alarm;
        alarm.severity = severity;
        alarm.source_id = fermenter_id;
        alarm.timestamp = time_->millis();
        core::safe_strncpy(alarm.message, message, sizeof(alarm.message));

        // Publish generic alarm event
        events_->publish_alarm(fermenter_id, alarm.timestamp);
    }
};

} // namespace modules
