#!/usr/bin/env python3
"""
Device Provisioning Script

Sets up WiFi credentials and admin password on a fresh ESP32 fermenter controller.

Usage:
    python provision_device.py --port /dev/ttyACM0 --ssid "MyNetwork" --wifi-pass "wifi123" --admin-pass "Admin123"

Requirements:
    pip install pyserial requests
"""

import argparse
import serial
import time
import requests
import sys


def wait_for_prompt(ser, timeout=5):
    """Wait for the debug console prompt."""
    end_time = time.time() + timeout
    buffer = ""
    while time.time() < end_time:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            buffer += data
            print(data, end='', flush=True)
            if '>' in buffer or 'fermenter>' in buffer:
                return True
        time.sleep(0.1)
    return False


def send_command(ser, command, wait_time=2):
    """Send a command and wait for response."""
    ser.write(f"{command}\r\n".encode())
    time.sleep(wait_time)
    response = ""
    while ser.in_waiting:
        response += ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
    print(response, end='', flush=True)
    return response


def wait_for_wifi(device_ip, timeout=30):
    """Wait for device to become reachable via HTTP."""
    print(f"\nWaiting for device at {device_ip}...")
    end_time = time.time() + timeout
    while time.time() < end_time:
        try:
            response = requests.get(f"http://{device_ip}/api/health", timeout=2)
            if response.status_code == 200:
                data = response.json()
                print(f"Device reachable: {data}")
                return data
        except requests.exceptions.RequestException:
            pass
        print(".", end='', flush=True)
        time.sleep(1)
    print("\nTimeout waiting for device")
    return None


def set_admin_password(device_ip, password):
    """Set admin password via HTTP API."""
    try:
        response = requests.post(
            f"http://{device_ip}/api/setup",
            json={"password": password},
            timeout=5
        )
        data = response.json()
        if response.status_code == 200 and data.get("success"):
            print(f"Admin password set successfully")
            return True
        else:
            print(f"Failed to set password: {data}")
            return False
    except requests.exceptions.RequestException as e:
        print(f"HTTP error: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="Provision ESP32 fermenter controller")
    parser.add_argument("--port", "-p", default="/dev/ttyACM0", help="Serial port")
    parser.add_argument("--baud", "-b", type=int, default=115200, help="Baud rate")
    parser.add_argument("--ssid", "-s", required=True, help="WiFi SSID")
    parser.add_argument("--wifi-pass", "-w", required=True, help="WiFi password")
    parser.add_argument("--admin-pass", "-a", required=True, help="Admin password (8+ chars, 2 of: upper/lower/digit)")
    parser.add_argument("--ip", "-i", default="192.168.0.139", help="Expected device IP")
    args = parser.parse_args()

    # Validate admin password
    if len(args.admin_pass) < 8:
        print("Error: Admin password must be at least 8 characters")
        sys.exit(1)

    categories = 0
    if any(c.islower() for c in args.admin_pass):
        categories += 1
    if any(c.isupper() for c in args.admin_pass):
        categories += 1
    if any(c.isdigit() for c in args.admin_pass):
        categories += 1

    if categories < 2:
        print("Error: Admin password must contain at least 2 of: lowercase, uppercase, digit")
        sys.exit(1)

    print(f"=== ESP32 Fermenter Controller Provisioning ===")
    print(f"Serial port: {args.port}")
    print(f"WiFi SSID: {args.ssid}")
    print(f"Device IP: {args.ip}")
    print()

    # Step 1: Connect to serial
    print("Step 1: Connecting to serial port...")
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
        time.sleep(0.5)
        ser.reset_input_buffer()
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)

    # Step 2: Set WiFi credentials
    print("\nStep 2: Setting WiFi credentials...")
    send_command(ser, "")  # Wake up console
    wait_for_prompt(ser)

    response = send_command(ser, f'wifi set {args.ssid} {args.wifi_pass}', wait_time=3)

    if "OK" in response or "Setting WiFi" in response or "Connected" in response:
        print("WiFi credentials set")
    else:
        print("Warning: Unexpected response, continuing anyway...")

    # Give device time to connect
    time.sleep(2)

    # Check WiFi status
    print("\nChecking WiFi status...")
    send_command(ser, "wifi", wait_time=2)

    ser.close()

    # Step 3: Wait for device to be reachable
    print("\nStep 3: Waiting for device to connect to WiFi...")
    health = wait_for_wifi(args.ip, timeout=30)

    if not health:
        print("Error: Device not reachable. Check WiFi credentials and IP address.")
        sys.exit(1)

    # Step 4: Set admin password if not already provisioned
    print("\nStep 4: Setting admin password...")
    if health.get("provisioned"):
        print("Device is already provisioned. Skipping password setup.")
        print("Use factory reset or change password via web interface.")
    else:
        if set_admin_password(args.ip, args.admin_pass):
            print("\n=== Provisioning Complete ===")
            print(f"Device is ready at: http://{args.ip}/admin/")
            print(f"Login with password: {args.admin_pass}")
        else:
            print("Error: Failed to set admin password")
            sys.exit(1)


if __name__ == "__main__":
    main()
