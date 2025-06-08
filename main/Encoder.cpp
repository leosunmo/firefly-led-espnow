#include "Encoder.h"
#include <cstring>

// Static task function for processing encoder events
void Encoder::taskFunction(void* arg) {
    Encoder* encoder = static_cast<Encoder*>(arg);
    ESP_LOGI(encoder->tag_, "Encoder event processing task started");
    
    while (encoder->running_) {
        // Wait for notification from the ISR callback with a timeout to allow clean exit
        uint32_t notification = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        
        if (notification > 0) {
            // We received a notification - process the event
            encoder->processEvents();
        }
        // No else clause needed - if timeout occurs, we just loop and check running_ again
    }
    
    ESP_LOGI(encoder->tag_, "Encoder event processing task ended");
    vTaskDelete(NULL);
}

Encoder::Encoder(const Config& config)
    : config_(config), 
      task_handle_(nullptr),
      running_(false),
      last_event_(Event::CLOCKWISE),
      last_event_time_(0),       // Will be properly initialized in start()
      debounce_ticks_(pdMS_TO_TICKS(config.debounce_ms)), // Cache the debounce threshold in ticks
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
    esp_log_level_set(tag_, ESP_LOG_DEBUG);
    ESP_LOGI(tag_, "Initializing encoder with PCNT");

    // Set up PCNT unit with tighter limits for more reliable step detection
    pcnt_unit_config_t unit_config = {
        .low_limit = -2,  // Tighter limit for more reliable triggering
        .high_limit = 2,  // Tighter limit for more reliable triggering
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
                                      PCNT_CHANNEL_EDGE_ACTION_DECREASE,   // No count on negative edge
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
                                      PCNT_CHANNEL_EDGE_ACTION_INCREASE,   // No count on negative edge
                                      PCNT_CHANNEL_EDGE_ACTION_DECREASE); // Count up on positive edge
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
    ret = pcnt_unit_add_watch_point(pcnt_unit_, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to add positive watch point: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = pcnt_unit_add_watch_point(pcnt_unit_, -1);
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
    
    // Initialize position
    position_ = 0;

    ESP_LOGI(tag_, "Encoder initialized successfully with PCNT (debounce: %lu ms)", config_.debounce_ms);
    
    return true;
}

void Encoder::registerCallback(Callback callback) {
    callback_ = callback;
}

void Encoder::start() {
    if (running_) {
        ESP_LOGW(tag_, "Encoder already running");
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
    
    // Create task to process events
    running_ = true;
    
    // Initialize timestamp to current time to ensure proper debouncing from the start
    last_event_time_ = xTaskGetTickCount();
    
    ESP_LOGI(tag_, "Encoder monitoring started");
    
    // Create a task to process events from the interrupt handler
    BaseType_t task_ret = xTaskCreate(
        taskFunction,
        tag_,
        3072,       // Stack size (increased to prevent stack overflow)
        this,       // Task parameter
        1,          // Priority
        &task_handle_
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(tag_, "Failed to create encoder event processing task");
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
    
    // Mark encoder as not running and stop task
    if (running_) {
        running_ = false;
        
        // Give the task time to exit gracefully
        if (task_handle_ != nullptr) {
            ESP_LOGI(tag_, "Waiting for task to terminate");
            vTaskDelay(pdMS_TO_TICKS(100));
            vTaskDelete(task_handle_);
            task_handle_ = nullptr;
        }
        
        ESP_LOGI(tag_, "Encoder monitoring stopped");
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
    // Simply return the software-tracked position
    // We don't need to read from the hardware counter as that gets reset
    // after each event trigger, and our position_ is tracking the full count
    return position_;
}

void Encoder::setPosition(int32_t position) {
    // Set the software position directly
    position_ = position;
    ESP_LOGI(tag_, "Encoder position set to %ld", position);
}

bool Encoder::processEvents() {
    // Simply process the event that was stored by the ISR
    // No need to check event_pending_ since we were woken by a notification
    
    // Get the current event that triggered the notification
    Event event = last_event_;
    
    // Log the event (safe to do here since we're in task context)
    ESP_LOGI(tag_, "%s rotation detected, position: %ld",
            (event == Event::CLOCKWISE) ? "Clockwise" : "Counter-clockwise", 
            position_);
    
    // Call user callback if registered
    if (callback_) {
        callback_(event, position_);
    }
    
    return true;
}

bool Encoder::pcntEventCallback(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx) {
    // This function runs in an interrupt context - keep it minimal!
    Encoder* encoder = static_cast<Encoder*>(user_ctx);
    
    if (!encoder || !edata) {
        return false;
    }
    
    // Skip processing if not running (safer)
    if (!encoder->running_) {
        return false;
    }
    
    // Get current time for debounce check
    TickType_t current_time = xTaskGetTickCountFromISR();
    
    // Check if we should debounce this event
    if (encoder->config_.debounce_ms > 0) {
        // Calculate elapsed time using our helper function
        TickType_t elapsed_time = calcElapsedTime(encoder->last_event_time_, current_time);
        
        // Skip this event if it occurred too soon after the last one
        if (elapsed_time < encoder->debounce_ticks_) {
            // Debounce in effect, ignore this event
            return false;
        }
    }
    
    // IMPORTANT: Update the last event time FIRST, to ensure accurate debouncing
    // The timestamp must be updated before any event processing
    encoder->last_event_time_ = current_time;

    // Determine direction from the watch point value (positive = clockwise)
    const int watch_value = edata->watch_point_value;
    bool clockwise = (watch_value > 0);
    
    // Update position based on direction
    if (clockwise) {
        encoder->position_++;
    } else {
        encoder->position_--;
    }
    
    // Reset the count to avoid missing events
    pcnt_unit_clear_count(unit);
    
    // Store the event for later processing
    encoder->last_event_ = clockwise ? Event::CLOCKWISE : Event::COUNTER_CLOCKWISE;
    
    // Notify the task that an event has occurred
    if (encoder->task_handle_ != nullptr) {
        // Send task notification from ISR - limit how often we notify to prevent overflows
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(encoder->task_handle_, &xHigherPriorityTaskWoken);
        
        // If a higher priority task was woken, request a context switch
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
    
    return true;  // Indicate that high priority processing is needed
}

TickType_t Encoder::calcElapsedTime(TickType_t start, TickType_t end) {
    // Handle tick count overflow (rare but possible)
    if (end >= start) {
        return end - start;
    } else {
        // Handle timer overflow by calculating the "wrap-around" distance
        return (portMAX_DELAY - start) + end + 1;
    }
}
