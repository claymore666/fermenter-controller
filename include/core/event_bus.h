#pragma once

#include "types.h"
#include <functional>

#ifdef ESP32_BUILD
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#else
#include <mutex>
#endif

namespace core {

// Callback type for event handlers
using EventCallback = std::function<void(const Event&)>;

/**
 * Subscriber registration
 */
struct Subscriber {
    EventType type;
    EventCallback callback;
    bool active;

    Subscriber() : type(EventType::SENSOR_UPDATE), callback(nullptr), active(false) {}
};

/**
 * Event Bus - Publish/Subscribe communication system
 * Decouples modules from direct dependencies
 */
class EventBus {
public:
    static constexpr uint8_t MAX_SUBSCRIBERS = 32;

    EventBus();
    ~EventBus();

    // Prevent copying
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    /**
     * Subscribe to an event type
     * @param type Event type to subscribe to
     * @param callback Function to call when event occurs
     * @return Subscription ID (for unsubscribe), or -1 on failure
     */
    int subscribe(EventType type, EventCallback callback);

    /**
     * Unsubscribe from events
     * @param subscription_id ID returned from subscribe()
     */
    void unsubscribe(int subscription_id);

    /**
     * Publish an event to all subscribers
     * @param event Event to publish
     */
    void publish(const Event& event);

    /**
     * Convenience method: publish sensor update
     */
    void publish_sensor_update(uint8_t sensor_id, float value, uint32_t timestamp);

    /**
     * Convenience method: publish relay change
     */
    void publish_relay_change(uint8_t relay_id, bool state, uint32_t timestamp);

    /**
     * Convenience method: publish plan step change
     */
    void publish_plan_step_change(uint8_t fermenter_id, uint8_t step, uint32_t timestamp);

    /**
     * Convenience method: publish alarm
     */
    void publish_alarm(uint8_t source_id, uint32_t timestamp);

    /**
     * Get number of active subscribers
     */
    uint8_t get_subscriber_count() const;

private:
    Subscriber subscribers_[MAX_SUBSCRIBERS];
    uint8_t subscriber_count_;

#ifdef ESP32_BUILD
    SemaphoreHandle_t mutex_;
#else
    std::mutex mutex_;
#endif

    void lock();
    void unlock();
};

// Global event bus instance
EventBus& get_event_bus();

} // namespace core
