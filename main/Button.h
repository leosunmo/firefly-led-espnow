#pragma once

#include <functional>
#include <string>
#include <memory>
#include "iot_button.h"
#include "button_types.h"
#include "button_gpio.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

/**
 * @brief Button handler for managing ESP32 buttons using ESP-IoT-Solution button component
 */
class Button {
public:
    /**
     * @brief Button event types used in callbacks
     */
    enum class Event {
        PRESSED,         // Button pressed (single click)
        DOUBLE_PRESSED,  // Button double clicked
        LONG_PRESSED,    // Button held for a longer time
        RELEASED         // Button released
    };

    /**
     * @brief Button configuration
     */
    struct Config {
        std::string name;             // Button name (for identification in logs)
        int32_t gpio_num;             // GPIO pin number
        bool active_low;              // true if button is active low (most common)
        uint16_t long_press_time_ms;  // Time in ms to consider as long press
        uint16_t short_press_time_ms; // Time in ms to consider as short press
    };

    /**
     * @brief Callback type for button events
     */
    using Callback = std::function<void(Event event)>;

    /**
     * @brief Initialize a new button
     * @param config Button configuration
     * @return true if initialization was successful
     */
    static bool init(const Config& config);

    /**
     * @brief Register callback for button events
     * @param callback Function to call when button events occur
     */
    static void registerCallback(Callback callback);

    /**
     * @brief Check if the button is currently pressed
     * @return true if button is pressed
     */
    static bool isPressed();

private:
    // Button event handler callbacks
    static void handleSingleClick(void* arg, void* user_data);
    static void handleDoubleClick(void* arg, void* user_data);
    static void handleLongPressStart(void* arg, void* user_data);
    static void handleButtonRelease(void* arg, void* user_data);

    // Button handle
    static button_handle_t button_handle_;
    
    // User callback
    static Callback callback_;

    // Button name for identification
    static std::string button_name_;
    
    // Logger tag
    static const char* TAG;
};
