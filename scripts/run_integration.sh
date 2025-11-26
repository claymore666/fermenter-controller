#!/bin/bash
# Integration test script - runs both Python and C++ simulators together

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MODBUS_SIM_DIR="$PROJECT_DIR/modbus_simulator"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Fermentation Controller Integration Test${NC}"
echo "=========================================="
echo

# Check prerequisites
if ! command -v socat &> /dev/null; then
    echo -e "${RED}Error: socat is not installed${NC}"
    echo "Install with: sudo apt-get install socat"
    exit 1
fi

# Create virtual serial port
echo -e "${YELLOW}Creating virtual serial port...${NC}"
cd "$PROJECT_DIR"

# Remove old symlinks if they exist
rm -f vport_simulator vport_client

# Start socat in background
socat -d -d pty,raw,echo=0,link=vport_simulator pty,raw,echo=0,link=vport_client &
SOCAT_PID=$!

# Wait for ports to be created
sleep 1

if [ ! -e "vport_simulator" ] || [ ! -e "vport_client" ]; then
    echo -e "${RED}Error: Failed to create virtual serial ports${NC}"
    kill $SOCAT_PID 2>/dev/null
    exit 1
fi

echo -e "${GREEN}Virtual serial ports created:${NC}"
echo "  Simulator port: $PROJECT_DIR/vport_simulator"
echo "  Client port:    $PROJECT_DIR/vport_client"
echo

# Start Python MODBUS simulator
echo -e "${YELLOW}Starting Python MODBUS simulator...${NC}"

cd "$MODBUS_SIM_DIR"

# Check if venv exists
if [ ! -d "venv" ]; then
    echo -e "${RED}Error: Python venv not found. Run:${NC}"
    echo "  cd $MODBUS_SIM_DIR && python -m venv venv && source venv/bin/activate && pip install -r requirements.txt"
    kill $SOCAT_PID 2>/dev/null
    exit 1
fi

# Create .env if it doesn't exist
if [ ! -f ".env" ]; then
    if [ -f ".env.example" ]; then
        cp .env.example .env
    fi
fi

# Update .env to use our virtual port
if [ -f ".env" ]; then
    sed -i "s|^MODBUS_PORT=.*|MODBUS_PORT=$PROJECT_DIR/vport_simulator|" .env
else
    echo "MODBUS_PORT=$PROJECT_DIR/vport_simulator" > .env
fi

# Start Python simulator in background
source venv/bin/activate
python main.py &
PYTHON_PID=$!

# Wait for Python simulator to start
sleep 2

echo -e "${GREEN}Python MODBUS simulator started (PID: $PYTHON_PID)${NC}"
echo "  Web interface: http://localhost:8080"
echo

# Build C++ simulator if needed
echo -e "${YELLOW}Building C++ simulator...${NC}"
cd "$PROJECT_DIR"

source "$PROJECT_DIR/venv/bin/activate"
pio run -e simulator

echo -e "${GREEN}C++ simulator built successfully${NC}"
echo

# Run C++ simulator
echo -e "${YELLOW}Starting C++ simulator...${NC}"
export MODBUS_PORT="$PROJECT_DIR/vport_client"
.pio/build/simulator/program config/default_config.json &
CPP_PID=$!

echo -e "${GREEN}C++ simulator started (PID: $CPP_PID)${NC}"
echo

# Cleanup function
cleanup() {
    echo
    echo -e "${YELLOW}Stopping simulators...${NC}"
    kill $CPP_PID 2>/dev/null || true
    kill $PYTHON_PID 2>/dev/null || true
    kill $SOCAT_PID 2>/dev/null || true
    rm -f "$PROJECT_DIR/vport_simulator" "$PROJECT_DIR/vport_client"
    echo -e "${GREEN}Cleanup complete${NC}"
}

trap cleanup EXIT

# Wait for user interrupt
echo -e "${GREEN}Integration test running. Press Ctrl+C to stop.${NC}"
echo "Open http://localhost:8080 to control sensors from the Python simulator."
echo

wait $CPP_PID
