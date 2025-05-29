#include "Button.h"
#include "button_gpio.h"
#include "esp_log.h"
#include <map>

// Map to track button instances by their handle
static std::map<button_handle_t, Button*> button_instances;


Button::Button(const Config& config) :
    button_handle_(nullptr), // Initialize button handle to null
    callback_(nullptr), // Initialize callback to null
    config_(config){

    sniprintf(tag_, sizeof(tag_), "Button_%s", config.name.c_str());
       
    ESP_LOGI(tag_, "Initializing button on GPIO %ld", config.gpio_num);

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
        ESP_LOGE(tag_, "Failed to create button");
    }

    // Add this button to the instances map
    button_instances[button_handle_] = this;

    // Register button callbacks
    ret = iot_button_register_cb(button_handle_, BUTTON_SINGLE_CLICK, NULL, handleSingleClick, button_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to register single click: %s", esp_err_to_name(ret));
    }

    ret = iot_button_register_cb(button_handle_, BUTTON_DOUBLE_CLICK, NULL, handleDoubleClick, button_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to register double click: %s", esp_err_to_name(ret));
    }

    // Register long press callback with specific time threshold
    button_event_args_t long_press_args = {0};
    long_press_args.long_press.press_time = config.long_press_time_ms;
    ret = iot_button_register_cb(button_handle_, BUTTON_LONG_PRESS_START, &long_press_args, handleLongPressStart, button_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to register long press: %s", esp_err_to_name(ret));
    }

    // Register release callback
    ret = iot_button_register_cb(button_handle_, BUTTON_PRESS_UP, NULL, handleButtonRelease, button_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to register release: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(tag_, "Button initialized successfully");
}

Button::~Button() {
    if (button_handle_ != nullptr) {
        // Remove from the instances map
        button_instances.erase(button_handle_);
        // Delete the button
        iot_button_delete(button_handle_);
        button_handle_ = nullptr;
    }
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

// Static handlers that dispatch to the correct button instance
void Button::handleSingleClick(void* arg, void* user_data) {
    button_handle_t handle = static_cast<button_handle_t>(user_data);
    if (button_instances.count(handle) > 0) {
        button_instances[handle]->onSingleClick();
    }
}

void Button::handleDoubleClick(void* arg, void* user_data) {
    button_handle_t handle = static_cast<button_handle_t>(user_data);
    if (button_instances.count(handle) > 0) {
        button_instances[handle]->onDoubleClick();
    }
}

void Button::handleLongPressStart(void* arg, void* user_data) {
    button_handle_t handle = static_cast<button_handle_t>(user_data);
    if (button_instances.count(handle) > 0) {
        button_instances[handle]->onLongPressStart();
    }
}

void Button::handleButtonRelease(void* arg, void* user_data) {
    button_handle_t handle = static_cast<button_handle_t>(user_data);
    if (button_instances.count(handle) > 0) {
        button_instances[handle]->onButtonRelease();
    }
}

// Instance methods for handling events
void Button::onSingleClick() {
    ESP_LOGI(tag_, "Button single click detected");
    if (callback_) {
        callback_(Event::PRESSED);
    }
}

void Button::onDoubleClick() {
    ESP_LOGI(tag_, "Button double click detected");
    if (callback_) {
        callback_(Event::DOUBLE_PRESSED);
    }
}

void Button::onLongPressStart() {
    ESP_LOGI(tag_, "Button long press detected");
    if (callback_) {
        callback_(Event::LONG_PRESSED);
    }
}

void Button::onButtonRelease() {
    ESP_LOGI(tag_, "Button released");
    if (callback_) {
        callback_(Event::RELEASED);
    }
}