"""
Pre-build script to extract version from version.h and pass to ESP-IDF.

This ensures version.h is the single source of truth for firmware version.
The script parses VERSION_STRING and sets PROJECT_VER for ESP-IDF.
"""

Import("env")
import re
import os

def extract_version():
    """Extract VERSION_STRING from version.h"""
    version_file = os.path.join(env["PROJECT_DIR"], "include", "version.h")

    try:
        with open(version_file, "r") as f:
            content = f.read()

        # Match: #define VERSION_STRING "x.y.z"
        match = re.search(r'#define\s+VERSION_STRING\s+"([^"]+)"', content)
        if match:
            return match.group(1)
    except Exception as e:
        print(f"Warning: Could not read version.h: {e}")

    return "0.0.0"  # Fallback

# Extract version and add to build flags
version = extract_version()
print(f"Firmware version: {version}")

# Add PROJECT_VER for ESP-IDF's esp_app_get_description()
env.Append(CPPDEFINES=[
    ("PROJECT_VER", f'\\"{version}\\"')
])
