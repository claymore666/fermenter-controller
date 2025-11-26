#!/bin/bash
#
# Virtual Serial Port Setup Script
# Creates or revokes a virtual PTY pair for MODBUS RTU development
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="$SCRIPT_DIR/.vport.pid"
LINK_SIMULATOR="$SCRIPT_DIR/vport_simulator"
LINK_CLIENT="$SCRIPT_DIR/vport_client"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

usage() {
    echo "Usage: $0 {start|stop|status|restart}"
    echo ""
    echo "Commands:"
    echo "  start   - Create virtual serial port pair"
    echo "  stop    - Remove virtual serial port pair"
    echo "  status  - Check if virtual port is running"
    echo "  restart - Stop and start virtual port"
    echo ""
    echo "After starting, use these ports:"
    echo "  Simulator: $LINK_SIMULATOR (set in .env as MODBUS_PORT)"
    echo "  Client:    $LINK_CLIENT"
    exit 1
}

check_socat() {
    if ! command -v socat &> /dev/null; then
        echo -e "${RED}Error: socat is not installed${NC}"
        echo "Install with:"
        echo "  Ubuntu/Debian: sudo apt install socat"
        echo "  Fedora/RHEL:   sudo dnf install socat"
        echo "  macOS:         brew install socat"
        exit 1
    fi
}

is_running() {
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE")
        if ps -p "$PID" > /dev/null 2>&1; then
            return 0
        fi
    fi
    return 1
}

start_vport() {
    check_socat

    if is_running; then
        echo -e "${YELLOW}Virtual port already running (PID: $(cat $PID_FILE))${NC}"
        show_status
        return 0
    fi

    echo "Starting virtual serial port..."

    # Create socat PTY pair
    socat -d -d pty,raw,echo=0,link="$LINK_SIMULATOR" pty,raw,echo=0,link="$LINK_CLIENT" &
    SOCAT_PID=$!

    # Wait a moment for socat to create the PTYs
    sleep 1

    # Check if socat started successfully
    if ps -p $SOCAT_PID > /dev/null 2>&1; then
        echo $SOCAT_PID > "$PID_FILE"
        echo -e "${GREEN}Virtual serial port started successfully${NC}"
        echo ""
        show_status

        # Update .env hint
        echo ""
        echo -e "${YELLOW}Update your .env file:${NC}"
        echo "MODBUS_PORT=$LINK_SIMULATOR"
    else
        echo -e "${RED}Failed to start virtual serial port${NC}"
        exit 1
    fi
}

stop_vport() {
    if ! is_running; then
        echo -e "${YELLOW}Virtual port is not running${NC}"
        # Clean up stale files
        rm -f "$PID_FILE" "$LINK_SIMULATOR" "$LINK_CLIENT"
        return 0
    fi

    PID=$(cat "$PID_FILE")
    echo "Stopping virtual serial port (PID: $PID)..."

    kill "$PID" 2>/dev/null

    # Wait for process to terminate
    for i in {1..10}; do
        if ! ps -p "$PID" > /dev/null 2>&1; then
            break
        fi
        sleep 0.1
    done

    # Force kill if still running
    if ps -p "$PID" > /dev/null 2>&1; then
        kill -9 "$PID" 2>/dev/null
    fi

    # Clean up files
    rm -f "$PID_FILE" "$LINK_SIMULATOR" "$LINK_CLIENT"

    echo -e "${GREEN}Virtual serial port stopped${NC}"
}

show_status() {
    if is_running; then
        PID=$(cat "$PID_FILE")
        echo -e "${GREEN}Virtual port is running (PID: $PID)${NC}"
        echo ""
        echo "Port paths:"

        if [ -L "$LINK_SIMULATOR" ]; then
            REAL_SIM=$(readlink -f "$LINK_SIMULATOR")
            echo "  Simulator: $LINK_SIMULATOR -> $REAL_SIM"
        fi

        if [ -L "$LINK_CLIENT" ]; then
            REAL_CLI=$(readlink -f "$LINK_CLIENT")
            echo "  Client:    $LINK_CLIENT -> $REAL_CLI"
        fi
    else
        echo -e "${RED}Virtual port is not running${NC}"
    fi
}

# Main
case "$1" in
    start)
        start_vport
        ;;
    stop)
        stop_vport
        ;;
    status)
        show_status
        ;;
    restart)
        stop_vport
        sleep 0.5
        start_vport
        ;;
    *)
        usage
        ;;
esac
