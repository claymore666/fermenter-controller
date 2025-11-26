#include <unity.h>
#include "modules/debug_console.h"
#include "hal/simulator/hal_simulator.h"
#include "core/state_manager.h"
#include "core/event_bus.h"
#include "core/config.h"
#include "modules/safety_controller.h"
#include "modules/fermentation_plan.h"
#include <cstring>

using namespace modules;
using namespace core;

// Test fixtures
static hal::simulator::SimulatorSerial* g_serial;
static hal::simulator::SimulatorTime* g_time;
static hal::simulator::SimulatorGPIO* g_gpio;
static hal::simulator::SimulatorStorage* g_storage;
static hal::simulator::SimulatorModbus* g_modbus;
static StateManager* g_state;
static EventBus* g_events;
static SystemConfig* g_config;
static SafetyController* g_safety;
static FermentationPlanManager* g_plans;
static DebugConsole* g_console;

void setUp(void) {
    g_serial = new hal::simulator::SimulatorSerial();
    g_time = new hal::simulator::SimulatorTime();
    g_gpio = new hal::simulator::SimulatorGPIO();
    g_storage = new hal::simulator::SimulatorStorage();
    g_modbus = new hal::simulator::SimulatorModbus();
    g_state = new StateManager();
    g_events = new EventBus();
    g_config = new SystemConfig();

    // Initialize config with test data
    g_config->fermenter_count = 2;
    g_config->fermenters[0].id = 1;
    strcpy(g_config->fermenters[0].name, "F1");
    g_config->fermenters[1].id = 2;
    strcpy(g_config->fermenters[1].name, "F2");

    g_state->initialize(*g_config);

    // Add test sensors
    g_state->register_sensor("test_pressure", "bar");
    g_state->register_sensor("test_temp", "C");

    // Add test relays
    g_state->register_relay("cooling", RelayType::SOLENOID_NC);
    g_state->register_relay("spunding", RelayType::SOLENOID_NO);

    g_safety = new SafetyController(g_time, g_gpio, g_state, g_events);
    g_plans = new FermentationPlanManager(g_time, g_storage, g_state, g_events);

    g_console = new DebugConsole(
        g_serial, g_time, g_state, g_events,
        g_config, g_safety, g_plans, g_modbus
    );
    g_console->initialize(115200);
    g_serial->clear_output(); // Clear welcome message
}

void tearDown(void) {
    delete g_console;
    delete g_plans;
    delete g_safety;
    delete g_config;
    delete g_events;
    delete g_state;
    delete g_modbus;
    delete g_storage;
    delete g_gpio;
    delete g_time;
    delete g_serial;
}

// Helper to send command and get output
std::string send_command(const char* cmd) {
    g_serial->clear_output();
    g_serial->inject_input(cmd);
    g_serial->inject_input("\r");
    g_console->process();
    return g_serial->get_output();
}

// Test cases

void test_help_command() {
    std::string output = send_command("help");

    TEST_ASSERT_TRUE(output.find("Available commands") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("status") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("sensors") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("relays") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("fermenters") != std::string::npos);
}

void test_status_command() {
    std::string output = send_command("status");

    TEST_ASSERT_TRUE(output.find("System:") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("Uptime") != std::string::npos);
}

void test_sensors_command() {
    // Update sensor value
    g_state->update_sensor_value(0, 0.5f, 1000);
    g_state->update_sensor_filtered(0, 0.5f, 0.5f);
    g_state->set_sensor_quality(0, SensorQuality::GOOD);

    std::string output = send_command("sensors");

    TEST_ASSERT_TRUE(output.find("Sensors") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("test_pressure") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("bar") != std::string::npos);
}

void test_sensor_detail_command() {
    g_state->update_sensor_value(0, 0.498f, 1000);
    g_state->update_sensor_filtered(0, 0.502f, 0.502f);

    std::string output = send_command("sensor test_pressure");

    TEST_ASSERT_TRUE(output.find("Sensor: test_pressure") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("Raw value") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("Filtered") != std::string::npos);
}

void test_sensor_not_found() {
    std::string output = send_command("sensor nonexistent");

    TEST_ASSERT_TRUE(output.find("not found") != std::string::npos);
}

void test_relays_command() {
    std::string output = send_command("relays");

    TEST_ASSERT_TRUE(output.find("Relays") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("cooling") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("spunding") != std::string::npos);
}

void test_relay_control_on() {
    std::string output = send_command("relay cooling on");

    TEST_ASSERT_TRUE(output.find("set to ON") != std::string::npos);

    // Verify relay state changed
    auto* relay = g_state->get_relay_by_id(0);
    TEST_ASSERT_NOT_NULL(relay);
    TEST_ASSERT_TRUE(relay->state);
}

void test_relay_control_off() {
    // First turn on
    g_state->set_relay_state(0, true, 1000);

    std::string output = send_command("relay cooling off");

    TEST_ASSERT_TRUE(output.find("set to OFF") != std::string::npos);

    auto* relay = g_state->get_relay_by_id(0);
    TEST_ASSERT_FALSE(relay->state);
}

void test_relay_not_found() {
    std::string output = send_command("relay nonexistent on");

    TEST_ASSERT_TRUE(output.find("not found") != std::string::npos);
}

void test_fermenters_command() {
    std::string output = send_command("fermenters");

    TEST_ASSERT_TRUE(output.find("Fermenters") != std::string::npos);
}

void test_fermenter_detail_command() {
    auto* ferm = g_state->get_fermenter(1);
    if (ferm) {
        ferm->current_temp = 18.5f;
        ferm->target_temp = 18.0f;
        ferm->mode = FermenterMode::MANUAL;
    }

    std::string output = send_command("fermenter 1");

    TEST_ASSERT_TRUE(output.find("Fermenter 1") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("Temperature") != std::string::npos);
}

void test_fermenter_setpoint() {
    std::string output = send_command("fermenter 1 setpoint 17.5");

    TEST_ASSERT_TRUE(output.find("setpoint set to") != std::string::npos);

    auto* ferm = g_state->get_fermenter(1);
    TEST_ASSERT_NOT_NULL(ferm);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 17.5f, ferm->target_temp);
}

void test_fermenter_mode() {
    std::string output = send_command("fermenter 1 mode manual");

    TEST_ASSERT_TRUE(output.find("mode set to") != std::string::npos);

    auto* ferm = g_state->get_fermenter(1);
    TEST_ASSERT_NOT_NULL(ferm);
    TEST_ASSERT_EQUAL(FermenterMode::MANUAL, ferm->mode);
}

void test_fermenter_not_found() {
    std::string output = send_command("fermenter 99");

    TEST_ASSERT_TRUE(output.find("not found") != std::string::npos);
}

void test_pid_command() {
    std::string output = send_command("pid 1");

    TEST_ASSERT_TRUE(output.find("PID for fermenter") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("Kp") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("Ki") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("Kd") != std::string::npos);
}

void test_pid_tune() {
    std::string output = send_command("pid 1 tune 2.5 0.15 1.2");

    TEST_ASSERT_TRUE(output.find("PID tuned") != std::string::npos);

    auto* ferm = g_state->get_fermenter(1);
    TEST_ASSERT_NOT_NULL(ferm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, ferm->pid_params.kp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.15f, ferm->pid_params.ki);
}

void test_alarms_no_active() {
    std::string output = send_command("alarms");

    TEST_ASSERT_TRUE(output.find("No active alarms") != std::string::npos);
}

void test_modbus_stats() {
    // Set some stats
    g_state->update_modbus_stats(100, 5);

    std::string output = send_command("modbus stats");

    TEST_ASSERT_TRUE(output.find("MODBUS Statistics") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("Transactions") != std::string::npos);
    TEST_ASSERT_TRUE(output.find("Errors") != std::string::npos);
}

void test_modbus_read() {
    // Set a register value
    g_modbus->set_register(1, 0, 12345);

    std::string output = send_command("modbus read 1 0");

    TEST_ASSERT_TRUE(output.find("12345") != std::string::npos);
}

void test_heap_command() {
    std::string output = send_command("heap");

    TEST_ASSERT_TRUE(output.find("Free heap") != std::string::npos);
}

void test_uptime_command() {
    g_state->update_system_uptime(3661); // 1 hour, 1 minute, 1 second

    std::string output = send_command("uptime");

    TEST_ASSERT_TRUE(output.find("Uptime") != std::string::npos);
}

void test_unknown_command() {
    std::string output = send_command("invalidcmd");

    TEST_ASSERT_TRUE(output.find("Unknown command") != std::string::npos);
}

void test_empty_command() {
    g_serial->clear_output();
    g_serial->inject_input("\r");
    g_console->process();

    // Should not crash, just show prompt
    std::string output = g_serial->get_output();
    TEST_ASSERT_TRUE(output.length() == 0 || output.find(">") != std::string::npos);
}

void test_backspace_handling() {
    g_serial->clear_output();
    g_serial->inject_input("helx");
    g_serial->inject_input("\b"); // Backspace
    g_serial->inject_input("p\r");
    g_console->process();

    std::string output = g_serial->get_output();
    TEST_ASSERT_TRUE(output.find("Available commands") != std::string::npos);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // Help and status
    RUN_TEST(test_help_command);
    RUN_TEST(test_status_command);
    RUN_TEST(test_heap_command);
    RUN_TEST(test_uptime_command);

    // Sensors
    RUN_TEST(test_sensors_command);
    RUN_TEST(test_sensor_detail_command);
    RUN_TEST(test_sensor_not_found);

    // Relays
    RUN_TEST(test_relays_command);
    RUN_TEST(test_relay_control_on);
    RUN_TEST(test_relay_control_off);
    RUN_TEST(test_relay_not_found);

    // Fermenters
    RUN_TEST(test_fermenters_command);
    RUN_TEST(test_fermenter_detail_command);
    RUN_TEST(test_fermenter_setpoint);
    RUN_TEST(test_fermenter_mode);
    RUN_TEST(test_fermenter_not_found);

    // PID
    RUN_TEST(test_pid_command);
    RUN_TEST(test_pid_tune);

    // Alarms
    RUN_TEST(test_alarms_no_active);

    // MODBUS
    RUN_TEST(test_modbus_stats);
    RUN_TEST(test_modbus_read);

    // Edge cases
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_empty_command);
    RUN_TEST(test_backspace_handling);

    return UNITY_END();
}
