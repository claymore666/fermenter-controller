#include <unity.h>
#include "modules/filters.h"
#include "modules/pid_controller.h"
#include "modules/modbus_module.h"
#include "hal/simulator/hal_simulator.h"
#include "core/state_manager.h"
#include "core/event_bus.h"
#include <cmath>

using namespace modules;
using namespace core;

void setUp(void) {}
void tearDown(void) {}

// EMA Filter tests

void test_ema_filter_initialization() {
    EMAFilter filter(0.3f);

    float result = filter.update(100.0f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, result);  // First value passes through
    TEST_ASSERT_TRUE(filter.is_ready());
}

void test_ema_filter_smoothing() {
    EMAFilter filter(0.5f);  // alpha = 0.5

    filter.update(100.0f);  // Initial: 100
    float result = filter.update(0.0f);  // 0.5*0 + 0.5*100 = 50

    TEST_ASSERT_EQUAL_FLOAT(50.0f, result);
}

void test_ema_filter_convergence() {
    EMAFilter filter(0.3f);

    // Feed constant value, should converge
    for (int i = 0; i < 50; i++) {
        filter.update(100.0f);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, filter.get_value());
}

void test_ema_filter_reset() {
    EMAFilter filter(0.3f);

    filter.update(100.0f);
    filter.update(100.0f);
    TEST_ASSERT_TRUE(filter.is_ready());

    filter.reset();
    TEST_ASSERT_FALSE(filter.is_ready());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, filter.get_value());
}

// Moving Average Filter tests

void test_moving_average_basic() {
    MovingAverageFilter filter(3);

    filter.update(10.0f);
    filter.update(20.0f);
    filter.update(30.0f);

    // Average of 10, 20, 30 = 20
    TEST_ASSERT_EQUAL_FLOAT(20.0f, filter.get_value());
    TEST_ASSERT_TRUE(filter.is_ready());
}

void test_moving_average_window() {
    MovingAverageFilter filter(3);

    filter.update(10.0f);
    filter.update(20.0f);
    filter.update(30.0f);
    filter.update(40.0f);  // Oldest (10) drops out

    // Average of 20, 30, 40 = 30
    TEST_ASSERT_EQUAL_FLOAT(30.0f, filter.get_value());
}

void test_moving_average_not_ready() {
    MovingAverageFilter filter(5);

    filter.update(10.0f);
    filter.update(20.0f);

    TEST_ASSERT_FALSE(filter.is_ready());
    TEST_ASSERT_EQUAL(2, filter.get_sample_count());
}

// Median Filter tests

void test_median_filter_basic() {
    MedianFilter filter(5);

    filter.update(1.0f);
    filter.update(2.0f);
    filter.update(100.0f);  // Outlier
    filter.update(3.0f);
    filter.update(4.0f);

    // Sorted: 1, 2, 3, 4, 100 -> Median = 3
    TEST_ASSERT_EQUAL_FLOAT(3.0f, filter.get_value());
}

void test_median_filter_outlier_rejection() {
    MedianFilter filter(5);

    filter.update(18.0f);
    filter.update(18.5f);
    filter.update(999.0f);  // Outlier
    filter.update(18.2f);
    filter.update(18.3f);

    // Sorted: 18.0, 18.2, 18.3, 18.5, 999.0 -> Median = 18.3
    TEST_ASSERT_EQUAL_FLOAT(18.3f, filter.get_value());
}

// Dual Rate Filter tests

void test_dual_rate_filter_blend() {
    DualRateFilter filter(0.5f, 0.5f, 0.5f);  // Equal blend

    filter.update_base(10.0f);
    filter.update_extra(20.0f);

    // Blend: 0.5 * 20 + 0.5 * 10 = 15
    TEST_ASSERT_EQUAL_FLOAT(15.0f, filter.get_value());
}

void test_dual_rate_filter_extra_priority() {
    DualRateFilter filter(0.5f, 0.5f, 0.8f);  // 80% extra weight

    filter.update_base(10.0f);
    filter.update_extra(20.0f);

    // Blend: 0.8 * 20 + 0.2 * 10 = 18
    TEST_ASSERT_EQUAL_FLOAT(18.0f, filter.get_value());
}

// No Filter tests

void test_no_filter_passthrough() {
    NoFilter filter;

    TEST_ASSERT_EQUAL_FLOAT(42.0f, filter.update(42.0f));
    TEST_ASSERT_EQUAL_FLOAT(42.0f, filter.get_value());
    TEST_ASSERT_TRUE(filter.is_ready());
}

// PID Controller tests

void test_pid_proportional_only() {
    PIDController pid(1.0f, 0.0f, 0.0f);  // Kp=1, Ki=0, Kd=0

    float output = pid.compute(20.0f, 18.0f);  // Setpoint=20, Input=18

    // Error = 2, Output = Kp * Error = 2
    TEST_ASSERT_EQUAL_FLOAT(2.0f, output);
}

void test_pid_integral_accumulation() {
    PIDController pid(0.0f, 1.0f, 0.0f);  // Ki only

    pid.compute(20.0f, 18.0f);  // Error = 2, Integral = 2
    float output = pid.compute(20.0f, 18.0f);  // Integral = 4

    TEST_ASSERT_EQUAL_FLOAT(4.0f, output);
}

void test_pid_output_clamping() {
    PIDController pid(100.0f, 0.0f, 0.0f);  // Large Kp
    pid.set_output_limits(0.0f, 100.0f);

    float output = pid.compute(100.0f, 0.0f);  // Error = 100

    // Should be clamped to 100
    TEST_ASSERT_EQUAL_FLOAT(100.0f, output);
}

void test_pid_anti_windup() {
    PIDController pid(0.0f, 1.0f, 0.0f);  // Ki only
    pid.set_output_limits(0.0f, 100.0f);

    // Drive to saturation
    for (int i = 0; i < 200; i++) {
        pid.compute(100.0f, 0.0f);  // Large error
    }

    // Integral should be clamped
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, pid.get_integral());

    // When error reverses, should respond quickly
    float output = pid.compute(0.0f, 100.0f);  // Error = -100

    // Should not be stuck at max due to windup
    TEST_ASSERT_LESS_THAN(100.0f, output);
}

void test_pid_reset() {
    PIDController pid(1.0f, 1.0f, 1.0f);

    pid.compute(20.0f, 18.0f);
    pid.compute(20.0f, 18.0f);

    pid.reset();

    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.get_integral());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.get_last_error());
}

void test_pid_bumpless_transfer() {
    PIDController pid(1.0f, 0.1f, 0.0f);
    pid.set_output_limits(0.0f, 100.0f);

    // Initialize with current output (e.g., from manual mode)
    pid.initialize(50.0f, 18.0f);

    // First compute should not jump
    float output = pid.compute(18.0f, 18.0f);  // Error = 0

    // Should be close to initialized value
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 50.0f, output);
}

void test_pid_derivative_on_measurement() {
    PIDController pid(0.0f, 0.0f, 1.0f);  // Kd only
    pid.set_output_limits(-100.0f, 100.0f);  // Allow negative output

    pid.compute(20.0f, 10.0f);  // First call
    float output = pid.compute(20.0f, 15.0f);  // Input changed by 5

    // Derivative should be -Kd * (input_change) = -1 * 5 = -5
    TEST_ASSERT_EQUAL_FLOAT(-5.0f, output);
}

// PID Autotuner tests

void test_autotuner_start() {
    PIDAutotuner tuner(0.0f, 100.0f, 0.5f);

    tuner.start(18.0f);

    TEST_ASSERT_EQUAL(PIDAutotuner::State::RUNNING, tuner.get_state());
}

void test_autotuner_relay_switching() {
    PIDAutotuner tuner(0.0f, 100.0f, 0.5f);

    tuner.start(18.0f);

    // Should start high
    float output = tuner.update(17.0f, 1000);  // Below setpoint
    TEST_ASSERT_EQUAL_FLOAT(100.0f, output);

    // Go above setpoint + hysteresis
    output = tuner.update(19.0f, 2000);  // Above 18.5
    TEST_ASSERT_EQUAL_FLOAT(0.0f, output);
}

// 4-20mA Fault Detection tests

void test_4to20ma_wire_break_detection() {
    hal::simulator::SimulatorTime time;
    hal::simulator::SimulatorModbus modbus;
    StateManager state;
    EventBus events;

    ModbusModule module(&modbus, &time, &state, &events);

    // Create config with min_raw threshold
    SystemConfig config;
    config.hardware.modbus_device_count = 1;
    auto& dev = config.hardware.modbus_devices[0];
    dev.address = 1;
    strcpy(dev.type, "analog_8ch");
    strcpy(dev.name, "Test Analog");
    dev.register_count = 1;

    auto& reg = dev.registers[0];
    strcpy(reg.name, "test_pressure");
    reg.reg = 0;
    reg.scale = 0.0000488f;
    reg.min_raw = 800;    // 4mA threshold
    reg.max_raw = 32000;  // 20mA threshold
    reg.filter = FilterType::NONE;

    // Initialize state with sensor
    state.register_sensor("test_pressure", "bar");

    module.initialize(config);

    // Set MODBUS to return value below min_raw (wire break)
    modbus.set_register(1, 0, 100);  // 100 < 800

    module.poll_cycle();

    // Check sensor quality is BAD
    auto* sensor = state.get_sensor("test_pressure");
    TEST_ASSERT_NOT_NULL(sensor);
    TEST_ASSERT_EQUAL(SensorQuality::BAD, sensor->quality);
}

void test_4to20ma_overrange_detection() {
    hal::simulator::SimulatorTime time;
    hal::simulator::SimulatorModbus modbus;
    StateManager state;
    EventBus events;

    ModbusModule module(&modbus, &time, &state, &events);

    SystemConfig config;
    config.hardware.modbus_device_count = 1;
    auto& dev = config.hardware.modbus_devices[0];
    dev.address = 1;
    strcpy(dev.type, "analog_8ch");
    strcpy(dev.name, "Test Analog");
    dev.register_count = 1;

    auto& reg = dev.registers[0];
    strcpy(reg.name, "test_pressure");
    reg.reg = 0;
    reg.scale = 0.0000488f;
    reg.min_raw = 800;
    reg.max_raw = 32000;
    reg.filter = FilterType::NONE;

    state.register_sensor("test_pressure", "bar");
    module.initialize(config);

    // Set MODBUS to return value above max_raw (sensor fault)
    modbus.set_register(1, 0, 35000);  // 35000 > 32000

    module.poll_cycle();

    auto* sensor = state.get_sensor("test_pressure");
    TEST_ASSERT_NOT_NULL(sensor);
    TEST_ASSERT_EQUAL(SensorQuality::BAD, sensor->quality);
}

void test_4to20ma_valid_range() {
    hal::simulator::SimulatorTime time;
    hal::simulator::SimulatorModbus modbus;
    StateManager state;
    EventBus events;

    ModbusModule module(&modbus, &time, &state, &events);

    SystemConfig config;
    config.hardware.modbus_device_count = 1;
    auto& dev = config.hardware.modbus_devices[0];
    dev.address = 1;
    strcpy(dev.type, "analog_8ch");
    strcpy(dev.name, "Test Analog");
    dev.register_count = 1;

    auto& reg = dev.registers[0];
    strcpy(reg.name, "test_pressure");
    reg.reg = 0;
    reg.scale = 0.0000488f;
    reg.min_raw = 800;
    reg.max_raw = 32000;
    reg.filter = FilterType::NONE;

    state.register_sensor("test_pressure", "bar");
    module.initialize(config);

    // Set MODBUS to return valid value (0.5 bar = ~10245)
    modbus.set_register(1, 0, 10245);

    module.poll_cycle();

    auto* sensor = state.get_sensor("test_pressure");
    TEST_ASSERT_NOT_NULL(sensor);
    TEST_ASSERT_EQUAL(SensorQuality::GOOD, sensor->quality);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, sensor->filtered_value);
}

void test_4to20ma_boundary_min() {
    hal::simulator::SimulatorTime time;
    hal::simulator::SimulatorModbus modbus;
    StateManager state;
    EventBus events;

    ModbusModule module(&modbus, &time, &state, &events);

    SystemConfig config;
    config.hardware.modbus_device_count = 1;
    auto& dev = config.hardware.modbus_devices[0];
    dev.address = 1;
    strcpy(dev.type, "analog_8ch");
    strcpy(dev.name, "Test Analog");
    dev.register_count = 1;

    auto& reg = dev.registers[0];
    strcpy(reg.name, "test_pressure");
    reg.reg = 0;
    reg.scale = 0.0000488f;
    reg.min_raw = 800;
    reg.max_raw = 32000;
    reg.filter = FilterType::NONE;

    state.register_sensor("test_pressure", "bar");
    module.initialize(config);

    // Set MODBUS to return exactly min_raw (boundary - should be valid)
    modbus.set_register(1, 0, 800);

    module.poll_cycle();

    auto* sensor = state.get_sensor("test_pressure");
    TEST_ASSERT_NOT_NULL(sensor);
    TEST_ASSERT_EQUAL(SensorQuality::GOOD, sensor->quality);
}

void test_4to20ma_boundary_max() {
    hal::simulator::SimulatorTime time;
    hal::simulator::SimulatorModbus modbus;
    StateManager state;
    EventBus events;

    ModbusModule module(&modbus, &time, &state, &events);

    SystemConfig config;
    config.hardware.modbus_device_count = 1;
    auto& dev = config.hardware.modbus_devices[0];
    dev.address = 1;
    strcpy(dev.type, "analog_8ch");
    strcpy(dev.name, "Test Analog");
    dev.register_count = 1;

    auto& reg = dev.registers[0];
    strcpy(reg.name, "test_pressure");
    reg.reg = 0;
    reg.scale = 0.0000488f;
    reg.min_raw = 800;
    reg.max_raw = 32000;
    reg.filter = FilterType::NONE;

    state.register_sensor("test_pressure", "bar");
    module.initialize(config);

    // Set MODBUS to return exactly max_raw (boundary - should be valid)
    modbus.set_register(1, 0, 32000);

    module.poll_cycle();

    auto* sensor = state.get_sensor("test_pressure");
    TEST_ASSERT_NOT_NULL(sensor);
    TEST_ASSERT_EQUAL(SensorQuality::GOOD, sensor->quality);
}

void test_filter_noise_smoothing() {
    EMAFilter filter(0.3f);

    // Simulate noisy sensor readings (5% noise around 1.0)
    float noisy_values[] = {0.95f, 1.05f, 0.98f, 1.02f, 0.97f, 1.03f, 0.99f, 1.01f, 0.96f, 1.04f};

    float filtered = 0;
    for (int i = 0; i < 10; i++) {
        filtered = filter.update(noisy_values[i]);
    }

    // Filtered value should be close to 1.0 (true value)
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, filtered);
}

void test_moving_avg_noise_smoothing() {
    MovingAverageFilter filter(5);

    // Simulate noisy sensor readings
    float noisy_values[] = {0.95f, 1.05f, 0.98f, 1.02f, 0.97f, 1.03f, 0.99f, 1.01f, 0.96f, 1.04f};

    float filtered = 0;
    for (int i = 0; i < 10; i++) {
        filtered = filter.update(noisy_values[i]);
    }

    // After 10 samples, should be averaging last 5
    // Last 5: 1.03, 0.99, 1.01, 0.96, 1.04 = avg 1.006
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, filtered);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // EMA Filter tests
    RUN_TEST(test_ema_filter_initialization);
    RUN_TEST(test_ema_filter_smoothing);
    RUN_TEST(test_ema_filter_convergence);
    RUN_TEST(test_ema_filter_reset);

    // Moving Average tests
    RUN_TEST(test_moving_average_basic);
    RUN_TEST(test_moving_average_window);
    RUN_TEST(test_moving_average_not_ready);

    // Median Filter tests
    RUN_TEST(test_median_filter_basic);
    RUN_TEST(test_median_filter_outlier_rejection);

    // Dual Rate Filter tests
    RUN_TEST(test_dual_rate_filter_blend);
    RUN_TEST(test_dual_rate_filter_extra_priority);

    // No Filter tests
    RUN_TEST(test_no_filter_passthrough);

    // PID Controller tests
    RUN_TEST(test_pid_proportional_only);
    RUN_TEST(test_pid_integral_accumulation);
    RUN_TEST(test_pid_output_clamping);
    RUN_TEST(test_pid_anti_windup);
    RUN_TEST(test_pid_reset);
    RUN_TEST(test_pid_bumpless_transfer);
    RUN_TEST(test_pid_derivative_on_measurement);

    // Autotuner tests
    RUN_TEST(test_autotuner_start);
    RUN_TEST(test_autotuner_relay_switching);

    // 4-20mA Fault Detection tests
    RUN_TEST(test_4to20ma_wire_break_detection);
    RUN_TEST(test_4to20ma_overrange_detection);
    RUN_TEST(test_4to20ma_valid_range);
    RUN_TEST(test_4to20ma_boundary_min);
    RUN_TEST(test_4to20ma_boundary_max);

    // Noise smoothing tests
    RUN_TEST(test_filter_noise_smoothing);
    RUN_TEST(test_moving_avg_noise_smoothing);

    return UNITY_END();
}
