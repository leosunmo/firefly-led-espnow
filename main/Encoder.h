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
        uint16_t poll_interval_ms;    // Interval for polling position if needed
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

private:
    // Private methods
    static void taskFunction(void* arg);
    void pollTask();
    static bool pcntEventCallback(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx);
    
    // Instance variables
    Config config_;
    Callback callback_;
    TaskHandle_t task_handle_;
    bool running_;
    
    // PCNT handles
    pcnt_unit_handle_t pcnt_unit_;
    pcnt_channel_handle_t pcnt_chan_a_;
    pcnt_channel_handle_t pcnt_chan_b_;
    
    // Encoder state
    int32_t position_;
    
    // Logger tag
    char tag_[32];
};
