#pragma once

/**
 * Firmware Version
 *
 * Semantic Versioning: MAJOR.MINOR.PATCH
 * - MAJOR: Breaking changes or major new features
 * - MINOR: New features, enhancements
 * - PATCH: Bug fixes and patches
 *
 * This version is also passed to ESP-IDF via PROJECT_VER in platformio.ini
 * so esp_app_get_description()->version returns this string.
 */

#define VERSION_MAJOR 0
#define VERSION_MINOR 3
#define VERSION_PATCH 0

// String version - MUST match the numbers above
// Also defined in platformio.ini as PROJECT_VER for ESP-IDF
#define VERSION_STRING "0.3.0"

// Helper macro to stringify
#define VERSION_STR_HELPER(x) #x
#define VERSION_STR(x) VERSION_STR_HELPER(x)

// Full version with all components (generated from numbers)
#define VERSION_FULL VERSION_STR(VERSION_MAJOR) "." VERSION_STR(VERSION_MINOR) "." VERSION_STR(VERSION_PATCH)

/**
 * Version as single integer for easy comparison
 * Format: MAJOR * 10000 + MINOR * 100 + PATCH
 * Example: 0.3.0 = 300, 1.2.3 = 10203
 */
#define VERSION_NUMBER ((VERSION_MAJOR * 10000) + (VERSION_MINOR * 100) + VERSION_PATCH)

/**
 * Parse version string into comparable integer
 * @param version_str Version string like "0.3.0" or "1.2.3"
 * @return Version number (MAJOR*10000 + MINOR*100 + PATCH), or -1 on parse error
 */
inline int parse_version(const char* version_str) {
    if (!version_str) return -1;

    int major = 0, minor = 0, patch = 0;
    int parsed = 0;

    // Parse MAJOR
    while (*version_str >= '0' && *version_str <= '9') {
        major = major * 10 + (*version_str - '0');
        version_str++;
        parsed++;
    }
    if (parsed == 0 || *version_str != '.') return -1;
    version_str++; // skip '.'
    parsed = 0;

    // Parse MINOR
    while (*version_str >= '0' && *version_str <= '9') {
        minor = minor * 10 + (*version_str - '0');
        version_str++;
        parsed++;
    }
    if (parsed == 0 || *version_str != '.') return -1;
    version_str++; // skip '.'
    parsed = 0;

    // Parse PATCH
    while (*version_str >= '0' && *version_str <= '9') {
        patch = patch * 10 + (*version_str - '0');
        version_str++;
        parsed++;
    }
    if (parsed == 0) return -1;

    // Sanity check ranges
    if (major > 99 || minor > 99 || patch > 99) return -1;

    return (major * 10000) + (minor * 100) + patch;
}

/**
 * Compare two version strings
 * @param v1 First version string
 * @param v2 Second version string
 * @return -1 if v1 < v2, 0 if equal, 1 if v1 > v2, -2 on parse error
 */
inline int compare_versions(const char* v1, const char* v2) {
    int n1 = parse_version(v1);
    int n2 = parse_version(v2);

    if (n1 < 0 || n2 < 0) return -2; // parse error

    if (n1 < n2) return -1;
    if (n1 > n2) return 1;
    return 0;
}

/**
 * Check if remote version is newer than current
 * @param remote_version Remote version string from update server
 * @return true if remote is newer, false otherwise (same, older, or parse error)
 */
inline bool is_newer_version(const char* remote_version) {
    return compare_versions(remote_version, VERSION_STRING) > 0;
}
