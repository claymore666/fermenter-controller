#include "core/event_bus.h"

namespace core {

// Global instance
static EventBus g_event_bus;

EventBus& get_event_bus() {
    return g_event_bus;
}

EventBus::EventBus() : subscriber_count_(0) {
#ifdef ESP32_BUILD
    mutex_ = xSemaphoreCreateMutex();
#endif
}

EventBus::~EventBus() {
#ifdef ESP32_BUILD
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
#endif
}

void EventBus::lock() {
#ifdef ESP32_BUILD
    xSemaphoreTake(mutex_, portMAX_DELAY);
#else
    mutex_.lock();
#endif
}

void EventBus::unlock() {
#ifdef ESP32_BUILD
    xSemaphoreGive(mutex_);
#else
    mutex_.unlock();
#endif
}

int EventBus::subscribe(EventType type, EventCallback callback) {
    lock();

    // Find empty slot
    int slot = -1;
    for (uint8_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!subscribers_[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        unlock();
        return -1; // No slots available
    }

    subscribers_[slot].type = type;
    subscribers_[slot].callback = callback;
    subscribers_[slot].active = true;
    subscriber_count_++;

    unlock();
    return slot;
}

void EventBus::unsubscribe(int subscription_id) {
    if (subscription_id < 0 || subscription_id >= MAX_SUBSCRIBERS) {
        return;
    }

    lock();
    if (subscribers_[subscription_id].active) {
        subscribers_[subscription_id].active = false;
        subscribers_[subscription_id].callback = nullptr;
        if (subscriber_count_ > 0) {
            subscriber_count_--;
        }
    }
    unlock();
}

void EventBus::publish(const Event& event) {
    lock();

    // Copy subscribers to avoid holding lock during callbacks
    Subscriber local_subs[MAX_SUBSCRIBERS];
    uint8_t count = 0;

    for (uint8_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (subscribers_[i].active && subscribers_[i].type == event.type) {
            local_subs[count++] = subscribers_[i];
        }
    }

    unlock();

    // Call subscribers outside of lock
    for (uint8_t i = 0; i < count; i++) {
        if (local_subs[i].callback) {
            local_subs[i].callback(event);
        }
    }
}

void EventBus::publish_sensor_update(uint8_t sensor_id, float value, uint32_t timestamp) {
    Event event;
    event.type = EventType::SENSOR_UPDATE;
    event.source_id = sensor_id;
    event.timestamp = timestamp;
    event.data.value = value;
    publish(event);
}

void EventBus::publish_relay_change(uint8_t relay_id, bool state, uint32_t timestamp) {
    Event event;
    event.type = EventType::RELAY_CHANGE;
    event.source_id = relay_id;
    event.timestamp = timestamp;
    event.data.state = state;
    publish(event);
}

void EventBus::publish_plan_step_change(uint8_t fermenter_id, uint8_t step, uint32_t timestamp) {
    Event event;
    event.type = EventType::PLAN_STEP_CHANGE;
    event.source_id = fermenter_id;
    event.timestamp = timestamp;
    event.data.step = step;
    publish(event);
}

void EventBus::publish_alarm(uint8_t source_id, uint32_t timestamp) {
    Event event;
    event.type = EventType::ALARM;
    event.source_id = source_id;
    event.timestamp = timestamp;
    publish(event);
}

uint8_t EventBus::get_subscriber_count() const {
    return subscriber_count_;
}

} // namespace core
