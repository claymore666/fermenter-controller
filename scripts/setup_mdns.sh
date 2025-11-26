#!/bin/bash
#
# Setup mDNS component for ESP-IDF
#
# This script clones the mDNS component from esp-protocols because PlatformIO's
# ESP-IDF component manager doesn't reliably fetch managed components.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
COMPONENTS_DIR="$PROJECT_DIR/components"
MDNS_DIR="$COMPONENTS_DIR/mdns"

echo "Setting up mDNS component..."

# Check if already exists
if [ -d "$MDNS_DIR" ]; then
    echo "mDNS component already exists at $MDNS_DIR"
    echo "To update, remove the directory and run this script again:"
    echo "  rm -rf $MDNS_DIR"
    exit 0
fi

# Create components directory if needed
mkdir -p "$COMPONENTS_DIR"

# Clone esp-protocols repo (shallow clone for speed)
echo "Cloning esp-protocols repository..."
cd "$COMPONENTS_DIR"
git clone --depth 1 https://github.com/espressif/esp-protocols.git esp-protocols-temp

# Extract only the mDNS component
echo "Extracting mDNS component..."
mv esp-protocols-temp/components/mdns "$MDNS_DIR"

# Clean up
echo "Cleaning up..."
rm -rf esp-protocols-temp

echo "mDNS component installed successfully at $MDNS_DIR"
echo ""
echo "Note: This component is not tracked in git. If you clone the repository,"
echo "run this script again to set up the component."
