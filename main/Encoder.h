#pragma once

#include <functional>
#include <string>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"

/**
 * @brief Rotary Encoder handler for ESP32
 * 
 * Features:
 * - Interrupt-driven event detection using PCNT
 * - Configurable debounce to prevent multiple events from a single rotation
 * - Configurable glitch filter for hardware noise reduction
 * - FreeRTOS task notification for efficient event handling
 */
class Encoder {
public:
    /**
     * @brief Encoder event types used in callbacks
     */
    enum class Event {
        CLOCKWISE,           // Encoder rotated clockwise
        COUNTER_CLOCKWISE    // Encoder rotated counter-clockwise
    };

    /**
     * @brief Encoder configuration
     */
    struct Config {
        std::string name;             // Encoder name (for identification in logs)
        int32_t a_pin;                // GPIO pin for encoder signal A
        int32_t b_pin;                // GPIO pin for encoder signal B
        uint32_t debounce_ms;         // Debounce time in milliseconds (default: 50ms)
        
        // Constructor with defaults for new parameters
        Config() : name("encoder"), a_pin(-1), b_pin(-1), debounce_ms(50) {}
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
     * @brief Start the encoder monitoring using callbacks
     */
    void start();

    /**
     * @brief Stop the encoder monitoring
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
     * @brief Process any pending encoder events
     * @return true if an event was processed
     * 
     * This should be called periodically from a task context to handle
     * encoder events that were triggered by interrupts.
     */
    bool processEvents();

private:
    // Private methods
    static bool pcntEventCallback(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx);
    static void taskFunction(void* arg);

    // Instance variables
    Config config_;
    Callback callback_;
    TaskHandle_t task_handle_;
    bool running_;
    // Event handling
    Event last_event_;
    TickType_t last_event_time_;      // Timestamp of the last event (for debouncing in ISR)
    TickType_t last_processed_time_;  // Timestamp of the last processed event (in task context)
    uint32_t debounced_count_;        // Counter for debounced events (for debugging)
    
    // PCNT handles
    pcnt_unit_handle_t pcnt_unit_;
    pcnt_channel_handle_t pcnt_chan_a_;
    pcnt_channel_handle_t pcnt_chan_b_;
    
    // Encoder state
    int32_t position_;
    
    // Logger tag
    char tag_[32];
};
