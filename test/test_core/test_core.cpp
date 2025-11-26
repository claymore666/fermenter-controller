#include <unity.h>
#include "core/types.h"
#include "core/config.h"
#include "core/state_manager.h"
#include "core/event_bus.h"

using namespace core;

void setUp(void) {
    // Setup before each test
}

void tearDown(void) {
    // Cleanup after each test
}

// State Manager tests

void test_state_manager_register_sensor() {
    StateManager sm;

    bool result = sm.register_sensor("fermenter_1_temp", "°C", 0.1f);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, sm.get_sensor_count());

    auto* sensor = sm.get_sensor("fermenter_1_temp");
    TEST_ASSERT_NOT_NULL(sensor);
    TEST_ASSERT_EQUAL_STRING("fermenter_1_temp", sensor->name);
    TEST_ASSERT_EQUAL_STRING("°C", sensor->unit);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, sensor->scale);
}

void test_state_manager_sensor_by_id() {
    StateManager sm;

    sm.register_sensor("sensor_a", "°C");
    sm.register_sensor("sensor_b", "bar");
    sm.register_sensor("sensor_c", "°C");

    uint8_t id = sm.get_sensor_id("sensor_b");
    TEST_ASSERT_EQUAL(1, id);

    auto* sensor = sm.get_sensor_by_id(id);
    TEST_ASSERT_NOT_NULL(sensor);
    TEST_ASSERT_EQUAL_STRING("sensor_b", sensor->name);
}

void test_state_manager_update_sensor_value() {
    StateManager sm;

    sm.register_sensor("test_sensor", "°C");
    uint8_t id = sm.get_sensor_id("test_sensor");

    sm.update_sensor_value(id, 18.5f, 1000);

    auto* sensor = sm.get_sensor_by_id(id);
    TEST_ASSERT_EQUAL_FLOAT(18.5f, sensor->raw_value);
    TEST_ASSERT_EQUAL(1000, sensor->timestamp);
}

void test_state_manager_register_relay() {
    StateManager sm;

    bool result = sm.register_relay("glycol_chiller", RelayType::CONTACTOR_COIL, 5);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, sm.get_relay_count());

    auto* relay = sm.get_relay("glycol_chiller");
    TEST_ASSERT_NOT_NULL(relay);
    TEST_ASSERT_EQUAL_STRING("glycol_chiller", relay->name);
    TEST_ASSERT_EQUAL(5, relay->gpio_pin);
    TEST_ASSERT_FALSE(relay->state);
}

void test_state_manager_set_relay_state() {
    StateManager sm;

    sm.register_relay("test_relay", RelayType::SOLENOID_NC, 10);
    uint8_t id = sm.get_relay_id("test_relay");

    sm.set_relay_state(id, true, 2000);

    auto* relay = sm.get_relay_by_id(id);
    TEST_ASSERT_TRUE(relay->state);
    TEST_ASSERT_EQUAL(2000, relay->last_change);
}

void test_state_manager_register_fermenter() {
    StateManager sm;

    // First register sensors and relays
    sm.register_sensor("f1_temp", "°C");
    sm.register_sensor("f1_pressure", "bar");
    sm.register_relay("f1_cooling", RelayType::SOLENOID_NC);
    sm.register_relay("f1_spunding", RelayType::SOLENOID_NO);

    // Now register fermenter
    FermenterDef def;
    def.id = 1;
    strncpy(def.name, "F1", MAX_NAME_LENGTH);
    strncpy(def.temp_sensor, "f1_temp", MAX_NAME_LENGTH);
    strncpy(def.pressure_sensor, "f1_pressure", MAX_NAME_LENGTH);
    strncpy(def.cooling_relay, "f1_cooling", MAX_NAME_LENGTH);
    strncpy(def.spunding_relay, "f1_spunding", MAX_NAME_LENGTH);

    bool result = sm.register_fermenter(def);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, sm.get_fermenter_count());

    auto* ferm = sm.get_fermenter(1);
    TEST_ASSERT_NOT_NULL(ferm);
    TEST_ASSERT_EQUAL_STRING("F1", ferm->name);
    TEST_ASSERT_EQUAL(0, ferm->temp_sensor_id);
    TEST_ASSERT_EQUAL(1, ferm->pressure_sensor_id);
}

// Event Bus tests

void test_event_bus_subscribe_publish() {
    EventBus bus;
    bool callback_called = false;
    float received_value = 0;

    int sub_id = bus.subscribe(EventType::SENSOR_UPDATE, [&](const Event& e) {
        callback_called = true;
        received_value = e.data.value;
    });

    TEST_ASSERT_TRUE(sub_id >= 0);

    Event event;
    event.type = EventType::SENSOR_UPDATE;
    event.source_id = 1;
    event.data.value = 18.5f;

    bus.publish(event);

    TEST_ASSERT_TRUE(callback_called);
    TEST_ASSERT_EQUAL_FLOAT(18.5f, received_value);
}

void test_event_bus_unsubscribe() {
    EventBus bus;
    int call_count = 0;

    int sub_id = bus.subscribe(EventType::RELAY_CHANGE, [&](const Event& e) {
        (void)e;
        call_count++;
    });

    Event event;
    event.type = EventType::RELAY_CHANGE;

    bus.publish(event);
    TEST_ASSERT_EQUAL(1, call_count);

    bus.unsubscribe(sub_id);
    bus.publish(event);
    TEST_ASSERT_EQUAL(1, call_count); // Should not increment
}

void test_event_bus_multiple_subscribers() {
    EventBus bus;
    int count1 = 0, count2 = 0;

    bus.subscribe(EventType::ALARM, [&](const Event& e) {
        (void)e;
        count1++;
    });

    bus.subscribe(EventType::ALARM, [&](const Event& e) {
        (void)e;
        count2++;
    });

    Event event;
    event.type = EventType::ALARM;
    bus.publish(event);

    TEST_ASSERT_EQUAL(1, count1);
    TEST_ASSERT_EQUAL(1, count2);
}

void test_event_bus_filter_by_type() {
    EventBus bus;
    int sensor_count = 0, relay_count = 0;

    bus.subscribe(EventType::SENSOR_UPDATE, [&](const Event& e) {
        (void)e;
        sensor_count++;
    });

    bus.subscribe(EventType::RELAY_CHANGE, [&](const Event& e) {
        (void)e;
        relay_count++;
    });

    Event sensor_event;
    sensor_event.type = EventType::SENSOR_UPDATE;
    bus.publish(sensor_event);

    TEST_ASSERT_EQUAL(1, sensor_count);
    TEST_ASSERT_EQUAL(0, relay_count);

    Event relay_event;
    relay_event.type = EventType::RELAY_CHANGE;
    bus.publish(relay_event);

    TEST_ASSERT_EQUAL(1, sensor_count);
    TEST_ASSERT_EQUAL(1, relay_count);
}

void test_event_bus_convenience_methods() {
    EventBus bus;
    uint8_t received_id = 0;
    float received_value = 0;

    bus.subscribe(EventType::SENSOR_UPDATE, [&](const Event& e) {
        received_id = e.source_id;
        received_value = e.data.value;
    });

    bus.publish_sensor_update(5, 22.3f, 1000);

    TEST_ASSERT_EQUAL(5, received_id);
    TEST_ASSERT_EQUAL_FLOAT(22.3f, received_value);
}

// Data structure tests

void test_sensor_state_initialization() {
    SensorState sensor;

    TEST_ASSERT_EQUAL(SensorQuality::UNKNOWN, sensor.quality);
    TEST_ASSERT_EQUAL(FilterType::EMA, sensor.filter_type);
    TEST_ASSERT_EQUAL_FLOAT(0.3f, sensor.alpha);
}

void test_pid_params_defaults() {
    PIDParams params;

    TEST_ASSERT_EQUAL_FLOAT(2.0f, params.kp);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, params.ki);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, params.kd);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, params.output_min);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, params.output_max);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // State Manager tests
    RUN_TEST(test_state_manager_register_sensor);
    RUN_TEST(test_state_manager_sensor_by_id);
    RUN_TEST(test_state_manager_update_sensor_value);
    RUN_TEST(test_state_manager_register_relay);
    RUN_TEST(test_state_manager_set_relay_state);
    RUN_TEST(test_state_manager_register_fermenter);

    // Event Bus tests
    RUN_TEST(test_event_bus_subscribe_publish);
    RUN_TEST(test_event_bus_unsubscribe);
    RUN_TEST(test_event_bus_multiple_subscribers);
    RUN_TEST(test_event_bus_filter_by_type);
    RUN_TEST(test_event_bus_convenience_methods);

    // Data structure tests
    RUN_TEST(test_sensor_state_initialization);
    RUN_TEST(test_pid_params_defaults);

    return UNITY_END();
}
