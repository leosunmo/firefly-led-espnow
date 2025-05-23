# Firefly LED ESP-NOW
This is the software for the ESP32 controllers on the firefly boards as well as a ESP32-C6 sender device to send commands to the firefly boards. The sender device uses ESP-NOW to communicate with the firefly boards, which are set up as receivers.

## Getting Started

### Prerequisites
- ESP32 development board(s)
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/linux-macos-setup.html) installed and set up
- Python 3.x

### Setup
1. Clone this repository:
   ```bash
   git clone git@github.com:leosunmo/firefly-led-espnow.git
   cd firefly-led-espnow
   ```
2. Export the ESP-IDF environment variables:
   ```bash
   . ${IDF_PATH}/export.sh
   ```
3. Run the Python script to build and flash the firmware:
   ```bash
   python3 auto_build_flash.py [sender|receiver]
   ```

### Monitoring
To monitor the output from the ESP32 boards, use the `monitor_device.py` script. This script automatically detects the correct port based on the role (sender or receiver).

#### Usage
Run the following command:
```bash
python3 monitor_device.py [sender|receiver]
```

For example, to monitor the sender device:
```bash
python3 monitor_device.py sender
```

To exit the monitoring session, press `Ctrl-]`.

## Project Structure
- `main/`: Contains the main application code.
- `build/`: Build artifacts.
- `esp-idf/`: ESP-IDF components.

## License
This project is licensed under the MIT License. See the LICENSE file for details.
