#include <unity.h>
#include "security/cert_generator.h"
#include <cstring>

// =============================================================================
// CertGeneratorResult Tests
// =============================================================================

void test_cert_result_initial_state() {
    security::CertGeneratorResult result;

    TEST_ASSERT_FALSE(result.success);
    TEST_ASSERT_EQUAL_STRING("", result.error_msg);
}

void test_cert_result_set_error() {
    security::CertGeneratorResult result;

    result.set_error("Test error message");

    TEST_ASSERT_FALSE(result.success);
    TEST_ASSERT_EQUAL_STRING("Test error message", result.error_msg);
}

void test_cert_result_error_truncation() {
    security::CertGeneratorResult result;

    // Error message buffer is 64 bytes, test truncation
    const char* long_error = "This is a very long error message that exceeds the buffer size limit of sixty-four characters";
    result.set_error(long_error);

    TEST_ASSERT_FALSE(result.success);
    TEST_ASSERT_EQUAL(63, strlen(result.error_msg));  // Truncated to 63 + null
}

// =============================================================================
// Certificate Generation Tests (Simulator)
// =============================================================================

void test_generate_cert_success() {
    char cert_pem[2048];
    char key_pem[2048];
    size_t cert_len = 0;
    size_t key_len = 0;

    auto result = security::generate_self_signed_cert(
        cert_pem, sizeof(cert_pem), &cert_len,
        key_pem, sizeof(key_pem), &key_len
    );

    TEST_ASSERT_TRUE(result.success);
    TEST_ASSERT_GREATER_THAN(0, cert_len);
    TEST_ASSERT_GREATER_THAN(0, key_len);
}

void test_generate_cert_contains_markers() {
    char cert_pem[2048];
    char key_pem[2048];
    size_t cert_len = 0;
    size_t key_len = 0;

    security::generate_self_signed_cert(
        cert_pem, sizeof(cert_pem), &cert_len,
        key_pem, sizeof(key_pem), &key_len
    );

    // Check PEM markers
    TEST_ASSERT_NOT_NULL(strstr(cert_pem, "-----BEGIN CERTIFICATE-----"));
    TEST_ASSERT_NOT_NULL(strstr(cert_pem, "-----END CERTIFICATE-----"));
    TEST_ASSERT_NOT_NULL(strstr(key_pem, "-----BEGIN PRIVATE KEY-----"));
    TEST_ASSERT_NOT_NULL(strstr(key_pem, "-----END PRIVATE KEY-----"));
}

void test_generate_cert_null_params() {
    char key_pem[2048];
    size_t cert_len = 0;
    size_t key_len = 0;

    // Test with null cert buffer - should still work for key
    auto result = security::generate_self_signed_cert(
        nullptr, 0, &cert_len,
        key_pem, sizeof(key_pem), &key_len
    );

    // Simulator stub returns success even with partial null params
    TEST_ASSERT_TRUE(result.success);
    TEST_ASSERT_GREATER_THAN(0, key_len);  // Key should still be generated
}

// =============================================================================
// Device Serial Tests
// =============================================================================

void test_get_device_serial() {
    char buffer[32];
    size_t len = security::get_device_serial(buffer, sizeof(buffer));

    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("SIMULATOR001", buffer);
}

void test_get_device_serial_null_buffer() {
    size_t len = security::get_device_serial(nullptr, 32);
    TEST_ASSERT_EQUAL(0, len);
}

void test_get_device_serial_small_buffer() {
    char buffer[5];  // Too small
    size_t len = security::get_device_serial(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(0, len);
}

void test_get_device_serial_exact_size() {
    char buffer[13];  // Minimum required size
    size_t len = security::get_device_serial(buffer, sizeof(buffer));
    TEST_ASSERT_GREATER_THAN(0, len);
}

// =============================================================================
// Constants Tests
// =============================================================================

void test_cert_constants() {
    TEST_ASSERT_EQUAL(2048, security::CERT_PEM_MAX_SIZE);
    TEST_ASSERT_EQUAL(2048, security::KEY_PEM_MAX_SIZE);
    TEST_ASSERT_EQUAL(2048, security::RSA_KEY_BITS);
    TEST_ASSERT_EQUAL(10, security::CERT_VALIDITY_YEARS);
}

// =============================================================================
// Test Runner
// =============================================================================

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    // CertGeneratorResult tests
    RUN_TEST(test_cert_result_initial_state);
    RUN_TEST(test_cert_result_set_error);
    RUN_TEST(test_cert_result_error_truncation);

    // Certificate generation tests
    RUN_TEST(test_generate_cert_success);
    RUN_TEST(test_generate_cert_contains_markers);
    RUN_TEST(test_generate_cert_null_params);

    // Device serial tests
    RUN_TEST(test_get_device_serial);
    RUN_TEST(test_get_device_serial_null_buffer);
    RUN_TEST(test_get_device_serial_small_buffer);
    RUN_TEST(test_get_device_serial_exact_size);

    // Constants tests
    RUN_TEST(test_cert_constants);

    return UNITY_END();
}
