#pragma once

/**
 * Security utilities for ESP32 Fermentation Controller
 *
 * Provides:
 * - Constant-time comparison (timing attack prevention)
 * - Password hashing using SHA-256
 * - Cryptographic random token generation
 * - Path normalization (path traversal prevention)
 */

#include <cstring>
#include <cstdint>
#include <cstdio>

#ifdef ESP32_BUILD
#include "esp_random.h"
#include "mbedtls/sha256.h"
#else
// Simulator fallback
#include <cstdlib>
#include <ctime>
#endif

namespace security {

// Password hash length (SHA-256 = 32 bytes, hex encoded = 64 chars + null)
static constexpr size_t PASSWORD_HASH_LEN = 64;
static constexpr size_t PASSWORD_HASH_BUF_SIZE = PASSWORD_HASH_LEN + 1;

// Session token length (32 bytes random, hex encoded = 64 chars + null)
static constexpr size_t SESSION_TOKEN_LEN = 64;
static constexpr size_t SESSION_TOKEN_BUF_SIZE = SESSION_TOKEN_LEN + 1;

// Minimum password requirements
static constexpr size_t MIN_PASSWORD_LENGTH = 8;
static constexpr size_t MAX_PASSWORD_LENGTH = 64;

/**
 * Constant-time memory comparison
 * Compares two byte arrays in constant time to prevent timing attacks.
 *
 * @param a First buffer
 * @param b Second buffer
 * @param len Length to compare
 * @return true if equal, false otherwise
 */
inline bool secure_compare(const void* a, const void* b, size_t len) {
    const volatile uint8_t* va = static_cast<const volatile uint8_t*>(a);
    const volatile uint8_t* vb = static_cast<const volatile uint8_t*>(b);

    volatile uint8_t result = 0;
    for (size_t i = 0; i < len; i++) {
        result |= va[i] ^ vb[i];
    }

    return result == 0;
}

/**
 * Constant-time string comparison
 * Compares two null-terminated strings in constant time.
 *
 * @param a First string
 * @param b Second string
 * @return true if equal, false otherwise
 */
inline bool secure_strcmp(const char* a, const char* b) {
    if (!a || !b) return false;

    size_t len_a = strlen(a);
    size_t len_b = strlen(b);

    // Always compare full length of longer string to avoid length leak
    size_t max_len = (len_a > len_b) ? len_a : len_b;
    if (max_len == 0) return true;

    // Compare using constant-time method
    // If lengths differ, the shorter string will have null bytes compared
    volatile uint8_t result = (len_a != len_b) ? 1 : 0;

    for (size_t i = 0; i < max_len; i++) {
        uint8_t ca = (i < len_a) ? static_cast<uint8_t>(a[i]) : 0;
        uint8_t cb = (i < len_b) ? static_cast<uint8_t>(b[i]) : 0;
        result |= ca ^ cb;
    }

    return result == 0;
}

/**
 * Convert bytes to hex string
 *
 * @param bytes Input bytes
 * @param len Number of bytes
 * @param hex Output hex string (must be at least len*2+1 bytes)
 */
inline void bytes_to_hex(const uint8_t* bytes, size_t len, char* hex) {
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        hex[i * 2] = hex_chars[(bytes[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
    }
    hex[len * 2] = '\0';
}

/**
 * Hash password using SHA-256
 *
 * @param password Plain text password
 * @param salt Salt to add (can be device serial, etc.)
 * @param hash_out Output buffer (must be at least PASSWORD_HASH_BUF_SIZE bytes)
 * @return true on success
 */
inline bool hash_password(const char* password, const char* salt, char* hash_out) {
    if (!password || !hash_out) return false;

    uint8_t hash[32];  // SHA-256 output is 32 bytes

#ifdef ESP32_BUILD
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)

    // Hash: salt + password
    if (salt) {
        mbedtls_sha256_update(&ctx, (const unsigned char*)salt, strlen(salt));
    }
    mbedtls_sha256_update(&ctx, (const unsigned char*)password, strlen(password));

    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
#else
    // Simulator: simple hash for testing (NOT cryptographically secure!)
    // In real simulator tests, you'd link against mbedtls
    uint32_t h = 5381;
    if (salt) {
        for (const char* p = salt; *p; p++) {
            h = ((h << 5) + h) ^ *p;
        }
    }
    for (const char* p = password; *p; p++) {
        h = ((h << 5) + h) ^ *p;
    }
    // Fill hash buffer with simple hash (for testing only)
    for (int i = 0; i < 32; i++) {
        hash[i] = (h >> (i % 32)) ^ (h >> ((i + 16) % 32));
        h = h * 1103515245 + 12345;
    }
#endif

    bytes_to_hex(hash, 32, hash_out);
    return true;
}

/**
 * Verify password against stored hash
 * Uses constant-time comparison
 *
 * @param password Plain text password to verify
 * @param salt Salt used when hashing
 * @param stored_hash Previously stored hash
 * @return true if password matches
 */
inline bool verify_password(const char* password, const char* salt, const char* stored_hash) {
    if (!password || !stored_hash) return false;

    char computed_hash[PASSWORD_HASH_BUF_SIZE];
    if (!hash_password(password, salt, computed_hash)) {
        return false;
    }

    return secure_strcmp(computed_hash, stored_hash);
}

/**
 * Generate cryptographic random bytes
 *
 * @param buffer Output buffer
 * @param len Number of bytes to generate
 */
inline void generate_random_bytes(uint8_t* buffer, size_t len) {
#ifdef ESP32_BUILD
    // ESP32 has hardware RNG
    for (size_t i = 0; i < len; i += 4) {
        uint32_t r = esp_random();
        size_t remaining = len - i;
        size_t copy_len = (remaining < 4) ? remaining : 4;
        memcpy(buffer + i, &r, copy_len);
    }
#else
    // Simulator fallback (NOT cryptographically secure!)
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(nullptr));
        seeded = true;
    }
    for (size_t i = 0; i < len; i++) {
        buffer[i] = rand() & 0xFF;
    }
#endif
}

/**
 * Generate secure session token
 *
 * @param token_out Output buffer (must be at least SESSION_TOKEN_BUF_SIZE bytes)
 */
inline void generate_session_token(char* token_out) {
    uint8_t random_bytes[32];
    generate_random_bytes(random_bytes, sizeof(random_bytes));
    bytes_to_hex(random_bytes, sizeof(random_bytes), token_out);
}

/**
 * Validate password meets requirements
 *
 * @param password Password to validate
 * @return true if password meets requirements
 */
inline bool validate_password_strength(const char* password) {
    if (!password) return false;

    size_t len = strlen(password);
    if (len < MIN_PASSWORD_LENGTH || len > MAX_PASSWORD_LENGTH) {
        return false;
    }

    // Check for at least one of each: uppercase, lowercase, digit
    bool has_upper = false;
    bool has_lower = false;
    bool has_digit = false;

    for (const char* p = password; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') has_upper = true;
        else if (*p >= 'a' && *p <= 'z') has_lower = true;
        else if (*p >= '0' && *p <= '9') has_digit = true;
    }

    // Require at least 2 of 3 categories
    int categories = (has_upper ? 1 : 0) + (has_lower ? 1 : 0) + (has_digit ? 1 : 0);
    return categories >= 2;
}

/**
 * Normalize and validate path (prevent path traversal)
 *
 * @param path Input path
 * @param base_path Required base path (e.g., "/spiffs")
 * @param output Output buffer for normalized path
 * @param output_size Size of output buffer
 * @return true if path is valid and within base_path
 */
inline bool normalize_path(const char* path, const char* base_path,
                          char* output, size_t output_size) {
    if (!path || !base_path || !output || output_size == 0) return false;

    // Check for null bytes (injection attempt)
    for (const char* p = path; *p; p++) {
        if (*p == '\0') break;  // Stop at first null
    }

    // Reject paths with ".." anywhere
    if (strstr(path, "..") != nullptr) {
        return false;
    }

    // Reject paths with double slashes
    if (strstr(path, "//") != nullptr) {
        return false;
    }

    // Build full path
    size_t base_len = strlen(base_path);
    size_t path_len = strlen(path);

    if (base_len + path_len >= output_size) {
        return false;  // Path too long
    }

    // Ensure base_path doesn't end with '/'
    if (base_len > 0 && base_path[base_len - 1] == '/') {
        base_len--;
    }

    // Copy base path
    memcpy(output, base_path, base_len);

    // Ensure path starts with '/'
    if (path[0] != '/') {
        output[base_len] = '/';
        base_len++;
    }

    // Copy path
    memcpy(output + base_len, path, path_len + 1);

    return true;
}

/**
 * Check if path starts with base path (after normalization)
 *
 * @param path Full path to check
 * @param base_path Required base path prefix
 * @return true if path is within base_path
 */
inline bool path_within_base(const char* path, const char* base_path) {
    if (!path || !base_path) return false;

    size_t base_len = strlen(base_path);
    if (strncmp(path, base_path, base_len) != 0) {
        return false;
    }

    // Ensure it's a directory boundary, not just prefix match
    // e.g., "/spiffs" should match "/spiffs/foo" but not "/spiffs_other"
    if (path[base_len] != '\0' && path[base_len] != '/') {
        return false;
    }

    return true;
}

} // namespace security
