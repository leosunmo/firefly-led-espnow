#pragma once

// Should match the original Potentiometer.h interface as closely as possible
#include <functional>
#include <string>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
#include "esp_log.h"
#include "ADS1015.h"

/**
 * @brief I2C Potentiometer handler for managing analog inputs through ADS1015
 */
class I2CPotentiometer {
public:
    // Maximum ADC value for ADS1015 (12-bit ADC)
    // For 12-bit resolution, the maximum value is 2047, but since
    // we're using gain 1 (+/- 4.096V), the effective range is 0-1648
    // if we apply 3.3v.
    static constexpr uint32_t MAX_ADC_VALUE = 1648;
    
    /**
     * @brief Potentiometer event types used in callbacks
     */
    enum class Event {
        VALUE_CHANGED,    // Potentiometer value has changed beyond threshold
        MIN_REACHED,      // Potentiometer reached minimum value
        MAX_REACHED,      // Potentiometer reached maximum value
        CENTER_REACHED    // Potentiometer reached center value
    };

    /**
     * @brief I2CPotentiometer configuration
     */
    struct Config {
        std::string name;                       // Potentiometer name (for identification in logs)
        std::shared_ptr<ADS1015> adc;           // I2C ADC device
        ADS1015::Channel channel;               // ADC channel to use for this potentiometer
        ADS1015::Gain gain = ADS1015::Gain::GAIN_TWO;  // Gain setting
        uint32_t poll_interval_ms;              // Interval in ms between ADC readings
        uint32_t change_threshold;              // Minimum change in ADC value to trigger event
        bool enable_center_event;               // Whether to trigger center-position events
        uint32_t center_threshold;              // Threshold around center to trigger center events
        bool use_cumulative_tracking = true;    // Whether to track cumulative changes
    };

    /**
     * @brief Callback type for potentiometer events
     */
    using Callback = std::function<void(Event event, uint32_t value, float percentage)>;

    /**
     * @brief Constructor
     * @param config Potentiometer configuration
     */
    I2CPotentiometer(const Config& config);

    /**
     * @brief Destructor to clean up resources
     */
    ~I2CPotentiometer();

    /**
     * @brief Initialize the potentiometer
     * @return true if initialization was successful
     */
    bool init();

    /**
     * @brief Register callback for potentiometer events
     * @param callback Function to call when potentiometer events occur
     */
    void registerCallback(Callback callback);

    /**
     * @brief Get the current raw ADC value
     * @return Raw ADC value (0-2047 for ADS1015)
     */
    uint32_t getRawValue();
    
    /**
     * @brief Get the current value as a percentage (0-100%)
     * @return Percentage value from 0.0 to 100.0
     */
    float getPercentage();

    /**
     * @brief Start the monitoring task
     */
    void start();

    /**
     * @brief Stop the monitoring task
     */
    void stop();

private:
    // Private methods
    static void taskFunction(void* arg);
    void pollTask();
    
    // Instance variables
    Config config_;
    Callback callback_;
    TaskHandle_t task_handle_;
    bool running_;
    uint32_t last_raw_value_;
    uint32_t last_reported_value_;  // Value at last VALUE_CHANGED event
    int32_t cumulative_change_;     // Accumulates changes between reports
    
    // Logger tag
    char tag_[32];
};
