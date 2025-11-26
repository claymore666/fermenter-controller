#include <unity.h>
#include "modules/mdns_service.h"
#include <cstring>

// Test fixture
static modules::MdnsService* mdns_service;

void setUp(void) {
    mdns_service = new modules::MdnsService();
}

void tearDown(void) {
    delete mdns_service;
}

// =============================================================================
// MdnsService Basic Tests
// =============================================================================

void test_mdns_init_success() {
    bool result = mdns_service->init();
    TEST_ASSERT_TRUE(result);
}

void test_mdns_is_initialized_after_init() {
    mdns_service->init();
    TEST_ASSERT_TRUE(mdns_service->is_initialized());
}

void test_mdns_double_init_succeeds() {
    // First init
    TEST_ASSERT_TRUE(mdns_service->init());
    // Second init should also succeed (idempotent)
    TEST_ASSERT_TRUE(mdns_service->init());
}

// =============================================================================
// Hostname Tests
// =============================================================================

void test_mdns_get_hostname() {
    mdns_service->init();
    const char* hostname = mdns_service->get_hostname();

    TEST_ASSERT_NOT_NULL(hostname);
    TEST_ASSERT_EQUAL_STRING("fermenter-SIM", hostname);
}

void test_mdns_get_fqdn() {
    mdns_service->init();

    char buffer[64];
    size_t len = mdns_service->get_fqdn(buffer, sizeof(buffer));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("fermenter-SIM.local", buffer);
}

void test_mdns_get_fqdn_null_buffer() {
    mdns_service->init();

    size_t len = mdns_service->get_fqdn(nullptr, 64);
    TEST_ASSERT_EQUAL(0, len);
}

void test_mdns_get_fqdn_small_buffer() {
    mdns_service->init();

    char buffer[10];  // Too small for FQDN
    size_t len = mdns_service->get_fqdn(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(0, len);
}

void test_mdns_get_fqdn_exact_size() {
    mdns_service->init();

    char buffer[32];  // Minimum required
    size_t len = mdns_service->get_fqdn(buffer, sizeof(buffer));
    TEST_ASSERT_GREATER_THAN(0, len);
}

// =============================================================================
// Static Hostname Generation Tests
// =============================================================================

void test_mdns_generate_hostname_static() {
    char buffer[32];
    size_t len = modules::MdnsService::generate_hostname(buffer, sizeof(buffer));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("fermenter-SIM", buffer);
}

void test_mdns_generate_hostname_null_buffer() {
    size_t len = modules::MdnsService::generate_hostname(nullptr, 32);
    TEST_ASSERT_EQUAL(0, len);
}

void test_mdns_generate_hostname_small_buffer() {
    char buffer[10];  // Too small
    size_t len = modules::MdnsService::generate_hostname(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(0, len);
}

// =============================================================================
// Stop/Cleanup Tests
// =============================================================================

void test_mdns_stop() {
    mdns_service->init();
    TEST_ASSERT_TRUE(mdns_service->is_initialized());

    mdns_service->stop();
    // Simulator stub doesn't change initialized state, but stop() should not crash
    // In real ESP32, this would set initialized_ to false
}

void test_mdns_stop_without_init() {
    // Should not crash when stopping without init
    mdns_service->stop();
}

// =============================================================================
// Test Runner
// =============================================================================

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    // Basic tests
    RUN_TEST(test_mdns_init_success);
    RUN_TEST(test_mdns_is_initialized_after_init);
    RUN_TEST(test_mdns_double_init_succeeds);

    // Hostname tests
    RUN_TEST(test_mdns_get_hostname);
    RUN_TEST(test_mdns_get_fqdn);
    RUN_TEST(test_mdns_get_fqdn_null_buffer);
    RUN_TEST(test_mdns_get_fqdn_small_buffer);
    RUN_TEST(test_mdns_get_fqdn_exact_size);

    // Static hostname generation tests
    RUN_TEST(test_mdns_generate_hostname_static);
    RUN_TEST(test_mdns_generate_hostname_null_buffer);
    RUN_TEST(test_mdns_generate_hostname_small_buffer);

    // Stop/cleanup tests
    RUN_TEST(test_mdns_stop);
    RUN_TEST(test_mdns_stop_without_init);

    return UNITY_END();
}
