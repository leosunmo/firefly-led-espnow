#pragma once

#include <functional>
#include <string>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "TCA6408A.h"

/**
 * @brief Button handler for TCA6408A I2C GPIO Expander
 */
class Button {
public:
    /**
     * @brief Button event types used in callbacks
     */
    enum class Event {
        PRESSED,         // Button pressed
        RELEASED         // Button released
    };

    /**
     * @brief Button configuration
     */
    struct Config {
        std::string name;                         // Button name (for identification in logs)
        std::shared_ptr<TCA6408A> i2c_expander;   // I2C expander instance
        uint8_t pin;                              // Pin number on TCA6408A (0-7)
        bool active_low;                          // true if button is active low (most common)
        uint16_t debounce_time_ms;                // Debounce time in ms
    };

    /**
     * @brief Callback type for button events
     */
    using Callback = std::function<void(Event event)>;

    /**
     * @brief Constructor
     * @param config Button configuration
     */
    Button(const Config& config);

    /**
     * @brief Destructor to clean up resources
     */
    ~Button();

    /**
     * @brief Initialize the button
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t init();

    /**
     * @brief Register callback for button events
     * @param callback Function to call when button events occur
     */
    void registerCallback(Callback callback);

    /**
     * @brief Check if the button is currently pressed
     * @return true if button is pressed
     */
    bool isPressed();

private:
    // Handle I2C pin changes
    void handlePinChange(uint8_t pin, uint8_t level);

    // Process button state
    void processButtonState(bool pressed);

    // FreeRTOS timer for debounce
    TimerHandle_t debounceTimer_ = nullptr;
    static void onDebounceTimerExpired(TimerHandle_t timer);

    // Button state tracking
    bool isPressed_ = false;
    
    // User callback
    Callback callback_;

    // Button configuration
    Config config_;
    
    // Logger tag
    char tag_[32];
};
