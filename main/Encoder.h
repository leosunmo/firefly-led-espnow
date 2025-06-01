#pragma once

#include <functional>
#include <string>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

/**
 * @brief Rotary Encoder handler for ESP32
 */
class Encoder {
public:
    /**
     * @brief Encoder event types used in callbacks
     */
    enum class Event {
        CLOCKWISE,           // Encoder rotated clockwise
        COUNTER_CLOCKWISE,   // Encoder rotated counter-clockwise
        BUTTON_PRESSED,      // Encoder button pressed (if available)
        BUTTON_RELEASED,     // Encoder button released (if available)
        BUTTON_LONG_PRESSED  // Encoder button long pressed (if available)
    };

    /**
     * @brief Encoder configuration
     */
    struct Config {
        std::string name;             // Encoder name (for identification in logs)
        int32_t a_pin;                // GPIO pin for encoder signal A
        int32_t b_pin;                // GPIO pin for encoder signal B
        int32_t btn_pin;              // GPIO pin for encoder button (optional, -1 for none)
        bool has_button;              // Whether this encoder has a button
        bool active_low;              // Whether button is active low (if used)
        uint16_t debounce_time_ms;    // Debounce time for input signals
        uint16_t long_press_time_ms;  // Time in ms for button long press (if used)
        uint16_t poll_interval_ms;    // Interval for polling encoder state
    };

    /**
     * @brief Callback type for encoder events
     */
    using Callback = std::function<void(Event event, int32_t position)>;

    /**
     * @brief Constructor
     * @param config Encoder configuration
     */
    Encoder(const Config& config);

    /**
     * @brief Destructor to clean up resources
     */
    ~Encoder();

    /**
     * @brief Initialize the encoder
     * @return true if initialization was successful
     */
    bool init();

    /**
     * @brief Register callback for encoder events
     * @param callback Function to call when encoder events occur
     */
    void registerCallback(Callback callback);

    /**
     * @brief Start the encoder monitoring task
     */
    void start();

    /**
     * @brief Stop the encoder monitoring task
     */
    void stop();

    /**
     * @brief Reset the encoder position to zero
     */
    void reset();

    /**
     * @brief Get the current encoder position
     * @return Current encoder position (positive or negative from start position)
     */
    int32_t getPosition();

    /**
     * @brief Set the encoder position
     * @param position New encoder position
     */
    void setPosition(int32_t position);

    /**
     * @brief Check if the encoder button is currently pressed
     * @return true if button is pressed, false otherwise or if no button
     */
    bool isButtonPressed();

private:
    // Private methods
    static void taskFunction(void* arg);
    void pollTask();
    void readEncoder();
    void handleButton(bool current_state);

    // Instance variables
    Config config_;
    Callback callback_;
    TaskHandle_t task_handle_;
    bool running_;
    
    // Encoder state
    int32_t position_;
    uint8_t last_encoded_;
    bool last_button_state_;
    uint32_t button_press_start_;
    bool long_press_fired_;
    
    // Logger tag
    char tag_[32];
};
