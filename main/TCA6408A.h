#pragma once

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <functional>
#include <map>
#include <mutex>

/**
 * @brief TCA6408A I2C GPIO Expander driver
 */
class TCA6408A {
public:
    /**
     * @brief Pin change callback type
     * @param pin Pin number (0-7)
     * @param level Pin level (0 or 1)
     */
    using PinChangeCallback = std::function<void(uint8_t pin, uint8_t level)>;

    /**
     * @brief Configuration for TCA6408A
     */
    struct Config {
        uint8_t i2c_address;       ///< I2C device address (default 0x20)
        int sda_pin;               ///< I2C SDA pin
        int scl_pin;               ///< I2C SCL pin
        uint32_t i2c_freq_hz;      ///< I2C frequency in Hz
        uint32_t timeout_ms;       ///< I2C timeout in milliseconds
        uint32_t poll_period_ms;   ///< Poll period for pin change detection (only used if int_pin is -1)
        int int_pin;               ///< GPIO pin connected to TCA6408A INT pin (set to -1 to use polling instead)
    };

    /**
     * @brief Constructor
     * @param config Configuration for TCA6408A
     */
    explicit TCA6408A(const Config& config);

    /**
     * @brief Destructor
     */
    ~TCA6408A();

    /**
     * @brief Initialize the TCA6408A
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t init();

    /**
     * @brief Configure a pin as input or output
     * @param pin Pin number (0-7)
     * @param isOutput true for output, false for input
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t configurePin(uint8_t pin, bool isOutput);

    /**
     * @brief Read input pin level
     * @param pin Pin number (0-7)
     * @param level Pointer to store pin level (0 or 1)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t readPin(uint8_t pin, uint8_t* level);

    /**
     * @brief Write output pin level
     * @param pin Pin number (0-7)
     * @param level Pin level (0 or 1)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t writePin(uint8_t pin, uint8_t level);

    /**
     * @brief Register a callback for pin change events
     * @param pin Pin number (0-7)
     * @param callback Function to call when pin changes
     * @param activeLow true if pin is active low, false if active high
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t registerCallback(uint8_t pin, PinChangeCallback callback, bool activeLow);

    /**
     * @brief Start monitoring pin changes
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t startMonitoring();

    /**
     * @brief Stop monitoring pin changes
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t stopMonitoring();
    
    /**
     * @brief Process pin changes (called automatically by interrupt handler or polling)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t processPinChanges();

    /**
     * @brief Read all input pins at once
     * @param portValue Pointer to store 8-bit port value
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t readPort(uint8_t* portValue);

    /**
     * @brief Write to all output pins at once
     * @param portValue 8-bit port value to write
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t writePort(uint8_t portValue);

private:
    // TCA6408A registers
    static constexpr uint8_t REG_INPUT      = 0x00;
    static constexpr uint8_t REG_OUTPUT     = 0x01;
    static constexpr uint8_t REG_POLARITY   = 0x02;
    static constexpr uint8_t REG_CONFIG     = 0x03;

    // Configuration
    Config config_;

    // Pin event structure for the queue
    struct PinEvent {
        uint8_t pin;
        uint8_t level;
    };

    // Task handles
    TaskHandle_t pollTaskHandle_ = nullptr;
    TaskHandle_t callbackTaskHandle_ = nullptr;
    TaskHandle_t interruptTaskHandle_ = nullptr;

    // Current pin states
    uint8_t inputState_ = 0;
    uint8_t outputState_ = 0;
    uint8_t configState_ = 0xFF;  // All pins as inputs by default

    // Pin callbacks
    struct CallbackInfo {
        PinChangeCallback callback;
        bool activeLow;
    };
    std::map<uint8_t, CallbackInfo> callbacks_;

    // Mutex for thread safety
    std::mutex mutex_;
    
    // I2C handles
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    i2c_master_dev_handle_t i2c_device_ = nullptr;

    // Event queue for pin changes and interrupt synchronization
    QueueHandle_t eventQueue_ = nullptr;
    
    // Interrupt flags
    bool interruptEnabled_ = false;
    bool interruptRunning_ = false;

    // I2C register read/write methods
    esp_err_t readRegister(uint8_t reg, uint8_t* data);
    esp_err_t writeRegister(uint8_t reg, uint8_t data);

    // Task functions
    static void pollTask(void* arg);
    static void callbackTask(void* arg);
    static void interruptTask(void* arg);
    bool pollRunning_ = false;
    bool callbackRunning_ = false;
    
    // Input polling and interrupt methods
    esp_err_t setupInterrupt();
    esp_err_t cleanupInterrupt();
    
    // Static ISR handler (needs to be static to be registered with ESP-IDF)
    static void IRAM_ATTR isrHandler(void* arg);
    
    // Store static instance pointer for ISR
    static TCA6408A* isrInstance_;

    // Helper methods
    bool isValidPin(uint8_t pin) const { return pin < 8; }

    // Logger tag
    static constexpr const char* TAG = "TCA6408A";
};
