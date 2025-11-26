#pragma once

#include "hal/interfaces.h"
#include "core/state_manager.h"
#include "core/event_bus.h"

namespace modules {

/**
 * CAN bus module for external device communication
 *
 * Provides CAN message send/receive functionality with statistics tracking.
 * Can be extended for specific CAN protocols (CANopen, J1939, etc.)
 */
class CANModule {
public:
    struct Stats {
        uint32_t tx_count;
        uint32_t rx_count;
        uint32_t error_count;
        bool bus_ok;
    };

    CANModule(hal::ICANInterface* can, hal::ITimeInterface* time,
              core::StateManager* state, core::EventBus* events)
        : can_(can), time_(time), state_(state), events_(events),
          initialized_(false), bitrate_(500000) {}

    virtual ~CANModule() = default;

    /**
     * Initialize CAN module
     * @param bitrate CAN bitrate (125000, 250000, 500000, 1000000)
     */
    bool initialize(uint32_t bitrate = 500000) {
        if (!can_) return false;

        bitrate_ = bitrate;
        if (!can_->initialize(bitrate)) {
            return false;
        }

        initialized_ = true;
        return true;
    }

    /**
     * Send a CAN message
     */
    bool send(const hal::CANMessage& msg, uint32_t timeout_ms = 100) {
        if (!initialized_ || !can_) return false;
        return can_->send(msg, timeout_ms);
    }

    /**
     * Send a standard CAN message (convenience method)
     */
    bool send(uint32_t id, const uint8_t* data, uint8_t len, uint32_t timeout_ms = 100) {
        hal::CANMessage msg = {};
        msg.id = id;
        msg.len = len > 8 ? 8 : len;
        msg.extended = false;
        msg.rtr = false;
        for (int i = 0; i < msg.len; i++) {
            msg.data[i] = data[i];
        }
        return send(msg, timeout_ms);
    }

    /**
     * Receive a CAN message
     */
    bool receive(hal::CANMessage& msg, uint32_t timeout_ms = 0) {
        if (!initialized_ || !can_) return false;
        return can_->receive(msg, timeout_ms);
    }

    /**
     * Check if messages are available
     */
    bool available() const {
        if (!initialized_ || !can_) return false;
        return can_->available();
    }

    /**
     * Process received messages (call periodically)
     * Override in subclass for protocol-specific handling
     */
    virtual void process() {
        if (!initialized_ || !can_) return;

        // Process any available messages
        hal::CANMessage msg;
        while (can_->receive(msg, 0)) {
            handle_message(msg);
        }
    }

    /**
     * Get module statistics
     */
    Stats get_stats() const {
        Stats stats = {};
        if (can_) {
            stats.tx_count = can_->get_tx_count();
            stats.rx_count = can_->get_rx_count();
            stats.error_count = can_->get_error_count();
            // Consider bus OK if we have more successful messages than errors
            stats.bus_ok = (stats.tx_count + stats.rx_count) >= stats.error_count;
        }
        return stats;
    }

    /**
     * Check if CAN bus is operational
     */
    bool is_bus_ok() const {
        if (!initialized_ || !can_) return false;
        // Simple heuristic: OK if error rate is below 10%
        uint32_t total = can_->get_tx_count() + can_->get_rx_count();
        uint32_t errors = can_->get_error_count();
        if (total == 0) return true;  // No traffic yet
        return (errors * 10) < total;
    }

    bool is_initialized() const { return initialized_; }
    uint32_t get_bitrate() const { return bitrate_; }

    /**
     * Stop CAN module
     */
    void stop() {
        if (can_) {
            can_->stop();
        }
        initialized_ = false;
    }

protected:
    /**
     * Handle received message - override for protocol-specific handling
     */
    virtual void handle_message(const hal::CANMessage& msg) {
        (void)msg;  // Base implementation does nothing
    }

    hal::ICANInterface* can_;
    hal::ITimeInterface* time_;
    core::StateManager* state_;
    core::EventBus* events_;
    bool initialized_;
    uint32_t bitrate_;
};

} // namespace modules
