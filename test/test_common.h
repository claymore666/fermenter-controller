#pragma once

/**
 * Common test constants and utilities
 * Shared across all test files for consistency
 */

namespace test {

// HTTP Status Codes
constexpr int HTTP_OK = 200;
constexpr int HTTP_BAD_REQUEST = 400;
constexpr int HTTP_UNAUTHORIZED = 401;
constexpr int HTTP_FORBIDDEN = 403;
constexpr int HTTP_NOT_FOUND = 404;
constexpr int HTTP_METHOD_NOT_ALLOWED = 405;
constexpr int HTTP_TOO_MANY_REQUESTS = 429;
constexpr int HTTP_INTERNAL_ERROR = 500;

// Test fixture values
constexpr uint32_t TEST_UNIX_TIME = 1700000000;  // Nov 14, 2023
constexpr float TEST_TEMP_CELSIUS = 18.5f;
constexpr float TEST_PRESSURE_BAR = 1.05f;

// Test credentials
constexpr const char* TEST_PASSWORD = "Admin123";
constexpr const char* TEST_WEAK_PASSWORD = "abc";

// Buffer sizes
constexpr size_t RESPONSE_BUFFER_SIZE = 4096;
constexpr size_t SMALL_BUFFER_SIZE = 128;

// Timing values (ms)
constexpr uint32_t SESSION_TIMEOUT_MS = 1800000;  // 30 minutes
constexpr uint32_t LOCKOUT_DURATION_MS = 300000;  // 5 minutes

} // namespace test
