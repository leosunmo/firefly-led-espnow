#include "Encoder.h"
#include <cstring>

Encoder::Encoder(const Config& config)
    : config_(config), 
      task_handle_(nullptr),
      running_(false),
      position_(0),
      last_encoded_(0),
      last_button_state_(false),
      button_press_start_(0),
      long_press_fired_(false)
{
    // Initialize tag for logging
    snprintf(tag_, sizeof(tag_), "Encoder:%s", config_.name.c_str());
    ESP_LOGI(tag_, "Creating encoder on pins A:%ld B:%ld Btn:%ld", 
             config_.a_pin, config_.b_pin, config_.btn_pin);
}

Encoder::~Encoder() {
    stop();
    ESP_LOGI(tag_, "Encoder destroyed");
}

bool Encoder::init() {
    ESP_LOGI(tag_, "Initializing encoder");

    // Configure GPIO pins for encoder signals
    gpio_config_t io_conf = {};
    
    // Configure A and B pins as inputs with pull-ups
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << config_.a_pin) | (1ULL << config_.b_pin);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;  // We'll use polling instead of interrupts
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to configure encoder pins: %s", esp_err_to_name(ret));
        return false;
    }

    // Configure button pin if available
    if (config_.has_button && config_.btn_pin >= 0) {
        io_conf.pin_bit_mask = (1ULL << config_.btn_pin);
        if (config_.active_low) {
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        } else {
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        }
        
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(tag_, "Failed to configure button pin: %s", esp_err_to_name(ret));
            // Continue anyway, as the encoder can work without the button
        }
    }

    // Initialize state
    position_ = 0;
    
    // Read initial state
    uint8_t a_val = gpio_get_level(static_cast<gpio_num_t>(config_.a_pin));
    uint8_t b_val = gpio_get_level(static_cast<gpio_num_t>(config_.b_pin));
    last_encoded_ = (a_val << 1) | b_val;
    
    if (config_.has_button && config_.btn_pin >= 0) {
        last_button_state_ = gpio_get_level(static_cast<gpio_num_t>(config_.btn_pin));
        if (config_.active_low) {
            last_button_state_ = !last_button_state_;
        }
    }

    ESP_LOGI(tag_, "Encoder initialized successfully");
    return true;
}

void Encoder::registerCallback(Callback callback) {
    callback_ = callback;
}

void Encoder::start() {
    if (running_) {
        ESP_LOGW(tag_, "Encoder task already running");
        return;
    }
    
    running_ = true;
    ESP_LOGI(tag_, "Starting encoder monitoring task");
    
    // Create task for polling encoder state
    BaseType_t ret = xTaskCreate(
        taskFunction,
        tag_,
        2048,         // Stack size
        this,         // Task parameter
        tskIDLE_PRIORITY + 1,  // Priority
        &task_handle_
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(tag_, "Failed to create encoder task");
        running_ = false;
    }
}

void Encoder::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    ESP_LOGI(tag_, "Stopping encoder monitoring task");
    
    // Give the task time to exit gracefully
    vTaskDelay(pdMS_TO_TICKS(100));
    
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
}

void Encoder::reset() {
    position_ = 0;
    ESP_LOGI(tag_, "Encoder position reset to 0");
}

int32_t Encoder::getPosition() {
    return position_;
}

void Encoder::setPosition(int32_t position) {
    position_ = position;
    ESP_LOGI(tag_, "Encoder position set to %ld", position);
}

bool Encoder::isButtonPressed() {
    if (!config_.has_button || config_.btn_pin < 0) {
        return false;
    }
    
    bool state = gpio_get_level(static_cast<gpio_num_t>(config_.btn_pin));
    if (config_.active_low) {
        state = !state;
    }
    
    return state;
}

void Encoder::taskFunction(void* arg) {
    Encoder* encoder = static_cast<Encoder*>(arg);
    encoder->pollTask();
}

void Encoder::pollTask() {
    ESP_LOGI(tag_, "Encoder monitoring task started");
    
    while (running_) {
        // Read and process encoder state
        readEncoder();
        
        // Check button if available
        if (config_.has_button && config_.btn_pin >= 0) {
            bool current_state = gpio_get_level(static_cast<gpio_num_t>(config_.btn_pin));
            if (config_.active_low) {
                current_state = !current_state;
            }
            
            handleButton(current_state);
        }
        
        // Sleep for the polling interval
        vTaskDelay(pdMS_TO_TICKS(config_.poll_interval_ms));
    }
    
    ESP_LOGI(tag_, "Encoder monitoring task ended");
    vTaskDelete(nullptr);
}

void Encoder::readEncoder() {
    // Read current state of encoder pins
    uint8_t a_val = gpio_get_level(static_cast<gpio_num_t>(config_.a_pin));
    uint8_t b_val = gpio_get_level(static_cast<gpio_num_t>(config_.b_pin));
    uint8_t encoded = (a_val << 1) | b_val;
    
    // Process encoder state change using Gray code
    uint8_t sum = (last_encoded_ << 2) | encoded;
    
    int32_t old_position = position_;
    
    // Determine direction based on state change pattern
    switch (sum) {
        case 0b1101:
        case 0b0100:
        case 0b0010:
        case 0b1011:
            position_++;  // Clockwise
            break;
        case 0b1110:
        case 0b0111:
        case 0b0001:
        case 0b1000:
            position_--;  // Counter-clockwise
            break;
    }
    
    last_encoded_ = encoded;
    
    // Only trigger callbacks if position has changed
    if (position_ != old_position && callback_) {
        if (position_ > old_position) {
            callback_(Event::CLOCKWISE, position_);
        } else {
            callback_(Event::COUNTER_CLOCKWISE, position_);
        }
    }
}

void Encoder::handleButton(bool current_state) {
    // Detect button state changes with simple debouncing
    if (current_state != last_button_state_) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Ensure button state has been stable for debounce period
        static uint32_t last_change_time = 0;
        if (now - last_change_time < config_.debounce_time_ms) {
            return;
        }
        
        last_change_time = now;
        last_button_state_ = current_state;
        
        if (current_state) {
            // Button pressed
            button_press_start_ = now;
            long_press_fired_ = false;
            if (callback_) {
                callback_(Event::BUTTON_PRESSED, position_);
            }
        } else {
            // Button released
            if (callback_) {
                callback_(Event::BUTTON_RELEASED, position_);
            }
        }
    }
    
    // Check for long press if button is currently pressed
    if (current_state && !long_press_fired_) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if ((now - button_press_start_) >= config_.long_press_time_ms) {
            long_press_fired_ = true;
            if (callback_) {
                callback_(Event::BUTTON_LONG_PRESSED, position_);
            }
        }
    }
}
