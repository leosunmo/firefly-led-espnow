import os
import subprocess
import sys
from esptool import get_default_connected_device, get_port_list
import argparse

def find_device(chip):
    """Find the connected ESP device using esptool."""
    try:
        print(f"Attempting to find device with chip: {chip}")
        ports = [port for port in get_port_list() if not port.startswith("/dev/ttyS")]
        if not ports:
            print("No valid ports found after filtering.")
            sys.exit(1)

        device = get_default_connected_device(
            serial_list=ports,
            port=None,
            connect_attempts=3,
            initial_baud=115200,
            chip=chip
        )
        print(f"Found {chip} device on port: {device.serial_port}")

        if device._port:
            device._port.close()

        return device.serial_port
    except Exception as e:
        print(f"No {chip} device found: {e}")
        sys.exit(1)

def monitor_device(device_port):
    """Monitor the ESP device using idf.py."""
    try:
        print(f"Starting monitor on port {device_port}...")
        subprocess.run(["idf.py", "-p", device_port, "monitor"], check=True)
    except subprocess.CalledProcessError as e:
        print(f"Error during monitoring: {e}")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Monitor ESP devices.")
    parser.add_argument("role", choices=["sender", "receiver"], help="Specify the device role.")
    args = parser.parse_args()

    chip = "esp32c3" if args.role == "receiver" else "esp32c6"

    print(f"Starting monitor process for {args.role} ({chip})...")

    device_port = find_device(chip)
    monitor_device(device_port)

if __name__ == "__main__":
    main()
