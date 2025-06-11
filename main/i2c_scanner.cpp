#include "i2c_scanner.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char* TAG = "I2C_SCANNER";

void i2c_scan_bus(i2c_master_bus_handle_t i2c_bus) {
    ESP_LOGI(TAG, "Scanning I2C bus for devices...");
    
    int devices_found = 0;
    uint8_t data;
    
    for (uint8_t address = 1; address < 128; address++) {
        i2c_master_dev_handle_t temp_device = nullptr;
        
        // Configure a temporary I2C device at this address
        i2c_device_config_t device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = address,
            .scl_speed_hz = 100000,  // Use 100kHz for best compatibility
        };
        
        // Try to add the device (don't check error - just testing if it exists)
        esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &device_config, &temp_device);
        
        if (ret == ESP_OK && temp_device != nullptr) {
            // Try to read a byte to see if device responds
            ret = i2c_master_receive(temp_device, &data, 1, -1);
            
            if (ret == ESP_OK || ret == ESP_ERR_NOT_FINISHED) {
                // Successful acknowledgment (even with read error) means device exists
                ESP_LOGI(TAG, "Device found at address 0x%02X", address);
                devices_found++;
            }
            
            // Clean up the temporary device
            i2c_master_bus_rm_device(temp_device);
        }
    }
    
    if (devices_found == 0) {
        ESP_LOGW(TAG, "No I2C devices found!");
    } else {
        ESP_LOGI(TAG, "Scan complete, found %d device(s)", devices_found);
    }
}
