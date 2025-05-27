#include "Button.h"
#include "button_gpio.h"
#include "esp_log.h"

// Initialize static members
button_handle_t Button::button_handle_ = nullptr;
Button::Callback Button::callback_ = nullptr;
std::string Button::button_name_;
const char* Button::TAG = "Button"; // Default tag value

bool Button::init(const Config& config) {
    // Store the button name
    button_name_ = config.name;
    
    // Create a custom tag based on the button name
    static char tag_buffer[32]; // Static buffer to ensure persistence
    if (!button_name_.empty()) {
        snprintf(tag_buffer, sizeof(tag_buffer), "Button_%s", button_name_.c_str());
        TAG = tag_buffer;
    }
    
    ESP_LOGI(TAG, "Initializing button on GPIO %ld", config.gpio_num);
    
    // Configure the button
    button_config_t button_config = {
        .long_press_time = config.long_press_time_ms,
        .short_press_time = config.short_press_time_ms,
    };

    // Configure button GPIO config
    button_gpio_config_t gpio_config = {
        .gpio_num = config.gpio_num,
        .active_level = (uint8_t)(config.active_low ? 0 : 1), // Active low if true, otherwise high
        .enable_power_save = false, // Set default value to avoid missing initializer
        .disable_pull = false // Set default value to avoid missing initializer
    };

    // Create the GPIO button
    esp_err_t ret = iot_button_new_gpio_device(&button_config, &gpio_config, &button_handle_);
    if (ret != ESP_OK || button_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create button");
        return false;
    }
    
    // Register button callbacks (single and double click don't need extra args)
    ret = iot_button_register_cb(button_handle_, BUTTON_SINGLE_CLICK, NULL, handleSingleClick, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register single click: %s", esp_err_to_name(ret));
    }
    
    ret = iot_button_register_cb(button_handle_, BUTTON_DOUBLE_CLICK, NULL, handleDoubleClick, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register double click: %s", esp_err_to_name(ret));
    }
    
    // Register long press callback with specific time threshold
    button_event_args_t long_press_args = {0};
    long_press_args.long_press.press_time = config.long_press_time_ms;
    ret = iot_button_register_cb(button_handle_, BUTTON_LONG_PRESS_START, &long_press_args, handleLongPressStart, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register long press: %s", esp_err_to_name(ret));
    }
    
    // Register release callback
    ret = iot_button_register_cb(button_handle_, BUTTON_PRESS_UP, NULL, handleButtonRelease, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register release: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "Button initialized successfully");
    return true;
}

void Button::registerCallback(Callback callback) {
    callback_ = callback;
}

bool Button::isPressed() {
    if (button_handle_ == nullptr) {
        return false;
    }
    
    return iot_button_get_key_level(button_handle_) == 1;
}

void Button::handleSingleClick(void* arg, void* user_data) {
    ESP_LOGI(TAG, "Button single click detected");
    if (callback_) {
        callback_(Event::PRESSED);
    }
}

void Button::handleDoubleClick(void* arg, void* user_data) {
    ESP_LOGI(TAG, "Button double click detected");
    if (callback_) {
        callback_(Event::DOUBLE_PRESSED);
    }
}

void Button::handleLongPressStart(void* arg, void* user_data) {
    ESP_LOGI(TAG, "Button long press detected");
    if (callback_) {
        callback_(Event::LONG_PRESSED);
    }
}

void Button::handleButtonRelease(void* arg, void* user_data) {
    ESP_LOGI(TAG, "Button released");
    if (callback_) {
        callback_(Event::RELEASED);
    }
}
