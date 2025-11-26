#include <unity.h>
#include "hal/simulator/hal_simulator.h"
#include "core/state_manager.h"
#include "core/event_bus.h"
#include "core/config_loader.h"
#include "modules/modbus_module.h"
#include "modules/fermentation_plan.h"

using namespace hal::simulator;
using namespace core;
using namespace modules;

// Test fixtures
static SimulatorModbus modbus;
static SimulatorGPIO gpio;
static SimulatorStorage storage;
static SimulatorTime sim_time;
static StateManager state;
static EventBus events;

void setUp(void) {
    modbus.reset_counters();
    modbus.set_inject_error(false);
    storage.reset();
    sim_time.set_millis(0);
    sim_time.set_unix_time(1700000000);
}

void tearDown(void) {}

// MODBUS Module tests

void test_modbus_module_initialize() {
    SystemConfig config;
    ConfigLoader::load_defaults(config);

    // Add a MODBUS device
    config.hardware.modbus_device_count = 1;
    config.hardware.modbus_devices[0].address = 1;
    strncpy(config.hardware.modbus_devices[0].type, "pt1000_8ch", 16);
    config.hardware.modbus_devices[0].register_count = 2;

    strncpy(config.hardware.modbus_devices[0].registers[0].name, "temp1", 32);
    config.hardware.modbus_devices[0].registers[0].reg = 0;
    config.hardware.modbus_devices[0].registers[0].scale = 0.1f;

    strncpy(config.hardware.modbus_devices[0].registers[1].name, "temp2", 32);
    config.hardware.modbus_devices[0].registers[1].reg = 1;
    config.hardware.modbus_devices[0].registers[1].scale = 0.1f;

    // Register sensors in state manager
    state.register_sensor("temp1", "°C", 0.1f);
    state.register_sensor("temp2", "°C", 0.1f);

    ModbusModule module(&modbus, &sim_time, &state, &events);
    bool result = module.initialize(config);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(2, module.get_sensor_count());
    TEST_ASSERT_GREATER_THAN(0, module.get_transaction_count());
}

void test_modbus_module_poll_cycle() {
    SystemConfig config;
    ConfigLoader::load_defaults(config);

    // Add a MODBUS device with one sensor
    config.hardware.modbus_device_count = 1;
    config.hardware.modbus_devices[0].address = 1;
    config.hardware.modbus_devices[0].register_count = 1;

    strncpy(config.hardware.modbus_devices[0].registers[0].name, "test_temp", 32);
    config.hardware.modbus_devices[0].registers[0].reg = 0;
    config.hardware.modbus_devices[0].registers[0].scale = 0.1f;

    // Register sensor
    state.register_sensor("test_temp", "°C", 0.1f);

    // Set simulator value (185 = 18.5°C with 0.1 scale)
    modbus.set_register(1, 0, 185);

    ModbusModule module(&modbus, &sim_time, &state, &events);
    module.initialize(config);

    // Execute poll cycle
    module.poll_cycle();

    // Check value was read
    float value = module.get_sensor_value("test_temp");
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 18.5f, value);

    // Check state manager was updated
    auto* sensor = state.get_sensor("test_temp");
    TEST_ASSERT_NOT_NULL(sensor);
    TEST_ASSERT_EQUAL(SensorQuality::GOOD, sensor->quality);
}

void test_modbus_module_error_handling() {
    SystemConfig config;
    ConfigLoader::load_defaults(config);

    config.hardware.modbus_device_count = 1;
    config.hardware.modbus_devices[0].address = 1;
    config.hardware.modbus_devices[0].register_count = 1;
    strncpy(config.hardware.modbus_devices[0].registers[0].name, "error_sensor", 32);

    state.register_sensor("error_sensor", "°C");

    ModbusModule module(&modbus, &sim_time, &state, &events);
    module.initialize(config);

    // Inject error
    modbus.set_inject_error(true);

    // Execute poll cycle
    module.poll_cycle();

    // Sensor should be marked as BAD
    TEST_ASSERT_EQUAL(SensorQuality::BAD, module.get_sensor_quality("error_sensor"));
}

// Fermentation Plan Manager tests

void test_plan_manager_start_plan() {
    // Register a fermenter
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

    FermentationPlanManager manager(&sim_time, &storage, &state, &events);

    // Create a simple plan
    PlanStep steps[2];
    strncpy(steps[0].name, "Primary", MAX_NAME_LENGTH);
    steps[0].duration_hours = 168;
    steps[0].target_temp = 18.0f;
    steps[0].target_pressure = 1.0f;

    strncpy(steps[1].name, "Cold Crash", MAX_NAME_LENGTH);
    steps[1].duration_hours = 48;
    steps[1].target_temp = 2.0f;
    steps[1].target_pressure = 1.5f;

    bool result = manager.start_plan(1, steps, 2);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(manager.is_plan_active(1));
    TEST_ASSERT_EQUAL(0, manager.get_current_step(1));
    TEST_ASSERT_EQUAL_FLOAT(18.0f, manager.get_target_temp(1));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, manager.get_target_pressure(1));
}

void test_plan_manager_step_advance() {
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

    FermentationPlanManager manager(&sim_time, &storage, &state, &events);

    // Create plan with short steps
    PlanStep steps[2];
    steps[0].duration_hours = 1;  // 1 hour
    steps[0].target_temp = 10.0f;
    steps[0].target_pressure = 0.5f;

    steps[1].duration_hours = 1;
    steps[1].target_temp = 20.0f;
    steps[1].target_pressure = 1.0f;

    manager.start_plan(2, steps, 2);
    TEST_ASSERT_EQUAL(0, manager.get_current_step(2));

    // Advance time by 1.5 hours (5400 seconds)
    sim_time.advance_unix_time(5400);
    manager.update();

    // Should now be on step 1
    TEST_ASSERT_EQUAL(1, manager.get_current_step(2));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, manager.get_target_temp(2));
}

void test_plan_manager_completion() {
    // Setup fermenter
    state.register_sensor("f3_temp", "°C");
    state.register_sensor("f3_pressure", "bar");
    state.register_relay("f3_cooling", RelayType::SOLENOID_NC);
    state.register_relay("f3_spunding", RelayType::SOLENOID_NO);

    FermenterDef def;
    def.id = 3;
    strncpy(def.name, "F3", MAX_NAME_LENGTH);
    strncpy(def.temp_sensor, "f3_temp", MAX_NAME_LENGTH);
    strncpy(def.pressure_sensor, "f3_pressure", MAX_NAME_LENGTH);
    strncpy(def.cooling_relay, "f3_cooling", MAX_NAME_LENGTH);
    strncpy(def.spunding_relay, "f3_spunding", MAX_NAME_LENGTH);
    state.register_fermenter(def);

    FermentationPlanManager manager(&sim_time, &storage, &state, &events);

    // Create very short plan
    PlanStep steps[1];
    steps[0].duration_hours = 1;
    steps[0].target_temp = 18.0f;
    steps[0].target_pressure = 1.0f;

    manager.start_plan(3, steps, 1);
    TEST_ASSERT_TRUE(manager.is_plan_active(3));

    // Advance time past plan end
    sim_time.advance_unix_time(7200);  // 2 hours
    manager.update();

    // Plan should be complete
    TEST_ASSERT_FALSE(manager.is_plan_active(3));
}

void test_plan_manager_persistence() {
    // Setup fermenter
    state.register_sensor("f4_temp", "°C");
    state.register_sensor("f4_pressure", "bar");
    state.register_relay("f4_cooling", RelayType::SOLENOID_NC);
    state.register_relay("f4_spunding", RelayType::SOLENOID_NO);

    FermenterDef def;
    def.id = 4;
    strncpy(def.name, "F4", MAX_NAME_LENGTH);
    strncpy(def.temp_sensor, "f4_temp", MAX_NAME_LENGTH);
    strncpy(def.pressure_sensor, "f4_pressure", MAX_NAME_LENGTH);
    strncpy(def.cooling_relay, "f4_cooling", MAX_NAME_LENGTH);
    strncpy(def.spunding_relay, "f4_spunding", MAX_NAME_LENGTH);
    state.register_fermenter(def);

    // Start plan with first manager
    {
        FermentationPlanManager manager(&sim_time, &storage, &state, &events);

        PlanStep steps[1];
        steps[0].duration_hours = 24;
        steps[0].target_temp = 15.0f;
        steps[0].target_pressure = 0.8f;

        manager.start_plan(4, steps, 1);
    }

    // Create new manager and load from storage
    {
        FermentationPlanManager manager(&sim_time, &storage, &state, &events);
        manager.load_from_storage();

        TEST_ASSERT_TRUE(manager.is_plan_active(4));
        TEST_ASSERT_EQUAL_FLOAT(15.0f, manager.get_target_temp(4));
    }
}

void test_plan_manager_stop() {
    FermentationPlanManager manager(&sim_time, &storage, &state, &events);

    // Use fermenter from previous test
    PlanStep steps[1];
    steps[0].duration_hours = 100;
    steps[0].target_temp = 10.0f;
    steps[0].target_pressure = 1.0f;

    manager.start_plan(4, steps, 1);
    TEST_ASSERT_TRUE(manager.is_plan_active(4));

    manager.stop_plan(4);
    TEST_ASSERT_FALSE(manager.is_plan_active(4));
}

// Event integration tests

void test_event_on_sensor_update() {
    SystemConfig config;
    ConfigLoader::load_defaults(config);

    config.hardware.modbus_device_count = 1;
    config.hardware.modbus_devices[0].address = 1;
    config.hardware.modbus_devices[0].register_count = 1;
    strncpy(config.hardware.modbus_devices[0].registers[0].name, "event_sensor", 32);
    config.hardware.modbus_devices[0].registers[0].reg = 0;
    config.hardware.modbus_devices[0].registers[0].scale = 0.1f;

    state.register_sensor("event_sensor", "°C", 0.1f);

    int event_count = 0;
    float last_value = 0;

    events.subscribe(EventType::SENSOR_UPDATE, [&](const Event& e) {
        event_count++;
        last_value = e.data.value;
    });

    modbus.set_register(1, 0, 200);  // 200 * 0.1 = 20.0°C

    ModbusModule module(&modbus, &sim_time, &state, &events);
    module.initialize(config);
    module.poll_cycle();

    TEST_ASSERT_GREATER_THAN(0, event_count);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 20.0f, last_value);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // MODBUS Module tests
    RUN_TEST(test_modbus_module_initialize);
    RUN_TEST(test_modbus_module_poll_cycle);
    RUN_TEST(test_modbus_module_error_handling);

    // Fermentation Plan Manager tests
    RUN_TEST(test_plan_manager_start_plan);
    RUN_TEST(test_plan_manager_step_advance);
    RUN_TEST(test_plan_manager_completion);
    RUN_TEST(test_plan_manager_persistence);
    RUN_TEST(test_plan_manager_stop);

    // Event integration tests
    RUN_TEST(test_event_on_sensor_update);

    return UNITY_END();
}
