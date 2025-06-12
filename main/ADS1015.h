#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include "esp_log.h"

/**
 * @brief ADS1015 I2C ADC driver
 * 
 * This class provides an interface to the ADS1015 12-bit ADC over I2C.
 * It supports reading from multiple channels and configuring gain settings.
 */
class ADS1015 {
public:
    /**
     * @brief Channel selection for ADS1015
     */
    enum class Channel {
        CHANNEL_0 = 0,   // A0 input
        CHANNEL_1,       // A1 input
        CHANNEL_2,       // A2 input
        CHANNEL_3        // A3 input
    };
    
    /**
     * @brief Gain settings for ADS1015 programmable gain amplifier
     */
    enum class Gain {
        GAIN_TWOTHIRDS = 0,  // +/- 6.144V
        GAIN_ONE,            // +/- 4.096V
        GAIN_TWO,            // +/- 2.048V
        GAIN_FOUR,           // +/- 1.024V
        GAIN_EIGHT,          // +/- 0.512V
        GAIN_SIXTEEN         // +/- 0.256V
    };
    
    /**
     * @brief Data rate options for ADS1015
     */
    enum class DataRate {
        RATE_128_SPS = 0,    // 128 samples per second
        RATE_250_SPS,        // 250 samples per second
        RATE_490_SPS,        // 490 samples per second
        RATE_920_SPS,        // 920 samples per second
        RATE_1600_SPS,       // 1600 samples per second (default)
        RATE_2400_SPS,       // 2400 samples per second
        RATE_3300_SPS        // 3300 samples per second
    };
    
    /**
     * @brief Configuration for ADS1015
     */
    struct Config {
        uint8_t i2c_address;     // I2C address (default 0x48)
        int sda_pin;             // I2C SDA pin
        int scl_pin;             // I2C SCL pin
        uint32_t i2c_freq_hz;    // I2C frequency in Hz
        uint32_t timeout_ms;     // I2C timeout in milliseconds
        i2c_master_bus_handle_t i2c_bus = nullptr; // Optional: Existing I2C bus handle to share
        bool manage_bus = true;  // Whether to create/delete the I2C bus (false if sharing)
    };
    
    /**
     * @brief Constructor
     * @param config Configuration for ADS1015
     */
    explicit ADS1015(const Config& config);
    
    /**
     * @brief Destructor
     */
    ~ADS1015();
    
    /**
     * @brief Initialize the ADS1015
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t init();
    
    /**
     * @brief Read the raw ADC value from a channel
     * @param channel Channel to read from
     * @param value Pointer to store the result (0-4095)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t readRaw(Channel channel, uint16_t* value);
    
    /**
     * @brief Set the gain for future readings
     * @param gain Gain setting
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t setGain(Gain gain);
    
    /**
     * @brief Set the data rate for future readings
     * @param rate Data rate setting
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t setDataRate(DataRate rate);
    
    /**
     * @brief Get the maximum value the ADC can report
     * @return The maximum value (4095 for ADS1015)
     */
    static constexpr uint16_t getMaxValue() { return 4095; }
    
private:
    // ADS1015 registers
    static constexpr uint8_t REG_CONVERSION = 0x00;
    static constexpr uint8_t REG_CONFIG     = 0x01;
    static constexpr uint8_t REG_LO_THRESH  = 0x02;
    static constexpr uint8_t REG_HI_THRESH  = 0x03;
    
    // Config register bits
    static constexpr uint16_t CONFIG_OS_MASK      = 0x8000;
    static constexpr uint16_t CONFIG_MUX_MASK     = 0x7000;
    static constexpr uint16_t CONFIG_PGA_MASK     = 0x0E00;
    static constexpr uint16_t CONFIG_MODE_MASK    = 0x0100;
    static constexpr uint16_t CONFIG_DR_MASK      = 0x00E0;
    static constexpr uint16_t CONFIG_COMP_MODE    = 0x0010;
    static constexpr uint16_t CONFIG_COMP_POL     = 0x0008;
    static constexpr uint16_t CONFIG_COMP_LAT     = 0x0004;
    static constexpr uint16_t CONFIG_COMP_QUE     = 0x0003;
    
    // MUX values for single-ended readings
    static constexpr uint16_t MUX_SINGLE_0 = 0x4000; // AIN0 to GND (100 << 12)
    static constexpr uint16_t MUX_SINGLE_1 = 0x5000; // AIN1 to GND (101 << 12)
    static constexpr uint16_t MUX_SINGLE_2 = 0x6000; // AIN2 to GND (110 << 12)
    static constexpr uint16_t MUX_SINGLE_3 = 0x7000; // AIN3 to GND (111 << 12)
    
    // MUX values for differential readings
    static constexpr uint16_t MUX_DIFF_0_1 = 0x0000; // AIN0 - AIN1 (000 << 12)
    static constexpr uint16_t MUX_DIFF_0_3 = 0x1000; // AIN0 - AIN3 (001 << 12)
    static constexpr uint16_t MUX_DIFF_1_3 = 0x2000; // AIN1 - AIN3 (010 << 12)
    static constexpr uint16_t MUX_DIFF_2_3 = 0x3000; // AIN2 - AIN3 (011 << 12)
    
    // Configuration
    Config config_;
    
    // Current settings
    Gain current_gain_;
    DataRate current_rate_;
    
    // Mutex for thread safety
    std::mutex mutex_;
    
    // I2C handles
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    i2c_master_dev_handle_t i2c_device_ = nullptr;
    
    // Helper methods
    esp_err_t writeRegister(uint8_t reg, uint16_t value);
    esp_err_t readRegister(uint8_t reg, uint16_t* value);
    uint16_t getMultiplexerConfig(Channel channel);
    uint16_t getGainConfig(Gain gain);
    uint16_t getDataRateConfig(DataRate rate);
    
    // Logger tag
    static constexpr const char* TAG = "ADS1015";
};
