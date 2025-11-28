#include <unity.h>
#include "hal/simulator/hal_simulator.h"
#include "core/state_manager.h"
#include "core/event_bus.h"
#include "core/config_loader.h"
#include "modules/http_server.h"
#include "modules/fermentation_plan.h"
#include "modules/safety_controller.h"
#include "security/secure_utils.h"

using namespace hal::simulator;
using namespace core;
using namespace modules;

// Test fixtures
static SimulatorStorage storage;
static SimulatorTime sim_time;
static SimulatorGPIO gpio;
static SimulatorModbus modbus;
static StateManager* state = nullptr;
static EventBus* events = nullptr;
static SystemConfig config;
static SafetyController* safety = nullptr;
static FermentationPlanManager* plans = nullptr;
static HttpServer* server = nullptr;

char response_buffer[4096];

// Test password that meets requirements (8+ chars, 2 categories)
static const char* TEST_PASSWORD = "Admin123";

// Helper to provision the device with test password
void provision_device() {
    char setup_response[1024];
    char setup_body[128];
    snprintf(setup_body, sizeof(setup_body), "{\"password\":\"%s\"}", TEST_PASSWORD);
    server->handle_request("POST", "/api/setup", setup_body, nullptr, setup_response, sizeof(setup_response));
}

// Helper to login and get token
const char* login_with_test_password() {
    if (!server->is_provisioned()) {
        provision_device();
    }
    return server->login(TEST_PASSWORD);
}

void setUp(void) {
    storage.reset();
    sim_time.set_millis(0);
    sim_time.set_unix_time(1700000000);
    ConfigLoader::load_defaults(config);

    // Reset state between tests
    if (events) delete events;
    events = new EventBus();

    if (state) delete state;
    state = new StateManager();

    // Create dependencies
    if (plans) delete plans;
    plans = new FermentationPlanManager(&sim_time, &storage, state, events);

    if (safety) delete safety;
    safety = new SafetyController(&sim_time, &gpio, state, events);

    if (server) delete server;
    server = new HttpServer(&sim_time, state, events, &config, safety, plans, &modbus, &storage, &gpio);

    memset(response_buffer, 0, sizeof(response_buffer));
}

void tearDown(void) {}

// ============================================
// AUTHENTICATION TESTS
// ============================================

void test_login_success() {
    // Provision device first
    provision_device();

    char body[128];
    snprintf(body, sizeof(body), "{\"password\":\"%s\"}", TEST_PASSWORD);
    int status = server->handle_request("POST", "/api/login",
        body, nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "success"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "true"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "token"));
}

void test_login_wrong_password() {
    provision_device();

    int status = server->handle_request("POST", "/api/login",
        "{\"password\":\"WrongPass1\"}", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(401, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "Invalid password"));
}

void test_login_empty_password() {
    provision_device();

    int status = server->handle_request("POST", "/api/login",
        "{\"password\":\"\"}", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(401, status);
}

void test_login_missing_password_field() {
    provision_device();

    int status = server->handle_request("POST", "/api/login",
        "{\"user\":\"admin\"}", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "Missing password"));
}

void test_login_malformed_json() {
    provision_device();

    int status = server->handle_request("POST", "/api/login",
        "not valid json", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
}

void test_login_empty_body() {
    provision_device();

    int status = server->handle_request("POST", "/api/login",
        "", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
}

void test_custom_password() {
    // Set first password
    server->set_admin_password("MySecret1");

    // Old password should fail
    int status = server->handle_request("POST", "/api/login",
        "{\"password\":\"WrongPass1\"}", nullptr, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(401, status);

    // New password should work
    status = server->handle_request("POST", "/api/login",
        "{\"password\":\"MySecret1\"}", nullptr, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(200, status);
}

void test_logout() {
    // Login first
    const char* token = login_with_test_password();
    TEST_ASSERT_NOT_NULL(token);

    // Make a copy of token since logout will clear it
    char token_copy[128];
    strncpy(token_copy, token, sizeof(token_copy) - 1);
    token_copy[sizeof(token_copy) - 1] = '\0';

    // Logout
    int status = server->handle_request("POST", "/api/logout",
        "", token_copy, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(200, status);

    // Token should be invalid now
    status = server->handle_request("GET", "/api/status",
        nullptr, token_copy, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(401, status);
}

void test_session_timeout() {
    // Login
    const char* token = login_with_test_password();
    TEST_ASSERT_NOT_NULL(token);
    char token_copy[128];
    strncpy(token_copy, token, sizeof(token_copy) - 1);
    token_copy[sizeof(token_copy) - 1] = '\0';

    // Should work immediately
    int status = server->handle_request("GET", "/api/status",
        nullptr, token_copy, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(200, status);

    // Advance time past session timeout (1 hour + 1 second)
    sim_time.advance_millis(3601000);

    // Should fail now
    status = server->handle_request("GET", "/api/status",
        nullptr, token_copy, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(401, status);
}

void test_session_refresh_on_activity() {
    // Login
    const char* token = login_with_test_password();
    TEST_ASSERT_NOT_NULL(token);
    char token_copy[128];
    strncpy(token_copy, token, sizeof(token_copy) - 1);
    token_copy[sizeof(token_copy) - 1] = '\0';

    // Advance time 50 minutes
    sim_time.advance_millis(50 * 60 * 1000);

    // Make a request (should refresh timeout)
    int status = server->handle_request("GET", "/api/status",
        nullptr, token_copy, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(200, status);

    // Advance another 50 minutes (total 100 minutes from login, but 50 from last activity)
    sim_time.advance_millis(50 * 60 * 1000);

    // Should still work because timeout was refreshed
    status = server->handle_request("GET", "/api/status",
        nullptr, token_copy, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(200, status);
}

// ============================================
// AUTHORIZATION TESTS
// ============================================

void test_unauthorized_access_without_token() {
    provision_device();  // Must be provisioned to get 401 vs 403

    int status = server->handle_request("GET", "/api/status",
        nullptr, nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(401, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "Unauthorized"));
}

void test_unauthorized_access_with_invalid_token() {
    provision_device();  // Must be provisioned to get 401 vs 403

    int status = server->handle_request("GET", "/api/status",
        nullptr, "invalid_token_12345", response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(401, status);
}

void test_health_endpoint_no_auth_required() {
    int status = server->handle_request("GET", "/api/health",
        nullptr, nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "ok"));
}

void test_health_shows_not_provisioned() {
    // Fresh server should show not provisioned
    int status = server->handle_request("GET", "/api/health",
        nullptr, nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "\"provisioned\":false"));
}

void test_health_shows_provisioned_after_setup() {
    // Provision the device
    server->handle_request("POST", "/api/setup",
        "{\"password\":\"ValidPass1\"}", nullptr, response_buffer, sizeof(response_buffer));

    // Health should now show provisioned
    int status = server->handle_request("GET", "/api/health",
        nullptr, nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "\"provisioned\":true"));
}

// ============================================
// STATUS API TESTS
// ============================================

void test_api_status_basic() {
    // Login first
    const char* token = login_with_test_password();
    TEST_ASSERT_NOT_NULL(token);

    state->update_system_uptime(3661);  // 1h 1m 1s
    state->update_free_heap(250000);
    state->update_wifi_rssi(-55);
    state->update_system_ntp_status(true);

    int status = server->handle_request("GET", "/api/status",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "version"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "build"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "uptime"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "free_heap"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "wifi_rssi"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "ntp_synced"));
}

void test_api_status_modbus_stats() {
    const char* token = login_with_test_password();

    state->update_modbus_stats(1000, 5);  // 1000 transactions, 5 errors

    int status = server->handle_request("GET", "/api/status",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "modbus_transactions"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "modbus_errors"));
}

// ============================================
// SENSORS API TESTS
// ============================================

void test_api_sensors_empty() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/sensors",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "sensors"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "[]"));
}

void test_api_sensors_with_data() {
    const char* token = login_with_test_password();

    state->register_sensor("temp_sensor", "°C", 0.1f);
    state->update_sensor_value(0, 18.5f, 1000);
    state->update_sensor_filtered(0, 18.5f, 18.5f);
    state->set_sensor_quality(0, SensorQuality::GOOD);

    int status = server->handle_request("GET", "/api/sensors",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "temp_sensor"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "18.5"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "GOOD"));
}

void test_api_sensor_individual() {
    const char* token = login_with_test_password();

    state->register_sensor("pressure_1", "bar", 0.01f);
    state->update_sensor_value(0, 1.25f, 2000);
    state->update_sensor_filtered(0, 1.25f, 1.25f);

    int status = server->handle_request("GET", "/api/sensor/pressure_1",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "pressure_1"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "raw_value"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "filtered_value"));
}

void test_api_sensor_not_found() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/sensor/nonexistent",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(404, status);
}

void test_api_sensors_quality_states() {
    const char* token = login_with_test_password();

    state->register_sensor("good_sensor", "°C");
    state->register_sensor("bad_sensor", "°C");
    state->register_sensor("suspect_sensor", "°C");
    state->register_sensor("warming_sensor", "°C");

    state->set_sensor_quality(0, SensorQuality::GOOD);
    state->set_sensor_quality(1, SensorQuality::BAD);
    state->set_sensor_quality(2, SensorQuality::SUSPECT);
    state->set_sensor_quality(3, SensorQuality::WARMING_UP);

    int status = server->handle_request("GET", "/api/sensors",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "GOOD"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "BAD"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "SUSPECT"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "WARMING_UP"));
}

// ============================================
// RELAYS API TESTS
// ============================================

void test_api_relays_empty() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/relays",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "relays"));
}

void test_api_relays_with_data() {
    const char* token = login_with_test_password();

    state->register_relay("chiller", RelayType::CONTACTOR_COIL);
    state->register_relay("valve1", RelayType::SOLENOID_NC);
    state->set_relay_state(0, true, 1000);

    int status = server->handle_request("GET", "/api/relays",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "chiller"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "valve1"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "true"));  // chiller is on
}

void test_api_relay_set_on() {
    const char* token = login_with_test_password();

    state->register_relay("test_relay", RelayType::SOLENOID_NC);

    int status = server->handle_request("POST", "/api/relay/test_relay",
        "{\"state\":true}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "success"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "true"));

    auto* relay = state->get_relay("test_relay");
    TEST_ASSERT_TRUE(relay->state);
}

void test_api_relay_set_off() {
    const char* token = login_with_test_password();

    state->register_relay("test_relay2", RelayType::SOLENOID_NC);
    state->set_relay_state(0, true, 1000);  // Start ON

    int status = server->handle_request("POST", "/api/relay/test_relay2",
        "{\"state\":false}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);

    auto* relay = state->get_relay("test_relay2");
    TEST_ASSERT_FALSE(relay->state);
}

void test_api_relay_not_found() {
    const char* token = login_with_test_password();

    int status = server->handle_request("POST", "/api/relay/nonexistent",
        "{\"state\":true}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(404, status);
}

// ============================================
// GPIO API TESTS
// ============================================

void test_api_inputs() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/inputs",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "inputs"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "id"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "state"));
}

void test_api_outputs() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/outputs",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "outputs"));
}

void test_api_output_set_valid() {
    const char* token = login_with_test_password();

    int status = server->handle_request("POST", "/api/output/1",
        "{\"state\":true}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "success"));
}

void test_api_output_set_invalid_id_zero() {
    const char* token = login_with_test_password();

    int status = server->handle_request("POST", "/api/output/0",
        "{\"state\":true}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
}

void test_api_output_set_invalid_id_too_high() {
    const char* token = login_with_test_password();

    int status = server->handle_request("POST", "/api/output/99",
        "{\"state\":true}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
}

// ============================================
// FERMENTER API TESTS
// ============================================

void setup_test_fermenter(uint8_t id, const char* name) {
    char temp_name[32], pressure_name[32], cool_name[32], spund_name[32];
    snprintf(temp_name, sizeof(temp_name), "%s_temp", name);
    snprintf(pressure_name, sizeof(pressure_name), "%s_pressure", name);
    snprintf(cool_name, sizeof(cool_name), "%s_cooling", name);
    snprintf(spund_name, sizeof(spund_name), "%s_spunding", name);

    state->register_sensor(temp_name, "°C");
    state->register_sensor(pressure_name, "bar");
    state->register_relay(cool_name, RelayType::SOLENOID_NC);
    state->register_relay(spund_name, RelayType::SOLENOID_NO);

    FermenterDef def;
    def.id = id;
    strncpy(def.name, name, MAX_NAME_LENGTH);
    strncpy(def.temp_sensor, temp_name, MAX_NAME_LENGTH);
    strncpy(def.pressure_sensor, pressure_name, MAX_NAME_LENGTH);
    strncpy(def.cooling_relay, cool_name, MAX_NAME_LENGTH);
    strncpy(def.spunding_relay, spund_name, MAX_NAME_LENGTH);
    state->register_fermenter(def);
}

void test_api_fermenters() {
    const char* token = login_with_test_password();

    setup_test_fermenter(1, "F1");
    setup_test_fermenter(2, "F2");

    int status = server->handle_request("GET", "/api/fermenters",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "fermenters"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "F1"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "F2"));
}

void test_api_fermenter_individual() {
    const char* token = login_with_test_password();

    setup_test_fermenter(3, "F3");
    state->update_fermenter_temps(3, 18.5f, 20.0f);
    state->update_fermenter_pressure(3, 1.2f, 1.5f);

    int status = server->handle_request("GET", "/api/fermenter/3",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "F3"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "18.5"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "setpoint"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "mode"));
}

void test_api_fermenter_not_found() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/fermenter/99",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(404, status);
}

void test_api_fermenter_set_setpoint() {
    const char* token = login_with_test_password();

    setup_test_fermenter(4, "F4");

    int status = server->handle_request("POST", "/api/fermenter/4",
        "{\"setpoint\":15.0}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);

    auto* ferm = state->get_fermenter(4);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, ferm->target_temp);
}

void test_api_fermenter_set_mode_off() {
    const char* token = login_with_test_password();

    setup_test_fermenter(5, "F5");

    int status = server->handle_request("POST", "/api/fermenter/5",
        "{\"mode\":\"OFF\"}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);

    auto* ferm = state->get_fermenter(5);
    TEST_ASSERT_EQUAL(FermenterMode::OFF, ferm->mode);
}

void test_api_fermenter_set_mode_manual() {
    const char* token = login_with_test_password();

    setup_test_fermenter(6, "F6");

    int status = server->handle_request("POST", "/api/fermenter/6",
        "{\"mode\":\"MANUAL\"}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);

    auto* ferm = state->get_fermenter(6);
    TEST_ASSERT_EQUAL(FermenterMode::MANUAL, ferm->mode);
}

void test_api_fermenter_set_mode_plan() {
    const char* token = login_with_test_password();

    setup_test_fermenter(7, "F7");

    int status = server->handle_request("POST", "/api/fermenter/7",
        "{\"mode\":\"PLAN\"}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);

    auto* ferm = state->get_fermenter(7);
    TEST_ASSERT_EQUAL(FermenterMode::PLAN, ferm->mode);
}

// ============================================
// PID API TESTS
// ============================================

void test_api_pid_get() {
    const char* token = login_with_test_password();

    setup_test_fermenter(8, "F8");

    int status = server->handle_request("GET", "/api/pid/8",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "kp"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "ki"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "kd"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "output"));
}

void test_api_pid_set() {
    const char* token = login_with_test_password();

    setup_test_fermenter(9, "F9");

    int status = server->handle_request("POST", "/api/pid/9",
        "{\"kp\":2.5,\"ki\":0.15,\"kd\":1.2}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);

    auto* ferm = state->get_fermenter(9);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, ferm->pid_params.kp);
    TEST_ASSERT_EQUAL_FLOAT(0.15f, ferm->pid_params.ki);
    TEST_ASSERT_EQUAL_FLOAT(1.2f, ferm->pid_params.kd);
}

void test_api_pid_not_found() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/pid/99",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(404, status);
}

// ============================================
// MODBUS API TESTS
// ============================================

void test_api_modbus_stats() {
    const char* token = login_with_test_password();

    state->update_modbus_stats(5000, 25);

    int status = server->handle_request("GET", "/api/modbus/stats",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "transactions"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "5000"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "errors"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "25"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "error_rate"));
}

void test_api_modbus_error_rate_calculation() {
    const char* token = login_with_test_password();

    state->update_modbus_stats(1000, 10);  // 1% error rate

    int status = server->handle_request("GET", "/api/modbus/stats",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "1.00"));  // 1% error rate
}

// ============================================
// ALARMS API TESTS
// ============================================

void test_api_alarms_empty() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/alarms",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "alarms"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "[]"));
}

// ============================================
// CONFIG API TESTS
// ============================================

void test_api_config() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/config",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "fermenter_count"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "timing"));
}

// ============================================
// MODULES API TESTS
// ============================================

void test_api_modules() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/modules",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "modules"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "wifi"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "ntp"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "http"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "mqtt"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "can"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "debug_console"));
}

// ============================================
// SYSTEM API TESTS
// ============================================

void test_api_reboot() {
    const char* token = login_with_test_password();

    int status = server->handle_request("POST", "/api/reboot",
        "", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "success"));
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "Rebooting"));
}

// ============================================
// EDGE CASES AND ERROR HANDLING
// ============================================

void test_endpoint_not_found() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/nonexistent",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(404, status);
}

void test_method_not_allowed() {
    const char* token = login_with_test_password();

    // PUT not supported on most endpoints
    int status = server->handle_request("PUT", "/api/sensors",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(404, status);
}

void test_empty_request_body() {
    const char* token = login_with_test_password();

    setup_test_fermenter(10, "F10");

    int status = server->handle_request("POST", "/api/fermenter/10",
        "", token, response_buffer, sizeof(response_buffer));

    // Should still succeed (no changes made)
    TEST_ASSERT_EQUAL(200, status);
}

void test_very_long_sensor_name() {
    const char* token = login_with_test_password();

    // Create sensor with max length name
    char long_name[MAX_NAME_LENGTH + 1];
    memset(long_name, 'a', MAX_NAME_LENGTH);
    long_name[MAX_NAME_LENGTH] = '\0';

    state->register_sensor(long_name, "°C");

    int status = server->handle_request("GET", "/api/sensors",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
}

void test_special_characters_in_json() {
    provision_device();

    // JSON with escaped characters
    int status = server->handle_request("POST", "/api/login",
        "{\"password\":\"pass\\\"word\"}", nullptr, response_buffer, sizeof(response_buffer));

    // Should fail because password doesn't match
    TEST_ASSERT_EQUAL(401, status);
}

void test_unicode_in_json() {
    provision_device();

    int status = server->handle_request("POST", "/api/login",
        "{\"password\":\"päss\"}", nullptr, response_buffer, sizeof(response_buffer));

    // Should fail because password doesn't match
    TEST_ASSERT_EQUAL(401, status);
}

void test_null_body_pointer() {
    const char* token = login_with_test_password();

    int status = server->handle_request("GET", "/api/status",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
}

void test_small_response_buffer() {
    const char* token = login_with_test_password();

    // Use very small buffer
    char small_buffer[32];

    int status = server->handle_request("GET", "/api/status",
        nullptr, token, small_buffer, sizeof(small_buffer));

    // Should still return success (response truncated)
    TEST_ASSERT_EQUAL(200, status);
}

void test_many_sensors() {
    const char* token = login_with_test_password();

    // Register many sensors
    for (int i = 0; i < 20; i++) {
        char name[32];
        snprintf(name, sizeof(name), "sensor_%d", i);
        state->register_sensor(name, "°C");
    }

    int status = server->handle_request("GET", "/api/sensors",
        nullptr, token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
}

void test_concurrent_session_replacement() {
    provision_device();

    // First login
    char body[128];
    snprintf(body, sizeof(body), "{\"password\":\"%s\"}", TEST_PASSWORD);
    server->handle_request("POST", "/api/login",
        body, nullptr, response_buffer, sizeof(response_buffer));

    // Extract first token from response
    const char* token1_json = strstr(response_buffer, "\"token\":\"");
    TEST_ASSERT_NOT_NULL(token1_json);
    char token1[128];
    token1_json += 9;  // Skip past "token":"
    int i = 0;
    while (token1_json[i] && token1_json[i] != '"' && i < 127) {
        token1[i] = token1_json[i];
        i++;
    }
    token1[i] = '\0';

    // Advance time slightly
    sim_time.advance_millis(1000);

    // Second login (should replace first session)
    server->handle_request("POST", "/api/login",
        body, nullptr, response_buffer, sizeof(response_buffer));

    // Extract second token
    const char* token2_json = strstr(response_buffer, "\"token\":\"");
    TEST_ASSERT_NOT_NULL(token2_json);
    char token2[128];
    token2_json += 9;
    i = 0;
    while (token2_json[i] && token2_json[i] != '"' && i < 127) {
        token2[i] = token2_json[i];
        i++;
    }
    token2[i] = '\0';

    // Token1 should be invalid now
    int status = server->handle_request("GET", "/api/status",
        nullptr, token1, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(401, status);

    // Token2 should work
    status = server->handle_request("GET", "/api/status",
        nullptr, token2, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(200, status);
}

// ============================================
// FIRST-BOOT PROVISIONING TESTS
// ============================================

void test_not_provisioned_login_rejected() {
    // Fresh server should not be provisioned
    TEST_ASSERT_FALSE(server->is_provisioned());

    // Login should fail with 403 (not provisioned)
    int status = server->handle_request("POST", "/api/login",
        "{\"password\":\"anything\"}", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(403, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "not provisioned"));
}

void test_setup_endpoint_creates_password() {
    // Setup with valid password
    int status = server->handle_request("POST", "/api/setup",
        "{\"password\":\"ValidPass1\"}", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "success"));
    TEST_ASSERT_TRUE(server->is_provisioned());
}

void test_setup_rejects_weak_password_too_short() {
    int status = server->handle_request("POST", "/api/setup",
        "{\"password\":\"Abc123\"}", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "requirements"));
    TEST_ASSERT_FALSE(server->is_provisioned());
}

void test_setup_rejects_password_one_category() {
    // Only lowercase
    int status = server->handle_request("POST", "/api/setup",
        "{\"password\":\"alllowercase\"}", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
    TEST_ASSERT_FALSE(server->is_provisioned());
}

void test_setup_cannot_provision_twice() {
    // First provision
    server->handle_request("POST", "/api/setup",
        "{\"password\":\"ValidPass1\"}", nullptr, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_TRUE(server->is_provisioned());

    // Try to provision again
    int status = server->handle_request("POST", "/api/setup",
        "{\"password\":\"DifferentPass2\"}", nullptr, response_buffer, sizeof(response_buffer));

    // Implementation returns 400 with "already provisioned" message
    TEST_ASSERT_EQUAL(400, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "already provisioned"));
}

void test_provisioned_login_works() {
    // Setup first
    server->handle_request("POST", "/api/setup",
        "{\"password\":\"MySecure123\"}", nullptr, response_buffer, sizeof(response_buffer));

    // Login should now work
    int status = server->handle_request("POST", "/api/login",
        "{\"password\":\"MySecure123\"}", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "token"));
}

// ============================================
// RATE LIMITING TESTS
// ============================================

void test_rate_limit_blocks_after_failures() {
    provision_device();

    // Make several failed login attempts
    for (int i = 0; i < 5; i++) {
        server->handle_request("POST", "/api/login",
            "{\"password\":\"WrongPass1\"}", nullptr, response_buffer, sizeof(response_buffer));
    }

    // Next attempt should be rate limited (429 Too Many Requests)
    int status = server->handle_request("POST", "/api/login",
        "{\"password\":\"WrongPass1\"}", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(429, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "Too many failed attempts"));
}

void test_rate_limit_resets_after_success() {
    provision_device();

    // Make a few failed attempts
    for (int i = 0; i < 3; i++) {
        server->handle_request("POST", "/api/login",
            "{\"password\":\"WrongPass1\"}", nullptr, response_buffer, sizeof(response_buffer));
    }

    // Wait for any delay
    sim_time.advance_millis(5000);

    // Successful login
    char body[128];
    snprintf(body, sizeof(body), "{\"password\":\"%s\"}", TEST_PASSWORD);
    int status = server->handle_request("POST", "/api/login",
        body, nullptr, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(200, status);

    // Failure counter should be reset - next failure shouldn't trigger rate limit
    status = server->handle_request("POST", "/api/login",
        "{\"password\":\"WrongPass1\"}", nullptr, response_buffer, sizeof(response_buffer));
    TEST_ASSERT_EQUAL(401, status);  // Regular failure, not 429
}

void test_lockout_after_many_failures() {
    provision_device();

    // Make many failed attempts (trigger lockout)
    for (int i = 0; i < 15; i++) {
        sim_time.advance_millis(2000);  // Advance time to bypass rate limits
        server->handle_request("POST", "/api/login",
            "{\"password\":\"WrongPass1\"}", nullptr, response_buffer, sizeof(response_buffer));
    }

    // Should be locked out
    int status = server->handle_request("POST", "/api/login",
        "{\"password\":\"WrongPass1\"}", nullptr, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_TRUE(status == 429 || status == 403);
}

// ============================================
// PATH TRAVERSAL TESTS
// ============================================

void test_path_traversal_rejected_in_url() {
    const char* token = login_with_test_password();

    // Try to access parent directory
    int status = server->handle_request("GET", "/api/../private/secret",
        nullptr, token, response_buffer, sizeof(response_buffer));

    // Should be rejected (400 or 403)
    TEST_ASSERT_TRUE(status == 400 || status == 403 || status == 404);
}

void test_path_traversal_sensor_name() {
    const char* token = login_with_test_password();

    // Try path traversal in sensor name
    int status = server->handle_request("GET", "/api/sensor/../../etc/passwd",
        nullptr, token, response_buffer, sizeof(response_buffer));

    // Should not expose file system - return 404 or 400
    TEST_ASSERT_TRUE(status == 400 || status == 404);
    TEST_ASSERT_NULL(strstr(response_buffer, "root"));
}

// ============================================
// SECURE_UTILS UNIT TESTS
// ============================================

void test_secure_compare_equal() {
    uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t b[] = {1, 2, 3, 4, 5};
    TEST_ASSERT_TRUE(security::secure_compare(a, b, 5));
}

void test_secure_compare_different() {
    uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t b[] = {1, 2, 3, 4, 6};
    TEST_ASSERT_FALSE(security::secure_compare(a, b, 5));
}

void test_secure_strcmp_equal() {
    TEST_ASSERT_TRUE(security::secure_strcmp("hello", "hello"));
}

void test_secure_strcmp_different() {
    TEST_ASSERT_FALSE(security::secure_strcmp("hello", "world"));
}

void test_secure_strcmp_different_lengths() {
    TEST_ASSERT_FALSE(security::secure_strcmp("hello", "hello!"));
    TEST_ASSERT_FALSE(security::secure_strcmp("hello!", "hello"));
}

void test_secure_strcmp_null_inputs() {
    TEST_ASSERT_FALSE(security::secure_strcmp(nullptr, "hello"));
    TEST_ASSERT_FALSE(security::secure_strcmp("hello", nullptr));
    TEST_ASSERT_FALSE(security::secure_strcmp(nullptr, nullptr));
}

void test_secure_strcmp_empty_strings() {
    TEST_ASSERT_TRUE(security::secure_strcmp("", ""));
}

void test_hash_password_produces_output() {
    char hash[security::PASSWORD_HASH_BUF_SIZE];
    TEST_ASSERT_TRUE(security::hash_password("test123", "salt", hash));
    TEST_ASSERT_EQUAL(security::PASSWORD_HASH_LEN, strlen(hash));
}

void test_hash_password_deterministic() {
    char hash1[security::PASSWORD_HASH_BUF_SIZE];
    char hash2[security::PASSWORD_HASH_BUF_SIZE];

    security::hash_password("password", "salt", hash1);
    security::hash_password("password", "salt", hash2);

    TEST_ASSERT_EQUAL_STRING(hash1, hash2);
}

void test_hash_password_different_salt() {
    char hash1[security::PASSWORD_HASH_BUF_SIZE];
    char hash2[security::PASSWORD_HASH_BUF_SIZE];

    security::hash_password("password", "salt1", hash1);
    security::hash_password("password", "salt2", hash2);

    TEST_ASSERT_TRUE(strcmp(hash1, hash2) != 0);
}

void test_verify_password_success() {
    char hash[security::PASSWORD_HASH_BUF_SIZE];
    security::hash_password("MyPassword", "device123", hash);

    TEST_ASSERT_TRUE(security::verify_password("MyPassword", "device123", hash));
}

void test_verify_password_failure() {
    char hash[security::PASSWORD_HASH_BUF_SIZE];
    security::hash_password("MyPassword", "device123", hash);

    TEST_ASSERT_FALSE(security::verify_password("WrongPassword", "device123", hash));
}

void test_validate_password_strength_valid() {
    TEST_ASSERT_TRUE(security::validate_password_strength("Password1"));
    TEST_ASSERT_TRUE(security::validate_password_strength("UPPER123"));
    TEST_ASSERT_TRUE(security::validate_password_strength("lower123"));
    TEST_ASSERT_TRUE(security::validate_password_strength("UpperLower"));
}

void test_validate_password_strength_too_short() {
    TEST_ASSERT_FALSE(security::validate_password_strength("Pass1"));
    TEST_ASSERT_FALSE(security::validate_password_strength("Aa1"));
}

void test_validate_password_strength_one_category() {
    TEST_ASSERT_FALSE(security::validate_password_strength("alllowercase"));
    TEST_ASSERT_FALSE(security::validate_password_strength("ALLUPPERCASE"));
    TEST_ASSERT_FALSE(security::validate_password_strength("12345678"));
}

void test_validate_password_strength_null() {
    TEST_ASSERT_FALSE(security::validate_password_strength(nullptr));
}

void test_normalize_path_basic() {
    char output[128];
    TEST_ASSERT_TRUE(security::normalize_path("/foo/bar", "/base", output, sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("/base/foo/bar", output);
}

void test_normalize_path_rejects_dotdot() {
    char output[128];
    TEST_ASSERT_FALSE(security::normalize_path("/../etc/passwd", "/base", output, sizeof(output)));
    TEST_ASSERT_FALSE(security::normalize_path("/foo/../bar", "/base", output, sizeof(output)));
}

void test_normalize_path_rejects_double_slash() {
    char output[128];
    TEST_ASSERT_FALSE(security::normalize_path("//foo/bar", "/base", output, sizeof(output)));
}

void test_path_within_base_valid() {
    TEST_ASSERT_TRUE(security::path_within_base("/base/foo", "/base"));
    TEST_ASSERT_TRUE(security::path_within_base("/base", "/base"));
}

void test_path_within_base_invalid() {
    TEST_ASSERT_FALSE(security::path_within_base("/other/foo", "/base"));
    TEST_ASSERT_FALSE(security::path_within_base("/base_other", "/base"));
}

void test_generate_session_token_length() {
    char token[security::SESSION_TOKEN_BUF_SIZE];
    security::generate_session_token(token);
    TEST_ASSERT_EQUAL(security::SESSION_TOKEN_LEN, strlen(token));
}

void test_generate_session_token_unique() {
    char token1[security::SESSION_TOKEN_BUF_SIZE];
    char token2[security::SESSION_TOKEN_BUF_SIZE];

    security::generate_session_token(token1);
    security::generate_session_token(token2);

    // Tokens should be different (extremely unlikely to be the same)
    TEST_ASSERT_TRUE(strcmp(token1, token2) != 0);
}

// ============================================
// SECURITY VALIDATION TESTS (#42, #48, #53)
// ============================================

// #42 - URL scheme validation for OTA downloads
// Note: In simulator without OTA_ENABLED, these endpoints return 404
// When OTA is enabled, invalid URL schemes return 400 with "Invalid URL scheme"
void test_firmware_download_rejects_ftp_url() {
    const char* token = login_with_test_password();

    int status = server->handle_request("POST", "/api/firmware/download",
        "{\"url\":\"ftp://evil.com/firmware.bin\"}", token, response_buffer, sizeof(response_buffer));

    // Expect 400 (invalid scheme) when OTA enabled, or 404 (not found) when disabled
    TEST_ASSERT_TRUE(status == 400 || status == 404);
    if (status == 400) {
        TEST_ASSERT_NOT_NULL(strstr(response_buffer, "Invalid URL scheme"));
    }
}

void test_firmware_download_rejects_file_url() {
    const char* token = login_with_test_password();

    int status = server->handle_request("POST", "/api/firmware/download",
        "{\"url\":\"file:///etc/passwd\"}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_TRUE(status == 400 || status == 404);
    if (status == 400) {
        TEST_ASSERT_NOT_NULL(strstr(response_buffer, "Invalid URL scheme"));
    }
}

void test_firmware_download_rejects_javascript_url() {
    const char* token = login_with_test_password();

    int status = server->handle_request("POST", "/api/firmware/download",
        "{\"url\":\"javascript:alert(1)\"}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_TRUE(status == 400 || status == 404);
    if (status == 400) {
        TEST_ASSERT_NOT_NULL(strstr(response_buffer, "Invalid URL scheme"));
    }
}

void test_firmware_download_accepts_http_url() {
    const char* token = login_with_test_password();

    // Note: This will fail at actual download (no server), but should pass URL validation
    int status = server->handle_request("POST", "/api/firmware/download",
        "{\"url\":\"http://example.com/firmware.bin\"}", token, response_buffer, sizeof(response_buffer));

    // Should not be 400 with "Invalid URL scheme" - may be 404 (OTA disabled) or other error
    if (status == 400) {
        TEST_ASSERT_NULL(strstr(response_buffer, "Invalid URL scheme"));
    }
}

void test_firmware_download_accepts_https_url() {
    const char* token = login_with_test_password();

    int status = server->handle_request("POST", "/api/firmware/download",
        "{\"url\":\"https://example.com/firmware.bin\"}", token, response_buffer, sizeof(response_buffer));

    // Should not be 400 with "Invalid URL scheme"
    if (status == 400) {
        TEST_ASSERT_NULL(strstr(response_buffer, "Invalid URL scheme"));
    }
}

// #48 - Numeric bounds checking for setpoints
void test_fermenter_setpoint_rejects_too_low() {
    const char* token = login_with_test_password();

    setup_test_fermenter(10, "F10");

    int status = server->handle_request("POST", "/api/fermenter/10",
        "{\"setpoint\":-15.0}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "out of range"));
}

void test_fermenter_setpoint_rejects_too_high() {
    const char* token = login_with_test_password();

    setup_test_fermenter(11, "F11");

    int status = server->handle_request("POST", "/api/fermenter/11",
        "{\"setpoint\":55.0}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "out of range"));
}

void test_fermenter_setpoint_accepts_boundary_low() {
    const char* token = login_with_test_password();

    setup_test_fermenter(12, "F12");

    int status = server->handle_request("POST", "/api/fermenter/12",
        "{\"setpoint\":-10.0}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);

    auto* ferm = state->get_fermenter(12);
    TEST_ASSERT_EQUAL_FLOAT(-10.0f, ferm->target_temp);
}

void test_fermenter_setpoint_accepts_boundary_high() {
    const char* token = login_with_test_password();

    setup_test_fermenter(13, "F13");

    int status = server->handle_request("POST", "/api/fermenter/13",
        "{\"setpoint\":50.0}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);

    auto* ferm = state->get_fermenter(13);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, ferm->target_temp);
}

// #48 - Numeric bounds checking for PID parameters
void test_pid_kp_rejects_negative() {
    const char* token = login_with_test_password();

    setup_test_fermenter(14, "F14");

    int status = server->handle_request("POST", "/api/pid/14",
        "{\"kp\":-1.0}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "out of range"));
}

void test_pid_kp_rejects_too_high() {
    const char* token = login_with_test_password();

    setup_test_fermenter(15, "F15");

    int status = server->handle_request("POST", "/api/pid/15",
        "{\"kp\":150.0}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "out of range"));
}

void test_pid_ki_rejects_too_high() {
    const char* token = login_with_test_password();

    setup_test_fermenter(16, "F16");

    int status = server->handle_request("POST", "/api/pid/16",
        "{\"ki\":60.0}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "out of range"));
}

void test_pid_kd_rejects_too_high() {
    const char* token = login_with_test_password();

    setup_test_fermenter(17, "F17");

    int status = server->handle_request("POST", "/api/pid/17",
        "{\"kd\":25.0}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(400, status);
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "out of range"));
}

void test_pid_accepts_valid_params() {
    const char* token = login_with_test_password();

    setup_test_fermenter(18, "F18");

    int status = server->handle_request("POST", "/api/pid/18",
        "{\"kp\":50.0,\"ki\":25.0,\"kd\":10.0}", token, response_buffer, sizeof(response_buffer));

    TEST_ASSERT_EQUAL(200, status);

    auto* ferm = state->get_fermenter(18);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, ferm->pid_params.kp);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, ferm->pid_params.ki);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, ferm->pid_params.kd);
}

// ============================================
// MAIN
// ============================================

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // Authentication tests
    RUN_TEST(test_login_success);
    RUN_TEST(test_login_wrong_password);
    RUN_TEST(test_login_empty_password);
    RUN_TEST(test_login_missing_password_field);
    RUN_TEST(test_login_malformed_json);
    RUN_TEST(test_login_empty_body);
    RUN_TEST(test_custom_password);
    RUN_TEST(test_logout);
    RUN_TEST(test_session_timeout);
    RUN_TEST(test_session_refresh_on_activity);

    // Authorization tests
    RUN_TEST(test_unauthorized_access_without_token);
    RUN_TEST(test_unauthorized_access_with_invalid_token);
    RUN_TEST(test_health_endpoint_no_auth_required);
    RUN_TEST(test_health_shows_not_provisioned);
    RUN_TEST(test_health_shows_provisioned_after_setup);

    // Status API tests
    RUN_TEST(test_api_status_basic);
    RUN_TEST(test_api_status_modbus_stats);

    // Sensors API tests
    RUN_TEST(test_api_sensors_empty);
    RUN_TEST(test_api_sensors_with_data);
    RUN_TEST(test_api_sensor_individual);
    RUN_TEST(test_api_sensor_not_found);
    RUN_TEST(test_api_sensors_quality_states);

    // Relays API tests
    RUN_TEST(test_api_relays_empty);
    RUN_TEST(test_api_relays_with_data);
    RUN_TEST(test_api_relay_set_on);
    RUN_TEST(test_api_relay_set_off);
    RUN_TEST(test_api_relay_not_found);

    // GPIO API tests
    RUN_TEST(test_api_inputs);
    RUN_TEST(test_api_outputs);
    RUN_TEST(test_api_output_set_valid);
    RUN_TEST(test_api_output_set_invalid_id_zero);
    RUN_TEST(test_api_output_set_invalid_id_too_high);

    // Fermenter API tests
    RUN_TEST(test_api_fermenters);
    RUN_TEST(test_api_fermenter_individual);
    RUN_TEST(test_api_fermenter_not_found);
    RUN_TEST(test_api_fermenter_set_setpoint);
    RUN_TEST(test_api_fermenter_set_mode_off);
    RUN_TEST(test_api_fermenter_set_mode_manual);
    RUN_TEST(test_api_fermenter_set_mode_plan);

    // PID API tests
    RUN_TEST(test_api_pid_get);
    RUN_TEST(test_api_pid_set);
    RUN_TEST(test_api_pid_not_found);

    // MODBUS API tests
    RUN_TEST(test_api_modbus_stats);
    RUN_TEST(test_api_modbus_error_rate_calculation);

    // Alarms API tests
    RUN_TEST(test_api_alarms_empty);

    // Config API tests
    RUN_TEST(test_api_config);

    // Modules API tests
    RUN_TEST(test_api_modules);

    // System API tests
    RUN_TEST(test_api_reboot);

    // Edge cases and error handling
    RUN_TEST(test_endpoint_not_found);
    RUN_TEST(test_method_not_allowed);
    RUN_TEST(test_empty_request_body);
    RUN_TEST(test_very_long_sensor_name);
    RUN_TEST(test_special_characters_in_json);
    RUN_TEST(test_unicode_in_json);
    RUN_TEST(test_null_body_pointer);
    RUN_TEST(test_small_response_buffer);
    RUN_TEST(test_many_sensors);
    RUN_TEST(test_concurrent_session_replacement);

    // First-boot provisioning tests
    RUN_TEST(test_not_provisioned_login_rejected);
    RUN_TEST(test_setup_endpoint_creates_password);
    RUN_TEST(test_setup_rejects_weak_password_too_short);
    RUN_TEST(test_setup_rejects_password_one_category);
    RUN_TEST(test_setup_cannot_provision_twice);
    RUN_TEST(test_provisioned_login_works);

    // Rate limiting tests
    RUN_TEST(test_rate_limit_blocks_after_failures);
    RUN_TEST(test_rate_limit_resets_after_success);
    RUN_TEST(test_lockout_after_many_failures);

    // Path traversal tests
    RUN_TEST(test_path_traversal_rejected_in_url);
    RUN_TEST(test_path_traversal_sensor_name);

    // secure_utils unit tests
    RUN_TEST(test_secure_compare_equal);
    RUN_TEST(test_secure_compare_different);
    RUN_TEST(test_secure_strcmp_equal);
    RUN_TEST(test_secure_strcmp_different);
    RUN_TEST(test_secure_strcmp_different_lengths);
    RUN_TEST(test_secure_strcmp_null_inputs);
    RUN_TEST(test_secure_strcmp_empty_strings);
    RUN_TEST(test_hash_password_produces_output);
    RUN_TEST(test_hash_password_deterministic);
    RUN_TEST(test_hash_password_different_salt);
    RUN_TEST(test_verify_password_success);
    RUN_TEST(test_verify_password_failure);
    RUN_TEST(test_validate_password_strength_valid);
    RUN_TEST(test_validate_password_strength_too_short);
    RUN_TEST(test_validate_password_strength_one_category);
    RUN_TEST(test_validate_password_strength_null);
    RUN_TEST(test_normalize_path_basic);
    RUN_TEST(test_normalize_path_rejects_dotdot);
    RUN_TEST(test_normalize_path_rejects_double_slash);
    RUN_TEST(test_path_within_base_valid);
    RUN_TEST(test_path_within_base_invalid);
    RUN_TEST(test_generate_session_token_length);
    RUN_TEST(test_generate_session_token_unique);

    // Security validation tests (#42, #48)
    RUN_TEST(test_firmware_download_rejects_ftp_url);
    RUN_TEST(test_firmware_download_rejects_file_url);
    RUN_TEST(test_firmware_download_rejects_javascript_url);
    RUN_TEST(test_firmware_download_accepts_http_url);
    RUN_TEST(test_firmware_download_accepts_https_url);
    RUN_TEST(test_fermenter_setpoint_rejects_too_low);
    RUN_TEST(test_fermenter_setpoint_rejects_too_high);
    RUN_TEST(test_fermenter_setpoint_accepts_boundary_low);
    RUN_TEST(test_fermenter_setpoint_accepts_boundary_high);
    RUN_TEST(test_pid_kp_rejects_negative);
    RUN_TEST(test_pid_kp_rejects_too_high);
    RUN_TEST(test_pid_ki_rejects_too_high);
    RUN_TEST(test_pid_kd_rejects_too_high);
    RUN_TEST(test_pid_accepts_valid_params);

    return UNITY_END();
}
