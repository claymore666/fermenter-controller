#include <unity.h>
#include "hal/simulator/hal_simulator.h"
#include "modules/wifi_module.h"
#include "modules/ntp_module.h"
#include "modules/wifi_provisioning.h"

// Test fixtures
static hal::simulator::SimulatorNetwork* network_hal;
static hal::simulator::SimulatorTime* time_hal;
static modules::WifiModule* wifi_module;
static modules::NtpModule* ntp_module;
static modules::WifiProvisioning* wifi_prov;

void setUp(void) {
    network_hal = new hal::simulator::SimulatorNetwork();
    time_hal = new hal::simulator::SimulatorTime();
    wifi_module = new modules::WifiModule(network_hal, time_hal);
    ntp_module = new modules::NtpModule(time_hal);
    wifi_prov = new modules::WifiProvisioning(time_hal);
}

void tearDown(void) {
    delete wifi_prov;
    delete ntp_module;
    delete wifi_module;
    delete time_hal;
    delete network_hal;
}

// =============================================================================
// WiFi Module Tests
// =============================================================================

void test_wifi_initial_state() {
    TEST_ASSERT_EQUAL(modules::WifiModule::State::DISCONNECTED, wifi_module->get_state());
    TEST_ASSERT_FALSE(wifi_module->is_connected());
    TEST_ASSERT_NULL(wifi_module->get_ip_address());
}

void test_wifi_configure() {
    wifi_module->configure("TestNetwork", "password123", "fermenter-1");

    const auto& config = wifi_module->get_config();
    TEST_ASSERT_EQUAL_STRING("TestNetwork", config.ssid);
    TEST_ASSERT_EQUAL_STRING("password123", config.password);
    TEST_ASSERT_EQUAL_STRING("fermenter-1", config.hostname);
}

void test_wifi_connect_success() {
    wifi_module->configure("TestNetwork", "password123");

    bool result = wifi_module->connect();

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(wifi_module->is_connected());
    TEST_ASSERT_EQUAL(modules::WifiModule::State::CONNECTED, wifi_module->get_state());
    TEST_ASSERT_NOT_NULL(wifi_module->get_ip_address());
    TEST_ASSERT_EQUAL(1, wifi_module->get_connect_count());
}

void test_wifi_connect_without_config() {
    // No SSID configured
    bool result = wifi_module->connect();

    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(wifi_module->is_connected());
}

void test_wifi_disconnect() {
    wifi_module->configure("TestNetwork", "password123");
    wifi_module->connect();

    wifi_module->disconnect();

    TEST_ASSERT_FALSE(wifi_module->is_connected());
    TEST_ASSERT_EQUAL(modules::WifiModule::State::DISCONNECTED, wifi_module->get_state());
    TEST_ASSERT_EQUAL(1, wifi_module->get_disconnect_count());
}

void test_wifi_rssi() {
    wifi_module->configure("TestNetwork", "password123");
    wifi_module->connect();

    // Simulator defaults to -50 dBm
    int rssi = wifi_module->get_rssi();
    TEST_ASSERT_EQUAL(-50, rssi);

    // Signal quality should be 100% at -50 dBm
    int quality = wifi_module->get_signal_quality();
    TEST_ASSERT_EQUAL(100, quality);
}

void test_wifi_signal_quality_calculation() {
    wifi_module->configure("TestNetwork", "password123");
    wifi_module->connect();

    // Test different RSSI values
    network_hal->set_rssi(-100);
    TEST_ASSERT_EQUAL(0, wifi_module->get_signal_quality());

    network_hal->set_rssi(-75);
    TEST_ASSERT_EQUAL(50, wifi_module->get_signal_quality());

    network_hal->set_rssi(-50);
    TEST_ASSERT_EQUAL(100, wifi_module->get_signal_quality());
}

void test_wifi_auto_reconnect() {
    wifi_module->configure("TestNetwork", "password123");
    wifi_module->set_reconnect_interval(5000);  // 5 seconds
    wifi_module->connect();

    // Simulate disconnect
    network_hal->set_connected(false);
    wifi_module->update();

    TEST_ASSERT_EQUAL(modules::WifiModule::State::DISCONNECTED, wifi_module->get_state());

    // Advance time past reconnect interval
    time_hal->advance_millis(6000);
    wifi_module->update();

    // Should have attempted reconnection
    TEST_ASSERT_EQUAL(2, wifi_module->get_connect_count());
}

void test_wifi_state_string() {
    TEST_ASSERT_EQUAL_STRING("DISCONNECTED", wifi_module->get_state_string());

    wifi_module->configure("TestNetwork", "password123");
    wifi_module->connect();

    TEST_ASSERT_EQUAL_STRING("CONNECTED", wifi_module->get_state_string());
}

void test_wifi_uptime() {
    wifi_module->configure("TestNetwork", "password123");
    wifi_module->connect();

    time_hal->advance_millis(10000);

    uint32_t uptime = wifi_module->get_uptime_ms();
    TEST_ASSERT_EQUAL(10000, uptime);
}

// =============================================================================
// NTP Module Tests
// =============================================================================

void test_ntp_initial_state() {
    TEST_ASSERT_EQUAL(modules::NtpModule::SyncStatus::NOT_STARTED, ntp_module->get_status());
}

void test_ntp_init() {
    bool result = ntp_module->init();

    TEST_ASSERT_TRUE(result);
    // Simulator marks as synced immediately
    TEST_ASSERT_EQUAL(modules::NtpModule::SyncStatus::SYNCED, ntp_module->get_status());
}

void test_ntp_is_synced() {
    ntp_module->init();

    TEST_ASSERT_TRUE(ntp_module->is_synced());
    TEST_ASSERT_EQUAL(1, ntp_module->get_sync_count());
}

void test_ntp_get_unix_time() {
    time_hal->set_unix_time(1700000000);

    uint32_t t = ntp_module->get_unix_time();
    TEST_ASSERT_EQUAL(1700000000, t);
}

void test_ntp_get_local_time() {
    // Set to a known time: 2023-11-14 22:13:20 UTC
    time_hal->set_unix_time(1700000000);

    struct tm local = ntp_module->get_local_time();

    TEST_ASSERT_EQUAL(2023 - 1900, local.tm_year);
    TEST_ASSERT_EQUAL(10, local.tm_mon);  // November (0-indexed)
    TEST_ASSERT_EQUAL(14, local.tm_mday);
}

void test_ntp_format_iso8601() {
    time_hal->set_unix_time(1700000000);

    char buffer[30];
    ntp_module->format_iso8601(buffer, sizeof(buffer));

    // Should contain date/time components
    TEST_ASSERT_NOT_NULL(strstr(buffer, "2023"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "11"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "14"));
}

void test_ntp_seconds_since_sync() {
    ntp_module->init();

    // Advance time by 1 hour
    time_hal->advance_unix_time(3600);

    uint32_t elapsed = ntp_module->get_seconds_since_sync();
    TEST_ASSERT_EQUAL(3600, elapsed);
}

void test_ntp_needs_resync() {
    modules::NtpModule::Config config;
    config.server = "pool.ntp.org";
    config.timezone = "UTC0";
    config.sync_interval_ms = 3600000;  // 1 hour
    config.boot_timeout_ms = 10000;

    ntp_module->configure(config);
    ntp_module->init();

    // Immediately after sync, should not need resync
    TEST_ASSERT_FALSE(ntp_module->needs_resync());

    // Advance time past sync interval
    time_hal->advance_unix_time(3601);

    TEST_ASSERT_TRUE(ntp_module->needs_resync());
}

void test_ntp_wait_for_sync() {
    bool result = ntp_module->init();
    TEST_ASSERT_TRUE(result);

    result = ntp_module->wait_for_sync(5000);
    TEST_ASSERT_TRUE(result);
}

void test_ntp_statistics() {
    ntp_module->init();

    TEST_ASSERT_EQUAL(1, ntp_module->get_sync_count());
    TEST_ASSERT_EQUAL(0, ntp_module->get_fail_count());
    TEST_ASSERT_TRUE(ntp_module->get_last_sync_time() > 0);
}

// =============================================================================
// WiFi Provisioning Tests
// =============================================================================

void test_prov_initial_state() {
    TEST_ASSERT_EQUAL(modules::WifiProvisioning::State::IDLE, wifi_prov->get_state());
    TEST_ASSERT_FALSE(wifi_prov->is_connected());
    TEST_ASSERT_FALSE(wifi_prov->is_provisioning());
}

void test_prov_init() {
    TEST_ASSERT_TRUE(wifi_prov->init());
}

void test_prov_start_connects() {
    wifi_prov->init();
    bool result = wifi_prov->start();

    // Simulator auto-connects
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(wifi_prov->is_connected());
}

void test_prov_provision_with_credentials() {
    wifi_prov->init();

    bool result = wifi_prov->provision("TestNetwork", "password123");

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(wifi_prov->is_connected());
    TEST_ASSERT_EQUAL_STRING("TestNetwork", wifi_prov->get_ssid());
}

void test_prov_get_ip_address() {
    wifi_prov->init();
    wifi_prov->start();

    const char* ip = wifi_prov->get_ip_address();
    TEST_ASSERT_NOT_NULL(ip);
}

void test_prov_get_ap_ip_address() {
    const char* ap_ip = wifi_prov->get_ap_ip_address();
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", ap_ip);
}

void test_prov_state_string() {
    TEST_ASSERT_EQUAL_STRING("IDLE", wifi_prov->get_state_string());

    wifi_prov->init();
    wifi_prov->start();

    TEST_ASSERT_EQUAL_STRING("CONNECTED", wifi_prov->get_state_string());
}

void test_prov_provision_method_stored() {
    wifi_prov->init();
    wifi_prov->start();

    TEST_ASSERT_EQUAL(modules::WifiProvisioning::ProvisionMethod::STORED,
                     wifi_prov->get_provision_method());
}

void test_prov_clear_credentials() {
    wifi_prov->init();
    wifi_prov->provision("TestNetwork", "password123");

    wifi_prov->clear_credentials();

    // SSID should be empty after clear
    TEST_ASSERT_EQUAL_STRING("", wifi_prov->get_ssid());
}

void test_prov_configure() {
    modules::WifiProvisioning::Config cfg;
    strncpy(cfg.ap_ssid, "MyFermenter", sizeof(cfg.ap_ssid));
    strncpy(cfg.ap_password, "secret", sizeof(cfg.ap_password));
    strncpy(cfg.hostname, "fermenter-1", sizeof(cfg.hostname));
    cfg.connect_timeout_ms = 5000;
    cfg.provision_timeout_ms = 60000;
    cfg.enable_smartconfig = true;
    cfg.enable_captive_portal = true;

    wifi_prov->configure(cfg);
    wifi_prov->init();

    // Configuration should be applied (no direct getter, but should not crash)
    TEST_ASSERT_TRUE(wifi_prov->start());
}

void test_prov_start_provisioning_mode() {
    wifi_prov->init();

    bool result = wifi_prov->start_provisioning();

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(modules::WifiProvisioning::State::PROVISIONING, wifi_prov->get_state());
    TEST_ASSERT_TRUE(wifi_prov->is_provisioning());
}

// =============================================================================
// Test Runner
// =============================================================================

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // WiFi Module tests
    RUN_TEST(test_wifi_initial_state);
    RUN_TEST(test_wifi_configure);
    RUN_TEST(test_wifi_connect_success);
    RUN_TEST(test_wifi_connect_without_config);
    RUN_TEST(test_wifi_disconnect);
    RUN_TEST(test_wifi_rssi);
    RUN_TEST(test_wifi_signal_quality_calculation);
    RUN_TEST(test_wifi_auto_reconnect);
    RUN_TEST(test_wifi_state_string);
    RUN_TEST(test_wifi_uptime);

    // NTP Module tests
    RUN_TEST(test_ntp_initial_state);
    RUN_TEST(test_ntp_init);
    RUN_TEST(test_ntp_is_synced);
    RUN_TEST(test_ntp_get_unix_time);
    RUN_TEST(test_ntp_get_local_time);
    RUN_TEST(test_ntp_format_iso8601);
    RUN_TEST(test_ntp_seconds_since_sync);
    RUN_TEST(test_ntp_needs_resync);
    RUN_TEST(test_ntp_wait_for_sync);
    RUN_TEST(test_ntp_statistics);

    // WiFi Provisioning tests
    RUN_TEST(test_prov_initial_state);
    RUN_TEST(test_prov_init);
    RUN_TEST(test_prov_start_connects);
    RUN_TEST(test_prov_provision_with_credentials);
    RUN_TEST(test_prov_get_ip_address);
    RUN_TEST(test_prov_get_ap_ip_address);
    RUN_TEST(test_prov_state_string);
    RUN_TEST(test_prov_provision_method_stored);
    RUN_TEST(test_prov_clear_credentials);
    RUN_TEST(test_prov_configure);
    RUN_TEST(test_prov_start_provisioning_mode);

    return UNITY_END();
}
