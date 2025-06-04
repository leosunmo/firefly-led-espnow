#include "Encoder.h"
#include <cstring>

Encoder::Encoder(const Config& config)
    : config_(config), 
      task_handle_(nullptr),
      running_(false),
      pcnt_unit_(nullptr),
      pcnt_chan_a_(nullptr),
      pcnt_chan_b_(nullptr),
      position_(0)
{
    // Initialize tag for logging
    snprintf(tag_, sizeof(tag_), "Encoder:%s", config_.name.c_str());
    ESP_LOGI(tag_, "Creating encoder on pins A:%ld B:%ld", 
             config_.a_pin, config_.b_pin);
}

Encoder::~Encoder() {
    stop();
    
    // Clean up PCNT resources
    if (pcnt_unit_) {
        pcnt_unit_stop(pcnt_unit_);
        pcnt_unit_disable(pcnt_unit_);
        
        if (pcnt_chan_a_) {
            pcnt_del_channel(pcnt_chan_a_);
        }
        
        if (pcnt_chan_b_) {
            pcnt_del_channel(pcnt_chan_b_);
        }
        
        pcnt_del_unit(pcnt_unit_);
    }
    
    // No queue to delete
    
    ESP_LOGI(tag_, "Encoder destroyed");
}

bool Encoder::init() {
    ESP_LOGI(tag_, "Initializing encoder with PCNT");

    // Set up PCNT unit with wider limits to ensure we capture events
    pcnt_unit_config_t unit_config = {
        .low_limit = -16,  // Set wider limit for reliable triggering
        .high_limit = 16,  // Set wider limit for reliable triggering
    };
    unit_config.flags.accum_count = true;  // Use hardware accumulation
    
    esp_err_t ret = pcnt_new_unit(&unit_config, &pcnt_unit_);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to create PCNT unit: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Set up glitch filter to debounce encoder signals
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,  // 1000ns = 1us glitch filtering
    };
    ret = pcnt_unit_set_glitch_filter(pcnt_unit_, &filter_config);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to set PCNT glitch filter: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Set up channel A
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = config_.a_pin,
        .level_gpio_num = config_.b_pin,
    };
    ret = pcnt_new_channel(pcnt_unit_, &chan_a_config, &pcnt_chan_a_);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to create PCNT channel A: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Set up channel B
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = config_.b_pin,
        .level_gpio_num = config_.a_pin,
    };
    ret = pcnt_new_channel(pcnt_unit_, &chan_b_config, &pcnt_chan_b_);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to create PCNT channel B: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Set edge and level actions for rotary encoding
    // For channel A (count up when A leads B)
    ret = pcnt_channel_set_edge_action(pcnt_chan_a_, 
                                      PCNT_CHANNEL_EDGE_ACTION_HOLD,   // No count on negative edge
                                      PCNT_CHANNEL_EDGE_ACTION_INCREASE); // Count up on positive edge
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to set edge action for PCNT channel A: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = pcnt_channel_set_level_action(pcnt_chan_a_, 
                                       PCNT_CHANNEL_LEVEL_ACTION_KEEP,   // No effect when B is low
                                       PCNT_CHANNEL_LEVEL_ACTION_INVERSE); // Reverse count when B is high
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to set level action for PCNT channel A: %s", esp_err_to_name(ret));
        return false;
    }
    
    // For channel B (count up when B leads A)
    ret = pcnt_channel_set_edge_action(pcnt_chan_b_, 
                                      PCNT_CHANNEL_EDGE_ACTION_HOLD,   // No count on negative edge
                                      PCNT_CHANNEL_EDGE_ACTION_INCREASE); // Count up on positive edge
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to set edge action for PCNT channel B: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = pcnt_channel_set_level_action(pcnt_chan_b_, 
                                       PCNT_CHANNEL_LEVEL_ACTION_KEEP,   // No effect when A is low
                                       PCNT_CHANNEL_LEVEL_ACTION_INVERSE); // Reverse count when A is high
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to set level action for PCNT channel B: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(tag_, "Edge and level actions configured for quadrature encoding");
    
    // No need for a queue as we use direct callbacks
    
    // Register PCNT event callback
    pcnt_event_callbacks_t cbs = {
        .on_reach = pcntEventCallback,
    };
    ret = pcnt_unit_register_event_callbacks(pcnt_unit_, &cbs, this);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to register PCNT event callback: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Add watch points to trigger events when counter reaches certain values
    // Add multiple watch points to ensure we catch rotation events
    ret = pcnt_unit_add_watch_point(pcnt_unit_, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to add positive watch point: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = pcnt_unit_add_watch_point(pcnt_unit_, -4);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to add negative watch point: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Add watch points at unit limits to ensure we don't miss events
    ret = pcnt_unit_add_watch_point(pcnt_unit_, unit_config.high_limit);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to add high limit watch point: %s", esp_err_to_name(ret));
        // Continue anyway, not critical
    }
    
    ret = pcnt_unit_add_watch_point(pcnt_unit_, unit_config.low_limit);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to add low limit watch point: %s", esp_err_to_name(ret));
        // Continue anyway, not critical
    }
    
    // No button configuration needed

    // Initialize position
    position_ = 0;

    ESP_LOGI(tag_, "Encoder initialized successfully with PCNT");
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
    
    ESP_LOGI(tag_, "Starting encoder monitoring");
    
    // Enable and start PCNT unit
    esp_err_t ret = pcnt_unit_enable(pcnt_unit_);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to enable PCNT unit: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = pcnt_unit_clear_count(pcnt_unit_);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to clear PCNT count: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = pcnt_unit_start(pcnt_unit_);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to start PCNT unit: %s", esp_err_to_name(ret));
        return;
    }
    
    // Create task for both button monitoring and tracking accumulated encoder position
    running_ = true;
    ESP_LOGI(tag_, "Starting encoder monitoring task");
    
    BaseType_t task_ret = xTaskCreate(
        taskFunction,
        tag_,
        2048,         // Stack size
        this,         // Task parameter
        tskIDLE_PRIORITY + 1,  // Priority
        &task_handle_
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(tag_, "Failed to create encoder monitoring task");
        running_ = false;
        return;
    }
}

void Encoder::stop() {
    // Stop PCNT unit
    if (pcnt_unit_) {
        ESP_LOGI(tag_, "Stopping PCNT unit");
        pcnt_unit_stop(pcnt_unit_);
        pcnt_unit_disable(pcnt_unit_);
    }
    
    // Stop button monitoring task if running
    if (running_) {
        running_ = false;
        ESP_LOGI(tag_, "Stopping button monitoring task");
        
        // Give the task time to exit gracefully
        vTaskDelay(pdMS_TO_TICKS(100));
        
        if (task_handle_ != nullptr) {
            vTaskDelete(task_handle_);
            task_handle_ = nullptr;
        }
    }
}

void Encoder::reset() {
    if (pcnt_unit_) {
        pcnt_unit_clear_count(pcnt_unit_);
        position_ = 0;
        ESP_LOGI(tag_, "Encoder position reset to 0");
    }
}

int32_t Encoder::getPosition() {
    if (pcnt_unit_) {
        int value;
        if (pcnt_unit_get_count(pcnt_unit_, &value) == ESP_OK) {
            position_ = value;
        }
    }
    return position_;
}

void Encoder::setPosition(int32_t position) {
    if (pcnt_unit_) {
        pcnt_unit_clear_count(pcnt_unit_);
        // The PCNT unit doesn't allow directly setting a value,
        // so we need to track the offset in software
        position_ = position;
        ESP_LOGI(tag_, "Encoder position set to %ld", position);
    }
}

// Button functionality has been removed

void Encoder::taskFunction(void* arg) {
    Encoder* encoder = static_cast<Encoder*>(arg);
    encoder->pollTask();
}

void Encoder::pollTask() {
    ESP_LOGI(tag_, "Encoder monitoring task started");
    
    // This task is useful for diagnostics and debugging
    while (running_) {
        int hw_count = 0;
        esp_err_t err = pcnt_unit_get_count(pcnt_unit_, &hw_count);
        
        if (err == ESP_OK) {
            ESP_LOGI(tag_, "Encoder diagnostics: HW count=%d, SW position=%ld", 
                    hw_count, position_);
        } else {
            ESP_LOGW(tag_, "Failed to read encoder count: %s", esp_err_to_name(err));
        }
        
        // Sleep for the polling interval
        vTaskDelay(pdMS_TO_TICKS(config_.poll_interval_ms));
    }
    
    ESP_LOGI(tag_, "Encoder monitoring task ended");
    vTaskDelete(nullptr);
}

bool Encoder::pcntEventCallback(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx) {
    Encoder* encoder = static_cast<Encoder*>(user_ctx);
    
    if (!encoder || !edata) {
        ESP_LOGE("Encoder", "Invalid callback parameters");
        return false;
    }
    
    // Get current count value
    int current_count = 0;
    esp_err_t err = pcnt_unit_get_count(unit, &current_count);
    if (err != ESP_OK) {
        ESP_LOGE(encoder->tag_, "Failed to get count: %s", esp_err_to_name(err));
        return false;
    }
    
    // Log the event
    ESP_LOGI(encoder->tag_, "Watch event triggered: watch_point=%d, current_count=%d", 
             edata->watch_point_value, current_count);
    
    // Determine direction from the watch point value and current count
    const int watch_value = edata->watch_point_value;
    bool clockwise = (watch_value > 0);
    
    // Update position based on direction
    if (clockwise) {
        encoder->position_++;
        ESP_LOGI(encoder->tag_, "Clockwise rotation detected, new position: %ld", encoder->position_);
    } else {
        encoder->position_--;
        ESP_LOGI(encoder->tag_, "Counter-clockwise rotation detected, new position: %ld", encoder->position_);
    }
    
    // Reset the count to halfway between limits to avoid missing events
    if (current_count >= watch_value || current_count <= watch_value) {
        pcnt_unit_clear_count(unit);
        ESP_LOGD(encoder->tag_, "Counter cleared");
    }
    
    // Call user callback if registered
    if (encoder->callback_) {
        if (clockwise) {
            encoder->callback_(Event::CLOCKWISE, encoder->position_);
        } else {
            encoder->callback_(Event::COUNTER_CLOCKWISE, encoder->position_);
        }
    }
    
    return false;  // No high-priority task wakeup needed
}
