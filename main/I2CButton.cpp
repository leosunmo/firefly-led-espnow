#include "I2CButton.h"
#include "esp_log.h"
#include <functional>

Button::Button(const Config& config) :
    callback_(nullptr),
    config_(config) {

    snprintf(tag_, sizeof(tag_), "Button_%s", config.name.c_str());
    ESP_LOGI(tag_, "Creating button on TCA6408A pin %d", config.pin);
}

esp_err_t Button::init() {
    if (config_.i2c_expander == nullptr) {
        ESP_LOGE(tag_, "TCA6408A instance is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Check if pin is already configured as input
    bool isInput = false;
    esp_err_t ret = config_.i2c_expander->isPinInput(config_.pin, &isInput);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to check pin configuration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure as input if it's not already
    if (!isInput) {
        ESP_LOGW(tag_, "Pin %d is not configured as input, configuring now", config_.pin);
        ret = config_.i2c_expander->configurePin(config_.pin, false); // false = input
        if (ret != ESP_OK) {
            ESP_LOGE(tag_, "Failed to configure pin %d as input: %s", 
                    config_.pin, esp_err_to_name(ret));
            return ret;
        }
    } else {
        ESP_LOGD(tag_, "Pin %d is already configured as input", config_.pin);
    }

    // Create debounce timer
    debounceTimer_ = xTimerCreate(
        tag_,                              // Timer name
        pdMS_TO_TICKS(config_.debounce_time_ms), // Timer period
        pdFALSE,                           // Auto-reload
        this,                              // Timer ID is the button instance
        onDebounceTimerExpired             // Callback
    );
    
    if (debounceTimer_ == nullptr) {
        ESP_LOGE(tag_, "Failed to create debounce timer");
        return ESP_ERR_NO_MEM;
    }

    // Register pin change callback
    using namespace std::placeholders;
    ret = config_.i2c_expander->registerCallback(
        config_.pin,
        std::bind(&Button::handlePinChange, this, _1, _2),
        config_.active_low
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to register pin change callback: %s", 
                esp_err_to_name(ret));
        return ret;
    }

    // Start monitoring
    ret = config_.i2c_expander->startMonitoring();
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to start monitoring: %s", 
                esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(tag_, "Button initialized successfully");
    return ESP_OK;
}

Button::~Button() {
    if (debounceTimer_ != nullptr) {
        xTimerDelete(debounceTimer_, portMAX_DELAY);
        debounceTimer_ = nullptr;
    }
}

void Button::registerCallback(Callback callback) {
    callback_ = callback;
}

bool Button::isPressed() {
    if (config_.i2c_expander == nullptr) {
        return false;
    }
    
    uint8_t level;
    esp_err_t ret = config_.i2c_expander->readPin(config_.pin, &level);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to read pin state: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Convert to pressed state based on active_low setting
    return (config_.active_low) ? (level == 0) : (level == 1);
}

void Button::handlePinChange(uint8_t pin, uint8_t level) {
    // Level is already adjusted for active_low in TCA6408A
    bool pressed = level == 1;
    
    // Start the debounce timer
    if (debounceTimer_ != nullptr) {
        xTimerStop(debounceTimer_, 0);
        xTimerStart(debounceTimer_, 0);
    }
    
    // Process state change
    processButtonState(pressed);
}

void Button::processButtonState(bool pressed) {
    if (pressed && !isPressed_) {
        // Button was just pressed
        isPressed_ = true;
        
        if (callback_) {
            callback_(Event::PRESSED);
            ESP_LOGI(tag_, "Button pressed");
        }
    }
    else if (!pressed && isPressed_) {
        // Button was just released
        isPressed_ = false;
        
        if (callback_) {
            callback_(Event::RELEASED);
            ESP_LOGI(tag_, "Button released");
        }
    }
}

void Button::onDebounceTimerExpired(TimerHandle_t timer) {
    // This function is now only used for debouncing
    // No additional action needed when timer expires
}