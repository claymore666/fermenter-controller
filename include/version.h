#pragma once

/**
 * Firmware Version
 *
 * Semantic Versioning: MAJOR.MINOR.PATCH
 * - MAJOR: Breaking changes or major new features
 * - MINOR: New features, enhancements
 * - PATCH: Bug fixes and patches
 *
 * Build number is automatically appended from __DATE__
 * Full version string: VERSION_STRING+YYMMDD (e.g., 0.1.0+251126)
 */

#define VERSION_MAJOR 0
#define VERSION_MINOR 1
#define VERSION_PATCH 0

#define VERSION_STRING "0.1.0"

// Helper macro to stringify
#define VERSION_STR_HELPER(x) #x
#define VERSION_STR(x) VERSION_STR_HELPER(x)

// Full version with all components
#define VERSION_FULL VERSION_STR(VERSION_MAJOR) "." VERSION_STR(VERSION_MINOR) "." VERSION_STR(VERSION_PATCH)
