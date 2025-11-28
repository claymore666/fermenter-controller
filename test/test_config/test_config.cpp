#include <unity.h>
#include <cstring>
#include "core/config_loader.h"

using namespace core;

void setUp(void) {}
void tearDown(void) {}

void test_load_defaults() {
    SystemConfig config;
    ConfigLoader::load_defaults(config);

    // Check timing defaults
    TEST_ASSERT_EQUAL(5000, config.modbus_timing.poll_interval_ms);
    TEST_ASSERT_EQUAL(1000, config.modbus_timing.timeout_ms);
    TEST_ASSERT_EQUAL(3, config.modbus_timing.max_retries);

    // Check scheduler defaults
    TEST_ASSERT_EQUAL(1000, config.scheduler.base_cycle_ms);
    TEST_ASSERT_EQUAL(3, config.scheduler.base_samples_per_cycle);
    TEST_ASSERT_TRUE(config.scheduler.bulk_read_enabled);

    // Check PID defaults
    TEST_ASSERT_EQUAL(5000, config.pid_timing.calculation_interval_ms);
    TEST_ASSERT_EQUAL(60000, config.pid_timing.output_cycle_time_ms);

    // Check safety defaults
    TEST_ASSERT_EQUAL_FLOAT(3.0f, config.safety_timing.max_temp_deviation);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, config.safety_timing.max_pressure_bar);

    // Check MQTT defaults
    TEST_ASSERT_EQUAL(1883, config.mqtt.port);
    TEST_ASSERT_FALSE(config.mqtt.enabled);

    // Check NTP defaults
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", config.ntp.server);
    TEST_ASSERT_TRUE(config.ntp.enabled);
}

void test_load_json_minimal() {
    SystemConfig config;

    const char* json = R"({
        "wifi": {
            "ssid": "TestNetwork",
            "password": "secret123"
        }
    })";

    bool result = ConfigLoader::load_from_json(json, config);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("TestNetwork", config.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("secret123", config.wifi.password);

    // Defaults should still be loaded
    TEST_ASSERT_EQUAL(5000, config.modbus_timing.poll_interval_ms);
}

void test_load_json_timing() {
    SystemConfig config;

    const char* json = R"({
        "timing": {
            "modbus": {
                "poll_interval_ms": 3000,
                "timeout_ms": 500,
                "max_retries": 5
            },
            "pid": {
                "calculation_interval_ms": 10000,
                "output_cycle_time_ms": 120000
            },
            "safety": {
                "max_temp_deviation": 5.0,
                "max_pressure_bar": 3.0
            }
        }
    })";

    bool result = ConfigLoader::load_from_json(json, config);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(3000, config.modbus_timing.poll_interval_ms);
    TEST_ASSERT_EQUAL(500, config.modbus_timing.timeout_ms);
    TEST_ASSERT_EQUAL(5, config.modbus_timing.max_retries);
    TEST_ASSERT_EQUAL(10000, config.pid_timing.calculation_interval_ms);
    TEST_ASSERT_EQUAL(120000, config.pid_timing.output_cycle_time_ms);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, config.safety_timing.max_temp_deviation);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, config.safety_timing.max_pressure_bar);
}

void test_load_json_mqtt() {
    SystemConfig config;

    const char* json = R"({
        "mqtt": {
            "broker": "192.168.1.100",
            "port": 1884,
            "username": "user",
            "password": "pass",
            "topic_prefix": "home/brewery",
            "enabled": true
        }
    })";

    bool result = ConfigLoader::load_from_json(json, config);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", config.mqtt.broker);
    TEST_ASSERT_EQUAL(1884, config.mqtt.port);
    TEST_ASSERT_EQUAL_STRING("user", config.mqtt.username);
    TEST_ASSERT_EQUAL_STRING("pass", config.mqtt.password);
    TEST_ASSERT_EQUAL_STRING("home/brewery", config.mqtt.topic_prefix);
    TEST_ASSERT_TRUE(config.mqtt.enabled);
}

void test_load_json_fermenters() {
    SystemConfig config;

    const char* json = R"({
        "fermenters": [
            {
                "id": 1,
                "name": "F1",
                "temp_sensor": "fermenter_1_temp",
                "pressure_sensor": "fermenter_1_pressure",
                "cooling_relay": "fermenter_1_cooling",
                "spunding_relay": "fermenter_1_spunding"
            },
            {
                "id": 2,
                "name": "F2",
                "temp_sensor": "fermenter_2_temp",
                "pressure_sensor": "fermenter_2_pressure",
                "cooling_relay": "fermenter_2_cooling",
                "spunding_relay": "fermenter_2_spunding"
            }
        ]
    })";

    bool result = ConfigLoader::load_from_json(json, config);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(2, config.fermenter_count);

    TEST_ASSERT_EQUAL(1, config.fermenters[0].id);
    TEST_ASSERT_EQUAL_STRING("F1", config.fermenters[0].name);
    TEST_ASSERT_EQUAL_STRING("fermenter_1_temp", config.fermenters[0].temp_sensor);
    TEST_ASSERT_EQUAL_STRING("fermenter_1_pressure", config.fermenters[0].pressure_sensor);
    TEST_ASSERT_EQUAL_STRING("fermenter_1_cooling", config.fermenters[0].cooling_relay);

    TEST_ASSERT_EQUAL(2, config.fermenters[1].id);
    TEST_ASSERT_EQUAL_STRING("F2", config.fermenters[1].name);
}

void test_load_json_modbus_devices() {
    SystemConfig config;

    const char* json = R"({
        "modbus": {
            "devices": [
                {
                    "address": 1,
                    "type": "pt1000_8ch",
                    "name": "PT1000 Module",
                    "registers": [
                        {
                            "name": "glycol_supply",
                            "reg": 0,
                            "scale": 0.1,
                            "priority": "low"
                        },
                        {
                            "name": "fermenter_1_temp",
                            "reg": 2,
                            "scale": 0.1,
                            "priority": "high",
                            "extra_samples_per_second": 5
                        }
                    ]
                }
            ]
        }
    })";

    bool result = ConfigLoader::load_from_json(json, config);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, config.hardware.modbus_device_count);

    ModbusDeviceDef& device = config.hardware.modbus_devices[0];
    TEST_ASSERT_EQUAL(1, device.address);
    TEST_ASSERT_EQUAL_STRING("pt1000_8ch", device.type);
    TEST_ASSERT_EQUAL_STRING("PT1000 Module", device.name);
    TEST_ASSERT_EQUAL(2, device.register_count);

    TEST_ASSERT_EQUAL_STRING("glycol_supply", device.registers[0].name);
    TEST_ASSERT_EQUAL(0, device.registers[0].reg);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, device.registers[0].scale);
    TEST_ASSERT_EQUAL(SensorPriority::LOW, device.registers[0].priority);

    TEST_ASSERT_EQUAL_STRING("fermenter_1_temp", device.registers[1].name);
    TEST_ASSERT_EQUAL(SensorPriority::HIGH, device.registers[1].priority);
    TEST_ASSERT_EQUAL(5, device.registers[1].extra_samples_per_second);
}

void test_load_json_invalid() {
    SystemConfig config;

    const char* json = "{ invalid json }";

    bool result = ConfigLoader::load_from_json(json, config);

    TEST_ASSERT_FALSE(result);
}

// #52 - JSON config size limit tests
void test_load_json_rejects_oversized() {
    SystemConfig config;

    // Create a JSON string > 8KB (the limit)
    char oversized_json[9000];
    memset(oversized_json, ' ', sizeof(oversized_json) - 1);
    oversized_json[0] = '{';
    oversized_json[sizeof(oversized_json) - 2] = '}';
    oversized_json[sizeof(oversized_json) - 1] = '\0';

    bool result = ConfigLoader::load_from_json(oversized_json, config);

    TEST_ASSERT_FALSE(result);
}

void test_load_json_accepts_max_size() {
    SystemConfig config;

    // Create valid JSON just under 8KB
    const char* valid_json = "{\"timing\":{\"modbus_poll_ms\":1000}}";

    bool result = ConfigLoader::load_from_json(valid_json, config);

    TEST_ASSERT_TRUE(result);
}

void test_load_json_rejects_null() {
    SystemConfig config;

    bool result = ConfigLoader::load_from_json(nullptr, config);

    TEST_ASSERT_FALSE(result);
}

void test_to_json() {
    SystemConfig config;
    ConfigLoader::load_defaults(config);

    // Add a fermenter
    config.fermenter_count = 1;
    config.fermenters[0].id = 1;
    strncpy(config.fermenters[0].name, "F1", MAX_NAME_LENGTH);
    strncpy(config.fermenters[0].temp_sensor, "temp1", MAX_NAME_LENGTH);

    // Serialize
    char buffer[4096];
    size_t len = ConfigLoader::to_json(config, buffer, sizeof(buffer));

    TEST_ASSERT_GREATER_THAN(0, len);

    // Should be able to parse it back
    SystemConfig config2;
    bool result = ConfigLoader::load_from_json(buffer, config2);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, config2.fermenter_count);
    TEST_ASSERT_EQUAL_STRING("F1", config2.fermenters[0].name);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_load_defaults);
    RUN_TEST(test_load_json_minimal);
    RUN_TEST(test_load_json_timing);
    RUN_TEST(test_load_json_mqtt);
    RUN_TEST(test_load_json_fermenters);
    RUN_TEST(test_load_json_modbus_devices);
    RUN_TEST(test_load_json_invalid);
    RUN_TEST(test_load_json_rejects_oversized);
    RUN_TEST(test_load_json_accepts_max_size);
    RUN_TEST(test_load_json_rejects_null);
    RUN_TEST(test_to_json);

    return UNITY_END();
}
