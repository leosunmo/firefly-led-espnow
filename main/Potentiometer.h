#pragma once

// Define this to enable ADC calibration for voltage readings
// Currently only used for debugging voltage outputs
// #define ENABLE_ADC_CALIBRATION

#include <functional>
#include <string>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "config.h"
#ifdef ENABLE_ADC_CALIBRATION
#include "esp_adc/adc_cali.h"
#endif
#include "esp_log.h"

/**
 * @brief Potentiometer handler for managing ESP32 analog input potentiometers
 */
class Potentiometer {
public:
    // Maximum ADC value for ESP32-C6 with 12dB attenuation (empirically determined)
    static constexpr uint32_t MAX_ADC_VALUE = 3200;
    
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
     * @brief ADC attenuation options
     */
    enum class Attenuation {
        DB_0,   // No attenuation (ADC_ATTEN_DB_0)
        DB_2_5, // 2.5dB attenuation (ADC_ATTEN_DB_2_5)
        DB_6,   // 6dB attenuation (ADC_ATTEN_DB_6)
        DB_12   // 12dB attenuation (ADC_ATTEN_DB_12)
    };

    /**
     * @brief Potentiometer configuration
     */
    struct Config {
        std::string name;             // Potentiometer name (for identification in logs)
        int gpio_num;                 // GPIO pin number for ADC input
        adc_unit_t adc_unit = ADC_UNIT_1;          // ADC unit (ADC_UNIT_1 or ADC_UNIT_2)
        adc_channel_t adc_channel = ADC_CHANNEL_1;    // ADC channel for this GPIO pin
        Attenuation attenuation = Attenuation::DB_12;      // Signal attenuation configuration
        uint32_t poll_interval_ms;    // Interval in ms between ADC readings
        uint32_t change_threshold;    // Minimum change in ADC value to trigger an event
        bool enable_center_event;     // Whether to trigger center-position events
        uint32_t center_threshold;    // Threshold around center to trigger center events
    };

    /**
     * @brief Callback type for potentiometer events
     */
    using Callback = std::function<void(Event event, uint32_t value, float percentage)>;

    /**
     * @brief Constructor
     * @param config Potentiometer configuration
     */
    Potentiometer(const Config& config);

    /**
     * @brief Destructor to clean up resources
     */
    ~Potentiometer();

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
     * @return Raw ADC value (0-4095 for ESP32)
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
    adc_atten_t getESPAttenuation();
    
    // Instance variables
    Config config_;
    Callback callback_;
    adc_oneshot_unit_handle_t adc_handle_;
    TaskHandle_t task_handle_;
    bool running_;
    uint32_t last_raw_value_;
    
#ifdef ENABLE_ADC_CALIBRATION
    // ADC calibration related
    bool calibration_enabled_;
    adc_cali_handle_t cali_handle_;
#endif
    
    // Logger tag
    char tag_[32];
};
