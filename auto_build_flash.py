#!/usr/bin/env python3

import os
import subprocess
import sys
import signal
import serial.tools.list_ports
from esptool import get_default_connected_device, get_port_list
import argparse

class TimeoutError(Exception):
    """Exception raised when a connection attempt times out."""
    pass

def timeout_handler(signum, frame):
    """Signal handler for connection timeout."""
    raise TimeoutError("Connection attempt timed out")

def find_device(chip, timeout=1.0, attempts=2):
    """Find the connected ESP device using esptool.
    
    Args:
        chip: The ESP chip type to connect to
        timeout: Serial connection timeout in seconds
        attempts: Number of connection attempts per port
    """
    print(f"Attempting to find device with chip: {chip}")
    # Filter out /dev/ttyS ports
    ports = [port for port in get_port_list() if not port.startswith("/dev/ttyS")]
    if not ports:
        print("No valid ports found after filtering.")
        sys.exit(1)
    
    print(f"Found {len(ports)} potential ports: {', '.join(ports)}")
    
    # Try each port in turn
    for port in ports:
        print(f"Trying port {port} with {attempts} attempt(s) and {timeout}s timeout...")
        
        # Set up the timeout signal handler
        old_handler = signal.signal(signal.SIGALRM, timeout_handler)
        
        try:
            # Set alarm for timeout seconds
            signal.setitimer(signal.ITIMER_REAL, timeout)
            
            try:
                device = get_default_connected_device(
                    serial_list=[port],
                    port=None,
                    connect_attempts=attempts,
                    initial_baud=115200,
                    chip=chip
                )
                
                # Cancel the alarm if we successfully connected
                signal.setitimer(signal.ITIMER_REAL, 0)
                
                print(f"Successfully connected to {chip} device on port: {device.serial_port}")
                
                # Explicitly close the port to ensure it is released
                if device._port:
                    device._port.close()
                
                return device.serial_port  # Extract the port string
                
            except TimeoutError:
                print(f"Connection to port {port} timed out after {timeout}s")
                # Continue to the next port
                
            except Exception as e:
                print(f"Failed to connect on port {port}: {e}")
                # Continue to the next port
                
        finally:
            # Restore the old signal handler and cancel any pending alarm
            signal.setitimer(signal.ITIMER_REAL, 0)
            signal.signal(signal.SIGALRM, old_handler)
    
    # If we get here, we couldn't connect to any port
    print(f"Could not find any accessible {chip} device on any port.")
    sys.exit(1)

def build_and_flash(device_port, chip):
    """Build and flash the ESP project using idf.py."""
    try:
        # Check the current target from sdkconfig to avoid unnecessary recompilation
        sdkconfig_path = "sdkconfig"
        current_target = None
        if os.path.exists(sdkconfig_path):
            with open(sdkconfig_path, "r") as file:
                for line in file:
                    if line.startswith("CONFIG_IDF_TARGET="):
                        current_target = line.strip().split("=")[1].strip('"')
                        break

        if current_target != chip:
            print(f"Setting IDF target to {chip}...")
            subprocess.run(["idf.py", "set-target", chip], check=True)
        else:
            print(f"IDF target is already set to {chip}, skipping set-target.")

        # Run idf.py build and flash in one step
        print("Building and flashing...")
        subprocess.run(["idf.py", "-p", device_port, "build", "flash"], check=True)

        print("Build and flash completed successfully.")
    except subprocess.CalledProcessError as e:
        print(f"Error during build/flash: {e}")
        sys.exit(1)

def update_device_role(role):
    """Update the DEVICE_ROLE in config.h based on the role."""
    config_path = "main/config.h"
    try:
        with open(config_path, "r") as file:
            lines = file.readlines()

        with open(config_path, "w") as file:
            for line in lines:
                if "#define DEVICE_ROLE" in line:
                    file.write(f"#define DEVICE_ROLE DEVICE_ROLE_{role.upper()}\n")
                else:
                    file.write(line)

        print(f"Updated DEVICE_ROLE to DEVICE_ROLE_{role.upper()} in {config_path}")
    except Exception as e:
        print(f"Error updating DEVICE_ROLE in {config_path}: {e}")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Build and flash ESP devices.")
    parser.add_argument("role", choices=["sender", "receiver"], help="Specify the device role.")
    parser.add_argument("--timeout", type=float, default=1.0, 
                       help="Serial connection timeout in seconds (default: 1.0)")
    parser.add_argument("--attempts", type=int, default=2,
                       help="Connection attempts per port (default: 2)")
    args = parser.parse_args()

    # Update chip names to match expected values in esptool
    chip = "esp32c3" if args.role == "receiver" else "esp32c6"

    print(f"Starting auto build and flash process for {args.role} ({chip})...")
    print(f"Using timeout: {args.timeout}s, attempts per port: {args.attempts}")

    # Update DEVICE_ROLE in config.h
    update_device_role(args.role)

    device_port = find_device(chip, timeout=args.timeout, attempts=args.attempts)
    build_and_flash(device_port, chip)

if __name__ == "__main__":
    main()
