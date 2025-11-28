#include <unity.h>
#include "hal/simulator/hal_simulator.h"
#include "core/state_manager.h"
#include "core/event_bus.h"
#include "core/config_loader.h"
#include "modules/rest_api.h"
#include "modules/fermentation_plan.h"
#include "modules/safety_controller.h"

using namespace hal::simulator;
using namespace core;
using namespace modules;

// Test fixtures
static SimulatorStorage storage;
static SimulatorTime sim_time;
static SimulatorGPIO gpio;
static StateManager state;
static EventBus events;
static SystemConfig config;

void setUp(void) {
    storage.reset();
    sim_time.set_millis(0);
    sim_time.set_unix_time(1700000000);
    ConfigLoader::load_defaults(config);
}

void tearDown(void) {}

// REST API tests

void test_api_get_sensors() {
    state.register_sensor("test_sensor", "°C", 0.1f);
    state.update_sensor_value(0, 18.5f, 1000);
    state.update_sensor_filtered(0, 18.5f, 18.5f);
    state.set_sensor_quality(0, SensorQuality::GOOD);

    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    HttpResponse response;
    api.handle_request(HttpMethod::GET, "/sensors", nullptr, response);

    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(strstr(response.body, "test_sensor"));
    TEST_ASSERT_NOT_NULL(strstr(response.body, "18.5"));
}

void test_api_get_single_sensor() {
    state.register_sensor("my_sensor", "bar", 0.01f);
    state.update_sensor_value(0, 1.5f, 2000);
    state.update_sensor_filtered(0, 1.5f, 1.5f);

    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    HttpResponse response;
    api.handle_request(HttpMethod::GET, "/sensors/my_sensor", nullptr, response);

    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(strstr(response.body, "my_sensor"));
}

void test_api_sensor_not_found() {
    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    HttpResponse response;
    api.handle_request(HttpMethod::GET, "/sensors/nonexistent", nullptr, response);

    TEST_ASSERT_EQUAL(404, response.status_code);
}

void test_api_get_relays() {
    state.register_relay("chiller", RelayType::CONTACTOR_COIL);
    state.register_relay("valve", RelayType::SOLENOID_NC);

    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    HttpResponse response;
    api.handle_request(HttpMethod::GET, "/relays", nullptr, response);

    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(strstr(response.body, "chiller"));
    TEST_ASSERT_NOT_NULL(strstr(response.body, "valve"));
}

void test_api_relay_control() {
    state.register_relay("test_relay", RelayType::SOLENOID_NC);

    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    // Turn on
    HttpResponse response;
    api.handle_request(HttpMethod::POST, "/relays/test_relay/on", nullptr, response);

    TEST_ASSERT_EQUAL(200, response.status_code);
    auto* relay = state.get_relay("test_relay");
    TEST_ASSERT_TRUE(relay->state);

    // Turn off
    api.handle_request(HttpMethod::POST, "/relays/test_relay/off", nullptr, response);
    TEST_ASSERT_FALSE(relay->state);
}

void test_api_get_fermenters() {
    // Setup fermenter
    state.register_sensor("f1_temp", "°C");
    state.register_sensor("f1_pressure", "bar");
    state.register_relay("f1_cooling", RelayType::SOLENOID_NC);
    state.register_relay("f1_spunding", RelayType::SOLENOID_NO);

    FermenterDef def;
    def.id = 1;
    strncpy(def.name, "F1", MAX_NAME_LENGTH);
    strncpy(def.temp_sensor, "f1_temp", MAX_NAME_LENGTH);
    strncpy(def.pressure_sensor, "f1_pressure", MAX_NAME_LENGTH);
    strncpy(def.cooling_relay, "f1_cooling", MAX_NAME_LENGTH);
    strncpy(def.spunding_relay, "f1_spunding", MAX_NAME_LENGTH);
    state.register_fermenter(def);

    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    HttpResponse response;
    api.handle_request(HttpMethod::GET, "/fermenters", nullptr, response);

    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(strstr(response.body, "F1"));
}

void test_api_set_setpoint() {
    // Setup fermenter
    state.register_sensor("f2_temp", "°C");
    state.register_sensor("f2_pressure", "bar");
    state.register_relay("f2_cooling", RelayType::SOLENOID_NC);
    state.register_relay("f2_spunding", RelayType::SOLENOID_NO);

    FermenterDef def;
    def.id = 2;
    strncpy(def.name, "F2", MAX_NAME_LENGTH);
    strncpy(def.temp_sensor, "f2_temp", MAX_NAME_LENGTH);
    strncpy(def.pressure_sensor, "f2_pressure", MAX_NAME_LENGTH);
    strncpy(def.cooling_relay, "f2_cooling", MAX_NAME_LENGTH);
    strncpy(def.spunding_relay, "f2_spunding", MAX_NAME_LENGTH);
    state.register_fermenter(def);

    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    HttpResponse response;
    api.handle_request(HttpMethod::PUT, "/fermenters/2/setpoint",
                      "{\"temperature\":15.5}", response);

    TEST_ASSERT_EQUAL(200, response.status_code);

    auto* ferm = state.get_fermenter(2);
    TEST_ASSERT_EQUAL_FLOAT(15.5f, ferm->target_temp);
    TEST_ASSERT_EQUAL(FermenterMode::MANUAL, ferm->mode);
}

void test_api_get_system_status() {
    state.update_system_uptime(3600);
    state.update_system_ntp_status(true);
    state.update_wifi_rssi(-65);

    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    HttpResponse response;
    api.handle_request(HttpMethod::GET, "/system/status", nullptr, response);

    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(strstr(response.body, "3600"));
    TEST_ASSERT_NOT_NULL(strstr(response.body, "true"));
}

void test_api_endpoint_not_found() {
    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    HttpResponse response;
    api.handle_request(HttpMethod::GET, "/invalid/endpoint", nullptr, response);

    TEST_ASSERT_EQUAL(404, response.status_code);
}

// #53 - REST API path suffix matching tests
// Relay names containing "on" or "off" substrings should work correctly

void test_api_relay_name_containing_on() {
    // Register relay with "on" in the name
    state.register_relay("heater_control", RelayType::SOLENOID_NC);

    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    // Turn on the relay
    HttpResponse response;
    api.handle_request(HttpMethod::POST, "/relays/heater_control/on", nullptr, response);

    TEST_ASSERT_EQUAL(200, response.status_code);
    auto* relay = state.get_relay("heater_control");
    TEST_ASSERT_TRUE(relay->state);

    // Turn off the relay
    api.handle_request(HttpMethod::POST, "/relays/heater_control/off", nullptr, response);
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_FALSE(relay->state);
}

void test_api_relay_name_ending_with_on() {
    // Register relay whose name ends with "on" (tricky case)
    state.register_relay("fan_option", RelayType::SOLENOID_NC);

    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    // Turn on the relay - should match /fan_option/on not just find "on" anywhere
    HttpResponse response;
    api.handle_request(HttpMethod::POST, "/relays/fan_option/on", nullptr, response);

    TEST_ASSERT_EQUAL(200, response.status_code);
    auto* relay = state.get_relay("fan_option");
    TEST_ASSERT_TRUE(relay->state);
}

void test_api_relay_name_containing_off() {
    // Register relay with "off" in the name
    state.register_relay("coffee_maker", RelayType::SOLENOID_NC);

    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    // Turn on the relay
    HttpResponse response;
    api.handle_request(HttpMethod::POST, "/relays/coffee_maker/on", nullptr, response);

    TEST_ASSERT_EQUAL(200, response.status_code);
    auto* relay = state.get_relay("coffee_maker");
    TEST_ASSERT_TRUE(relay->state);

    // Turn off the relay
    api.handle_request(HttpMethod::POST, "/relays/coffee_maker/off", nullptr, response);
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_FALSE(relay->state);
}

void test_api_relay_name_with_on_off_substring() {
    // Edge case: relay name contains both "on" and "off"
    state.register_relay("confounding", RelayType::SOLENOID_NC);

    FermentationPlanManager plans(&sim_time, &storage, &state, &events);
    RestApiHandler api(&state, &events, &plans, &config);

    // Should correctly parse the /on suffix
    HttpResponse response;
    api.handle_request(HttpMethod::POST, "/relays/confounding/on", nullptr, response);

    TEST_ASSERT_EQUAL(200, response.status_code);
    auto* relay = state.get_relay("confounding");
    TEST_ASSERT_TRUE(relay->state);

    // Should correctly parse the /off suffix
    api.handle_request(HttpMethod::POST, "/relays/confounding/off", nullptr, response);
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_FALSE(relay->state);
}

// Safety Controller tests

void test_safety_no_alarms_initially() {
    SafetyController safety(&sim_time, &gpio, &state, &events);

    TEST_ASSERT_FALSE(safety.has_active_alarms());
}

void test_safety_pressure_alarm() {
    // Setup fermenter with sensors
    state.register_sensor("sf_temp", "°C");
    state.register_sensor("sf_pressure", "bar");
    state.register_relay("sf_cooling", RelayType::SOLENOID_NC);
    state.register_relay("sf_spunding", RelayType::SOLENOID_NO);

    FermenterDef def;
    def.id = 5;  // Use unique ID to avoid collision with other tests
    strncpy(def.name, "SF", MAX_NAME_LENGTH);
    strncpy(def.temp_sensor, "sf_temp", MAX_NAME_LENGTH);
    strncpy(def.pressure_sensor, "sf_pressure", MAX_NAME_LENGTH);
    strncpy(def.cooling_relay, "sf_cooling", MAX_NAME_LENGTH);
    strncpy(def.spunding_relay, "sf_spunding", MAX_NAME_LENGTH);
    state.register_fermenter(def);

    // Set sensors as good (use actual sensor IDs)
    uint8_t temp_id = state.get_sensor_id("sf_temp");
    uint8_t pressure_id = state.get_sensor_id("sf_pressure");
    state.set_sensor_quality(temp_id, SensorQuality::GOOD);
    state.set_sensor_quality(pressure_id, SensorQuality::GOOD);

    // Set high pressure
    state.update_fermenter_pressure(5, 3.0f, 1.0f);  // 3 bar > 2.5 limit

    SafetyController safety(&sim_time, &gpio, &state, &events);
    safety.check();

    TEST_ASSERT_TRUE(safety.has_active_alarms());

    const auto* alarm = safety.get_alarm_state(5);
    TEST_ASSERT_NOT_NULL(alarm);
    TEST_ASSERT_TRUE(alarm->pressure_high_alarm);
}

void test_safety_sensor_failure() {
    // Setup fermenter
    state.register_sensor("fail_temp", "°C");
    state.register_sensor("fail_pressure", "bar");
    state.register_relay("fail_cooling", RelayType::SOLENOID_NC);
    state.register_relay("fail_spunding", RelayType::SOLENOID_NO);

    FermenterDef def;
    def.id = 3;
    strncpy(def.name, "Fail", MAX_NAME_LENGTH);
    strncpy(def.temp_sensor, "fail_temp", MAX_NAME_LENGTH);
    strncpy(def.pressure_sensor, "fail_pressure", MAX_NAME_LENGTH);
    strncpy(def.cooling_relay, "fail_cooling", MAX_NAME_LENGTH);
    strncpy(def.spunding_relay, "fail_spunding", MAX_NAME_LENGTH);
    state.register_fermenter(def);

    // Set sensor as BAD
    state.set_sensor_quality(state.get_sensor_id("fail_temp"), SensorQuality::BAD);

    SafetyController safety(&sim_time, &gpio, &state, &events);
    safety.check();

    const auto* alarm = safety.get_alarm_state(3);
    TEST_ASSERT_NOT_NULL(alarm);
    TEST_ASSERT_TRUE(alarm->sensor_failure_alarm);
}

void test_safety_override() {
    SafetyController safety(&sim_time, &gpio, &state, &events);

    // Enable override for 10 seconds
    safety.enable_override(10000);

    // Even with dangerous conditions, no alarms during override
    safety.check();
    TEST_ASSERT_FALSE(safety.has_active_alarms());

    // After timeout, checks resume
    sim_time.advance_millis(15000);
    safety.check();
    // Would trigger alarms if conditions are bad
}

void test_safety_clear_alarms() {
    SafetyController safety(&sim_time, &gpio, &state, &events);

    // Manually clear alarms
    safety.clear_alarms(1);

    const auto* alarm = safety.get_alarm_state(1);
    TEST_ASSERT_FALSE(alarm->temp_high_alarm);
    TEST_ASSERT_FALSE(alarm->pressure_high_alarm);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // REST API tests
    RUN_TEST(test_api_get_sensors);
    RUN_TEST(test_api_get_single_sensor);
    RUN_TEST(test_api_sensor_not_found);
    RUN_TEST(test_api_get_relays);
    RUN_TEST(test_api_relay_control);
    RUN_TEST(test_api_get_fermenters);
    RUN_TEST(test_api_set_setpoint);
    RUN_TEST(test_api_get_system_status);
    RUN_TEST(test_api_endpoint_not_found);

    // Path suffix matching tests (#53)
    RUN_TEST(test_api_relay_name_containing_on);
    RUN_TEST(test_api_relay_name_ending_with_on);
    RUN_TEST(test_api_relay_name_containing_off);
    RUN_TEST(test_api_relay_name_with_on_off_substring);

    // Safety Controller tests
    RUN_TEST(test_safety_no_alarms_initially);
    RUN_TEST(test_safety_pressure_alarm);
    RUN_TEST(test_safety_sensor_failure);
    RUN_TEST(test_safety_override);
    RUN_TEST(test_safety_clear_alarms);

    return UNITY_END();
}
