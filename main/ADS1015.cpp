#include "ADS1015.h"

ADS1015::ADS1015(const Config &config) : 
    config_(config),
    current_gain_(Gain::GAIN_TWO),         // Default to GAIN_TWO (+/- 2.048V)
    current_rate_(DataRate::RATE_1600_SPS) // Default to 1600 SPS
{
    ESP_LOGI(TAG, "Creating ADS1015 instance with I2C address 0x%02x", config.i2c_address);
}

ADS1015::~ADS1015()
{
    // Deinitialize I2C device handle
    if (i2c_device_ != nullptr)
    {
        i2c_master_bus_rm_device(i2c_device_);
        i2c_device_ = nullptr;
    }

    // Delete I2C bus only if we created it and we're responsible for managing it
    if (i2c_bus_ != nullptr && config_.manage_bus)
    {
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
    }
}

esp_err_t ADS1015::init()
{
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    esp_err_t ret = ESP_OK;
    
    // Check if we're using a shared bus or creating our own
    if (config_.i2c_bus != nullptr) {
        // Use the provided I2C bus
        i2c_bus_ = config_.i2c_bus;
        ESP_LOGI(TAG, "Using shared I2C bus for ADS1015 at address 0x%02x", config_.i2c_address);
    } else {
        // Create our own I2C bus
        ESP_LOGI(TAG, "Initializing ADS1015 on SDA:%d, SCL:%d",
                config_.sda_pin, config_.scl_pin);
                
        // Configure I2C bus
        i2c_master_bus_config_t bus_config = {
            .i2c_port = -1, // Automatically select the first available I2C port
            .sda_io_num = static_cast<gpio_num_t>(config_.sda_pin),
            .scl_io_num = static_cast<gpio_num_t>(config_.scl_pin),
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
        };

        // Don't use internal pullups if external ones are present
        bus_config.flags.enable_internal_pullup = false;

        // Initialize I2C bus
        ret = i2c_new_master_bus(&bus_config, &i2c_bus_);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // Limit speed to configured value for maximum reliability
    uint32_t i2c_speed = config_.i2c_freq_hz;
    
    ESP_LOGI(TAG, "Configuring I2C device with address 0x%02x at %lu Hz", 
             config_.i2c_address, i2c_speed);

    // Configure I2C device
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config_.i2c_address,
        .scl_speed_hz = i2c_speed,
    };

    ret = i2c_master_bus_add_device(i2c_bus_, &device_config, &i2c_device_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        
        // Only delete the bus if we created it ourselves
        if (config_.manage_bus)
        {
            i2c_del_master_bus(i2c_bus_);
            i2c_bus_ = nullptr;
        }
        
        return ret;
    }

    // Verify device is responding by attempting to read from config register
    uint16_t config_value = 0;
    int retry_count = 0;
    const int max_retries = 5;
    bool device_found = false;

    while (retry_count < max_retries && !device_found)
    {
        ret = readRegister(REG_CONFIG, &config_value);
        if (ret == ESP_OK)
        {
            device_found = true;
            ESP_LOGI(TAG, "ADS1015 device found after %d attempts", retry_count + 1);
            ESP_LOGI(TAG, "Current config register: 0x%04x", config_value);
            break;
        }

        ESP_LOGW(TAG, "Attempt %d: Failed to read from ADS1015: %s",
                 retry_count + 1, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(100)); // Wait 100ms between retries
        retry_count++;
    }

    if (!device_found)
    {
        ESP_LOGE(TAG, "ADS1015 device not found after %d attempts", max_retries);
        i2c_master_bus_rm_device(i2c_device_);
        i2c_device_ = nullptr;
        
        // Only delete the bus if we created it ourselves
        if (config_.manage_bus)
        {
            i2c_del_master_bus(i2c_bus_);
            i2c_bus_ = nullptr;
        }
        
        return ESP_ERR_NOT_FOUND;
    }

    // Set default configuration
    uint16_t config = 0;
    config |= CONFIG_OS_MASK;                              // Start a conversion
    config |= getMultiplexerConfig(Channel::CHANNEL_0);    // Default to channel 0
    config |= getGainConfig(current_gain_);                // Set gain
    config |= CONFIG_MODE_MASK;                            // Single-shot mode
    config |= getDataRateConfig(current_rate_);            // Set data rate
    config |= 0x0003;                                      // Disable comparator
    
    ret = writeRegister(REG_CONFIG, config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set initial configuration: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ADS1015 initialized successfully");
    return ESP_OK;
}

esp_err_t ADS1015::readRaw(Channel channel, uint16_t* value)
{
    if (value == nullptr || i2c_device_ == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Set up the configuration for this reading
    uint16_t config = 0;
    config |= CONFIG_OS_MASK;                      // Start a conversion
    config |= getMultiplexerConfig(channel);       // Set channel
    config |= getGainConfig(current_gain_);        // Set gain
    config |= CONFIG_MODE_MASK;                    // Single-shot mode
    config |= getDataRateConfig(current_rate_);    // Set data rate
    config |= 0x0003;                              // Disable comparator

    esp_err_t ret = writeRegister(REG_CONFIG, config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set channel configuration: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for conversion to complete based on data rate
    uint16_t delay_ms;
    switch (current_rate_) {
        case DataRate::RATE_128_SPS:  delay_ms = 8; break;
        case DataRate::RATE_250_SPS:  delay_ms = 4; break;
        case DataRate::RATE_490_SPS:  delay_ms = 3; break;
        case DataRate::RATE_920_SPS:  delay_ms = 2; break;
        case DataRate::RATE_1600_SPS: delay_ms = 1; break;
        case DataRate::RATE_2400_SPS: delay_ms = 1; break;
        case DataRate::RATE_3300_SPS: delay_ms = 1; break;
        default: delay_ms = 8; break;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    // Read the conversion result
    uint16_t raw_value;
    ret = readRegister(REG_CONVERSION, &raw_value);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read conversion result: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // ADS1015 returns a 12-bit value left-justified in 16-bits
    // Shift right by 4 to get the 12-bit value
    *value = raw_value >> 4;
    
    ESP_LOGD(TAG, "Channel %d raw value: %u", static_cast<int>(channel), *value);
    return ESP_OK;
}

esp_err_t ADS1015::setGain(Gain gain)
{
    std::lock_guard<std::mutex> lock(mutex_);
    current_gain_ = gain;
    ESP_LOGI(TAG, "Gain set to %d", static_cast<int>(gain));
    return ESP_OK;
}

esp_err_t ADS1015::setDataRate(DataRate rate)
{
    std::lock_guard<std::mutex> lock(mutex_);
    current_rate_ = rate;
    ESP_LOGI(TAG, "Data rate set to %d", static_cast<int>(rate));
    return ESP_OK;
}

esp_err_t ADS1015::writeRegister(uint8_t reg, uint16_t value)
{
    if (i2c_device_ == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // Set timeout for the operation
    uint32_t timeout_ms = config_.timeout_ms > 0 ? config_.timeout_ms : 100; // Default to 100ms

    // ADS1015 uses big-endian (MSB first) for registers
    uint8_t write_buffer[3] = {
        reg,                    // Register address
        static_cast<uint8_t>(value >> 8), // MSB
        static_cast<uint8_t>(value & 0xFF) // LSB
    };

    // Write to the device
    esp_err_t ret = i2c_master_transmit(
        i2c_device_,          // Device handle
        write_buffer,         // Write buffer
        sizeof(write_buffer), // Write length
        timeout_ms            // Timeout in milliseconds
    );

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to write register 0x%02x: %s", reg, esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t ADS1015::readRegister(uint8_t reg, uint16_t* value)
{
    if (value == nullptr || i2c_device_ == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Set timeout for the operation - default 100ms if not specified
    uint32_t timeout_ms = config_.timeout_ms > 0 ? config_.timeout_ms : 100;

    // First write the register address we want to read from
    esp_err_t ret = i2c_master_transmit(
        i2c_device_,     // Device handle
        &reg,            // Write buffer (register address)
        1,               // Write length
        timeout_ms       // Timeout in milliseconds
    );

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to write register address 0x%02x: %s", 
                 reg, esp_err_to_name(ret));
        return ret;
    }

    // Now read the data from that register (2 bytes for ADS1015)
    uint8_t read_buffer[2];
    ret = i2c_master_receive(
        i2c_device_,     // Device handle
        read_buffer,     // Read buffer
        sizeof(read_buffer), // Read length
        timeout_ms       // Timeout in milliseconds
    );

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to read from register 0x%02x: %s", 
                 reg, esp_err_to_name(ret));
        return ret;
    }

    // ADS1015 uses big-endian (MSB first)
    *value = (static_cast<uint16_t>(read_buffer[0]) << 8) | static_cast<uint16_t>(read_buffer[1]);

    return ESP_OK;
}

uint16_t ADS1015::getMultiplexerConfig(Channel channel)
{
    switch (channel)
    {
    case Channel::CHANNEL_0:
        return MUX_SINGLE_0;
    case Channel::CHANNEL_1:
        return MUX_SINGLE_1;
    case Channel::CHANNEL_2:
        return MUX_SINGLE_2;
    case Channel::CHANNEL_3:
        return MUX_SINGLE_3;
    default:
        return MUX_SINGLE_0; // Default to channel 0
    }
}

uint16_t ADS1015::getGainConfig(Gain gain)
{
    return static_cast<uint16_t>(gain) << 9; // PGA bits are 9-11
}

uint16_t ADS1015::getDataRateConfig(DataRate rate)
{
    return static_cast<uint16_t>(rate) << 5; // DR bits are 5-7
}
