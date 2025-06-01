#!/usr/bin/env python3

import os
import subprocess
import sys
import signal
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
                
                if device._port:
                    device._port.close()
                
                return device.serial_port
                
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
    parser.add_argument("--timeout", type=float, default=1.0, 
                       help="Serial connection timeout in seconds (default: 1.0)")
    parser.add_argument("--attempts", type=int, default=2,
                       help="Connection attempts per port (default: 2)")
    args = parser.parse_args()

    chip = "esp32c3" if args.role == "receiver" else "esp32c6"

    print(f"Starting monitor process for {args.role} ({chip})...")
    print(f"Using timeout: {args.timeout}s, attempts per port: {args.attempts}")

    device_port = find_device(chip, timeout=args.timeout, attempts=args.attempts)
    monitor_device(device_port)

if __name__ == "__main__":
    main()
