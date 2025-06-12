#include "I2CPotentiometer.h"
#include <stdio.h>

I2CPotentiometer::I2CPotentiometer(const Config& config) :
    config_(config),
    callback_(nullptr),
    task_handle_(nullptr),
    running_(false),
    last_raw_value_(0),
    last_reported_value_(0),
    cumulative_change_(0)
{
    snprintf(tag_, sizeof(tag_), "I2CPot_%s", config.name.c_str());
    ESP_LOGI(tag_, "Creating I2C potentiometer on ADS1015 channel %d", static_cast<int>(config.channel));
}

I2CPotentiometer::~I2CPotentiometer()
{
    stop();
    ESP_LOGI(tag_, "I2C potentiometer destroyed");
}

bool I2CPotentiometer::init()
{
    ESP_LOGI(tag_, "Initializing I2C potentiometer on channel %d",
             static_cast<int>(config_.channel));
    
    if (config_.adc == nullptr)
    {
        ESP_LOGE(tag_, "ADS1015 instance is not provided");
        return false;
    }
    
    // Set the gain for this channel
    esp_err_t ret = config_.adc->setGain(config_.gain);
    if (ret != ESP_OK)
    {
        ESP_LOGE(tag_, "Failed to set gain: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(tag_, "I2C potentiometer initialized successfully");
    return true;
}

void I2CPotentiometer::registerCallback(Callback callback)
{
    callback_ = callback;
}

uint32_t I2CPotentiometer::getRawValue()
{
    uint16_t adc_value = 0;
    
    if (config_.adc == nullptr)
    {
        ESP_LOGE(tag_, "ADS1015 handle is null, not initialized");
        return 0;
    }
    
    esp_err_t ret = config_.adc->readRaw(config_.channel, &adc_value);
    if (ret != ESP_OK)
    {
        ESP_LOGE(tag_, "Failed to read ADC: %s", esp_err_to_name(ret));
        return 0;
    }
    
    // Log raw value for debugging
    ESP_LOGD(tag_, "ADC raw: %d", adc_value);
    
    return adc_value;
}

float I2CPotentiometer::getPercentage()
{
    uint32_t raw_value = getRawValue();
    
    // Calculate percentage based on ADS1015 max value (4095)
    const float max_value = static_cast<float>(MAX_ADC_VALUE);
    
    float percentage = (raw_value / max_value) * 100.0f;
    
    // Clamp to range 0-100
    if (percentage < 0.0f) percentage = 0.0f;
    if (percentage > 100.0f) percentage = 100.0f;
    
    // Log raw and percentage values for debugging
    ESP_LOGD(tag_, "Raw: %lu -> Percentage: %.2f%%", raw_value, percentage);
    
    return percentage;
}

void I2CPotentiometer::start()
{
    if (running_)
    {
        ESP_LOGW(tag_, "Monitoring task already running");
        return;
    }

    running_ = true;
       
    // Create a task to monitor the potentiometer value
    BaseType_t ret = xTaskCreate(
        taskFunction,
        config_.name.c_str(),
        3072,  // Stack size
        this,  // Parameter
        tskIDLE_PRIORITY + 1,
        &task_handle_
    );
    
    if (ret != pdPASS)
    {
        ESP_LOGE(tag_, "Failed to create monitoring task");
        running_ = false;
        return;
    }
    
    ESP_LOGI(tag_, "Monitoring task started");
}

void I2CPotentiometer::stop()
{
    if (!running_)
    {
        return;
    }
    
    running_ = false;
    
    if (task_handle_ != nullptr)
    {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
        ESP_LOGI(tag_, "Monitoring task stopped");
    }
}

void I2CPotentiometer::taskFunction(void* arg)
{
    I2CPotentiometer* pot = static_cast<I2CPotentiometer*>(arg);
    pot->pollTask();
}

void I2CPotentiometer::pollTask()
{
    uint32_t prev_value = getRawValue();
    last_raw_value_ = prev_value;
    last_reported_value_ = prev_value;
    cumulative_change_ = 0;
    
    // Use direct thresholds without any scaling adjustments
    uint32_t min_threshold = config_.change_threshold;
    uint32_t max_threshold = MAX_ADC_VALUE - config_.change_threshold;
    
    bool was_min = (prev_value < min_threshold);
    bool was_max = (prev_value > max_threshold);
    bool was_center = false;
    
    if (config_.enable_center_event)
    {
        const uint32_t center_value = MAX_ADC_VALUE / 2;
        was_center = (prev_value > (center_value - config_.center_threshold) && 
                      prev_value < (center_value + config_.center_threshold));
    }
    
    while (running_)
    {
        // Read the current value
        uint32_t current_value = getRawValue();
        last_raw_value_ = current_value;
        
        // Calculate percentage
        float percentage = getPercentage();

        bool should_report = false;
        
        if (config_.use_cumulative_tracking)
        {
            // Track cumulative changes since last reported value
            int32_t change = static_cast<int32_t>(current_value) - static_cast<int32_t>(prev_value);
            cumulative_change_ += change;
            
            // Check if the absolute cumulative change exceeds threshold
            if (abs(cumulative_change_) >= static_cast<int32_t>(config_.change_threshold))
            {
                should_report = true;
                // Reset cumulative change after reporting
                cumulative_change_ = 0;
                last_reported_value_ = current_value;
            }
        }
        else
        {
            // Original behavior - check for significant instantaneous change
            int32_t diff = abs(static_cast<int32_t>(current_value) - static_cast<int32_t>(prev_value));
            if (diff >= static_cast<int32_t>(config_.change_threshold))
            {
                should_report = true;
            }
        }
        
        // Report value change if needed
        if (should_report && callback_)
        {
            callback_(Event::VALUE_CHANGED, current_value, percentage);
        }
        
        // Always update the previous value for next comparison
        prev_value = current_value;
        
        // Check for min/max position events
        bool is_min = (current_value < min_threshold);
        bool is_max = (current_value > max_threshold);
        
        if (is_min && !was_min)
        {
            if (callback_)
            {
                callback_(Event::MIN_REACHED, current_value, percentage);
            }
        }
        
        if (is_max && !was_max)
        {
            if (callback_)
            {
                callback_(Event::MAX_REACHED, current_value, percentage);
            }
        }
        
        was_min = is_min;
        was_max = is_max;
        
        // Handle center position if enabled
        if (config_.enable_center_event)
        {
            const uint32_t center_value = MAX_ADC_VALUE / 2;
            bool is_center = (current_value > (center_value - config_.center_threshold) && 
                             current_value < (center_value + config_.center_threshold));
                             
            if (is_center && !was_center)
            {
                if (callback_)
                {
                    callback_(Event::CENTER_REACHED, current_value, percentage);
                }
            }
            
            was_center = is_center;
        }
        
        // Delay before next reading
        vTaskDelay(pdMS_TO_TICKS(config_.poll_interval_ms));
    }
}
