#pragma once

#include "hal/interfaces.h"
#include "core/types.h"
#include "core/config.h"
#include "core/state_manager.h"
#include "core/event_bus.h"
#include "core/utils.h"
#include "modules/filters.h"
#include <memory>

namespace modules {

/**
 * Scheduled transaction for poll scheduler
 */
struct ScheduledTransaction {
    uint32_t time_ms;           // Time within cycle
    uint8_t device_addr;
    uint16_t start_reg;
    uint16_t count;
    bool is_bulk;               // True for bulk read, false for single
    uint8_t sensor_ids[16];     // Sensor IDs for this transaction
    uint8_t sensor_count;
};

/**
 * Sensor configuration for MODBUS module
 */
struct ModbusSensorConfig {
    char name[32];
    uint8_t device_addr;
    uint16_t reg;
    float scale;
    float offset;
    core::SensorPriority priority;
    uint8_t extra_samples_per_second;
    core::FilterType filter_type;
    float filter_alpha;
    uint8_t sensor_id;          // ID in state manager
    uint16_t min_raw;           // Minimum valid raw value (4mA threshold)
    uint16_t max_raw;           // Maximum valid raw value (20mA threshold)
};

/**
 * MODBUS Module
 * Handles polling of MODBUS devices and sensor value processing
 */
class ModbusModule {
public:
    static constexpr uint8_t MAX_SENSORS = 32;
    static constexpr uint8_t MAX_TRANSACTIONS = 64;

    ModbusModule(hal::IModbusInterface* modbus,
                 hal::ITimeInterface* time,
                 core::StateManager* state,
                 core::EventBus* events)
        : modbus_(modbus)
        , time_(time)
        , state_(state)
        , events_(events)
        , sensor_count_(0)
        , transaction_count_(0)
        , cycle_start_time_(0)
        , current_transaction_(0) {}

    /**
     * Initialize module with configuration
     */
    bool initialize(const core::SystemConfig& config) {
        // Clear existing config
        sensor_count_ = 0;
        transaction_count_ = 0;

        // Store timing config
        base_cycle_ms_ = config.scheduler.base_cycle_ms;
        base_samples_per_cycle_ = config.scheduler.base_samples_per_cycle;
        min_sample_spacing_ms_ = config.scheduler.min_sample_spacing_ms;
        bulk_read_enabled_ = config.scheduler.bulk_read_enabled;

        // Register sensors from MODBUS devices
        for (uint8_t d = 0; d < config.hardware.modbus_device_count; d++) {
            const auto& device = config.hardware.modbus_devices[d];

            for (uint8_t r = 0; r < device.register_count; r++) {
                if (sensor_count_ >= MAX_SENSORS) break;

                const auto& reg = device.registers[r];

                ModbusSensorConfig& sensor = sensors_[sensor_count_];
                core::safe_strncpy(sensor.name, reg.name, sizeof(sensor.name));
                sensor.device_addr = device.address;
                sensor.reg = reg.reg;
                sensor.scale = reg.scale;
                sensor.offset = 0;
                sensor.priority = reg.priority;
                sensor.extra_samples_per_second = reg.extra_samples_per_second;
                sensor.filter_type = reg.filter;
                sensor.filter_alpha = reg.filter_alpha;
                sensor.min_raw = reg.min_raw;
                sensor.max_raw = reg.max_raw;

                // Get sensor ID from state manager
                sensor.sensor_id = state_->get_sensor_id(reg.name);

                // Create filter for this sensor
                create_filter(sensor_count_);

                sensor_count_++;
            }
        }

        // Build poll schedule
        build_schedule();

        return true;
    }

    /**
     * Execute one poll cycle
     * Call this from main loop or timer task
     */
    void poll_cycle() {
        if (sensor_count_ == 0) return;

        cycle_start_time_ = time_->millis();
        current_transaction_ = 0;

        // Execute all scheduled transactions
        while (current_transaction_ < transaction_count_) {
            ScheduledTransaction& trans = schedule_[current_transaction_];

            // Wait for scheduled time
            uint32_t elapsed = time_->millis() - cycle_start_time_;
            if (elapsed < trans.time_ms) {
                time_->delay_ms(trans.time_ms - elapsed);
            }

            // Execute transaction
            execute_transaction(trans);

            current_transaction_++;
        }
    }

    /**
     * Get sensor value by name
     */
    float get_sensor_value(const char* name) const {
        for (uint8_t i = 0; i < sensor_count_; i++) {
            if (strcmp(sensors_[i].name, name) == 0) {
                if (filters_[i]) {
                    return filters_[i]->get_value();
                }
            }
        }
        return 0;
    }

    /**
     * Get sensor quality by name
     */
    core::SensorQuality get_sensor_quality(const char* name) const {
        for (uint8_t i = 0; i < sensor_count_; i++) {
            if (strcmp(sensors_[i].name, name) == 0) {
                auto* sensor = state_->get_sensor_by_id(sensors_[i].sensor_id);
                if (sensor) return sensor->quality;
            }
        }
        return core::SensorQuality::UNKNOWN;
    }

    uint8_t get_sensor_count() const { return sensor_count_; }
    uint8_t get_transaction_count() const { return transaction_count_; }

private:
    hal::IModbusInterface* modbus_;
    hal::ITimeInterface* time_;
    core::StateManager* state_;
    core::EventBus* events_;

    ModbusSensorConfig sensors_[MAX_SENSORS];
    std::unique_ptr<IFilter> filters_[MAX_SENSORS];
    uint8_t sensor_count_;

    ScheduledTransaction schedule_[MAX_TRANSACTIONS];
    uint8_t transaction_count_;

    uint32_t base_cycle_ms_;
    uint8_t base_samples_per_cycle_;
    uint32_t min_sample_spacing_ms_;
    bool bulk_read_enabled_;

    uint32_t cycle_start_time_;
    uint8_t current_transaction_;

    void create_filter(uint8_t sensor_idx) {
        const auto& sensor = sensors_[sensor_idx];

        switch (sensor.filter_type) {
            case core::FilterType::EMA:
                filters_[sensor_idx] = std::make_unique<EMAFilter>(sensor.filter_alpha);
                break;
            case core::FilterType::MOVING_AVERAGE:
                filters_[sensor_idx] = std::make_unique<MovingAverageFilter>(5);
                break;
            case core::FilterType::MEDIAN:
                filters_[sensor_idx] = std::make_unique<MedianFilter>(5);
                break;
            default:
                filters_[sensor_idx] = std::make_unique<NoFilter>();
                break;
        }
    }

    void build_schedule() {
        transaction_count_ = 0;

        // Calculate base sample times
        uint32_t base_interval = base_cycle_ms_ / base_samples_per_cycle_;

        // Group sensors by device address for bulk reads
        struct DeviceGroup {
            uint8_t device_addr;
            uint8_t sensor_indices[16];
            uint8_t count;
            uint16_t min_reg;
            uint16_t max_reg;
        };

        DeviceGroup groups[8];
        uint8_t group_count = 0;

        // Build device groups
        for (uint8_t i = 0; i < sensor_count_; i++) {
            uint8_t addr = sensors_[i].device_addr;

            // Find or create group
            int group_idx = -1;
            for (uint8_t g = 0; g < group_count; g++) {
                if (groups[g].device_addr == addr) {
                    group_idx = g;
                    break;
                }
            }

            if (group_idx < 0 && group_count < 8) {
                group_idx = group_count++;
                groups[group_idx].device_addr = addr;
                groups[group_idx].count = 0;
                groups[group_idx].min_reg = 0xFFFF;
                groups[group_idx].max_reg = 0;
            }

            if (group_idx >= 0 && groups[group_idx].count < 16) {
                groups[group_idx].sensor_indices[groups[group_idx].count++] = i;
                if (sensors_[i].reg < groups[group_idx].min_reg) {
                    groups[group_idx].min_reg = sensors_[i].reg;
                }
                if (sensors_[i].reg > groups[group_idx].max_reg) {
                    groups[group_idx].max_reg = sensors_[i].reg;
                }
            }
        }

        // Schedule base samples (bulk reads)
        for (uint8_t sample = 0; sample < base_samples_per_cycle_; sample++) {
            uint32_t sample_time = sample * base_interval;

            for (uint8_t g = 0; g < group_count; g++) {
                if (transaction_count_ >= MAX_TRANSACTIONS) break;

                ScheduledTransaction& trans = schedule_[transaction_count_++];
                trans.time_ms = sample_time;
                trans.device_addr = groups[g].device_addr;
                trans.start_reg = groups[g].min_reg;
                uint16_t reg_count = groups[g].max_reg - groups[g].min_reg + 1;
                // Clamp to maximum buffer size
                trans.count = (reg_count > 16) ? 16 : reg_count;
                trans.is_bulk = bulk_read_enabled_;
                trans.sensor_count = (groups[g].count > 16) ? 16 : groups[g].count;

                for (uint8_t s = 0; s < trans.sensor_count && s < 16; s++) {
                    trans.sensor_ids[s] = groups[g].sensor_indices[s];
                }

                // Offset next device slightly
                sample_time += 5;  // 5ms per transaction
            }
        }

        // Schedule extra samples in idle windows
        // Simple round-robin distribution for now
        uint32_t extra_time = 15;  // Start after first bulk reads

        for (uint8_t i = 0; i < sensor_count_; i++) {
            uint8_t extras = sensors_[i].extra_samples_per_second;
            if (extras == 0) continue;

            for (uint8_t e = 0; e < extras && transaction_count_ < MAX_TRANSACTIONS; e++) {
                ScheduledTransaction& trans = schedule_[transaction_count_++];

                // Find next available slot
                trans.time_ms = extra_time;
                trans.device_addr = sensors_[i].device_addr;
                trans.start_reg = sensors_[i].reg;
                trans.count = 1;
                trans.is_bulk = false;
                trans.sensor_count = 1;
                trans.sensor_ids[0] = i;

                extra_time += 5;

                // Wrap around if needed, skip base sample times
                if (extra_time >= base_cycle_ms_) {
                    extra_time = 15;
                }
            }
        }

        // Sort schedule by time
        core::bubble_sort(schedule_, transaction_count_,
            [](const ScheduledTransaction& a, const ScheduledTransaction& b) {
                return a.time_ms > b.time_ms;
            });
    }

    void execute_transaction(ScheduledTransaction& trans) {
        uint16_t data[16];

        // Validate transaction parameters to prevent buffer overflow
        if (trans.count == 0 || trans.count > 16) {
            // Invalid register count - mark sensors as bad
            for (uint8_t s = 0; s < trans.sensor_count; s++) {
                if (trans.sensor_ids[s] < sensor_count_) {
                    state_->set_sensor_quality(sensors_[trans.sensor_ids[s]].sensor_id,
                                              core::SensorQuality::BAD);
                }
            }
            return;
        }

        if (trans.sensor_count > 16) {
            trans.sensor_count = 16;  // Clamp to maximum
        }

        bool success = modbus_->read_holding_registers(
            trans.device_addr,
            trans.start_reg,
            trans.count,
            data
        );

        uint32_t timestamp = time_->millis();

        if (success) {
            // Process each sensor in this transaction
            for (uint8_t s = 0; s < trans.sensor_count; s++) {
                uint8_t idx = trans.sensor_ids[s];

                // Bounds check sensor index
                if (idx >= sensor_count_) {
                    continue;  // Skip invalid sensor index
                }

                const auto& sensor = sensors_[idx];

                // Calculate register offset within bulk read
                uint16_t reg_offset = sensor.reg - trans.start_reg;
                if (reg_offset >= trans.count) continue;

                uint16_t raw_modbus = data[reg_offset];

                // Check 4-20mA range validity
                if (raw_modbus < sensor.min_raw) {
                    // Wire break detected (<4mA)
                    state_->set_sensor_quality(sensor.sensor_id, core::SensorQuality::BAD);
                    if (events_) {
                        events_->publish_alarm(sensor.sensor_id, timestamp);
                    }
                    continue;
                }
                if (raw_modbus > sensor.max_raw) {
                    // Sensor fault or over-range (>20mA)
                    state_->set_sensor_quality(sensor.sensor_id, core::SensorQuality::BAD);
                    if (events_) {
                        events_->publish_alarm(sensor.sensor_id, timestamp);
                    }
                    continue;
                }

                // Convert to engineering units
                float raw = raw_modbus * sensor.scale + sensor.offset;

                // Apply filter
                float filtered = raw;
                if (filters_[idx]) {
                    filtered = filters_[idx]->update(raw);
                }

                // Update state manager
                state_->update_sensor_value(sensor.sensor_id, raw, timestamp);
                state_->update_sensor_filtered(sensor.sensor_id, filtered, filtered);
                state_->set_sensor_quality(sensor.sensor_id, core::SensorQuality::GOOD);

                // Publish event
                if (events_) {
                    events_->publish_sensor_update(sensor.sensor_id, filtered, timestamp);
                }
            }
        } else {
            // Mark sensors as bad quality
            for (uint8_t s = 0; s < trans.sensor_count; s++) {
                uint8_t idx = trans.sensor_ids[s];
                // Bounds check sensor index
                if (idx < sensor_count_) {
                    state_->set_sensor_quality(sensors_[idx].sensor_id, core::SensorQuality::BAD);
                }
            }
        }
    }
};

} // namespace modules
