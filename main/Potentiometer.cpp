#include "Potentiometer.h"

Potentiometer::Potentiometer(const Config& config) :
    config_(config),
    callback_(nullptr),
    adc_handle_(nullptr),
    task_handle_(nullptr),
    running_(false),
    last_raw_value_(0)
#ifdef ENABLE_ADC_CALIBRATION
    ,calibration_enabled_(false),
    cali_handle_(nullptr)
#endif
{
    esp_log_level_set(tag_, ESP_LOG_INFO);

    sniprintf(tag_, sizeof(tag_), "Pot_%s", config.name.c_str());
    ESP_LOGI(tag_, "Creating potentiometer on GPIO %d", config.gpio_num);
}

Potentiometer::~Potentiometer() {
    stop();
    
    if (adc_handle_ != nullptr) {
        adc_oneshot_del_unit(adc_handle_);
        adc_handle_ = nullptr;
        ESP_LOGI(tag_, "ADC resources freed");
    }
    
#ifdef ENABLE_ADC_CALIBRATION
    if (calibration_enabled_ && cali_handle_ != nullptr) {
        adc_cali_delete_scheme_curve_fitting(cali_handle_);
        cali_handle_ = nullptr;
        ESP_LOGI(tag_, "ADC calibration resources freed");
    }
#endif
}

bool Potentiometer::init() {
    ESP_LOGI(tag_, "Initializing potentiometer on GPIO %d (ADC Unit %d, Channel %d)",
             config_.gpio_num, config_.adc_unit, config_.adc_channel);

    // Setup oneshot ADC config
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = config_.adc_unit,
        .clk_src = soc_periph_adc_digi_clk_src_t::ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    // Initialize ADC unit
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to create ADC unit: %s", esp_err_to_name(ret));
        return false;
    }

    // Configure ADC channel
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = getESPAttenuation(),
        .bitwidth = ADC_BITWIDTH_12,
    };
    
    ret = adc_oneshot_config_channel(adc_handle_, config_.adc_channel, &channel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        return false;
    }
    
#ifdef ENABLE_ADC_CALIBRATION
    // Initialize ADC calibration
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = config_.adc_unit,
        .chan = config_.adc_channel,
        .atten = getESPAttenuation(),
        .bitwidth = ADC_BITWIDTH_12,
    };
    
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle_);
    if (ret == ESP_OK) {
        calibration_enabled_ = true;
        ESP_LOGI(tag_, "ADC calibration enabled successfully");
    } else {
        calibration_enabled_ = false;
        ESP_LOGW(tag_, "ADC calibration failed to initialize: %s", esp_err_to_name(ret));
        ESP_LOGW(tag_, "Continuing without calibration");
    }
#endif

    ESP_LOGI(tag_, "Potentiometer initialized successfully");
    return true;
}

void Potentiometer::registerCallback(Callback callback) {
    callback_ = callback;
}

uint32_t Potentiometer::getRawValue() {
    int adc_value = 0;
    
    if (adc_handle_ == nullptr) {
        ESP_LOGE(tag_, "ADC handle is null, not initialized");
        return 0;
    }
    
    esp_err_t ret = adc_oneshot_read(adc_handle_, config_.adc_channel, &adc_value);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_, "Failed to read ADC: %s", esp_err_to_name(ret));
        return 0;
    }
    
#ifdef ENABLE_ADC_CALIBRATION
    // Optionally log the voltage if calibration is enabled (for debug information only)
    if (calibration_enabled_ && cali_handle_ != nullptr) {
        int voltage_mv = 0;
        ret = adc_cali_raw_to_voltage(cali_handle_, adc_value, &voltage_mv);
        if (ret == ESP_OK) {
            ESP_LOGD(tag_, "ADC raw: %d -> Voltage: %d mV", adc_value, voltage_mv);
        }
    }
#endif
    
    return adc_value;
}

float Potentiometer::getPercentage() {
    uint32_t raw_value = getRawValue();
    
    // Use the class constant for consistent max value
    const float max_value = static_cast<float>(MAX_ADC_VALUE);
    
    // Simply calculate percentage without any scaling
    float percentage = (raw_value / max_value) * 100.0f;
    
    // Clamp to range 0-100
    if (percentage < 0.0f) percentage = 0.0f;
    if (percentage > 100.0f) percentage = 100.0f;
    
    // Log raw and percentage values for debugging
    ESP_LOGD(tag_, "Raw: %lu -> Percentage: %.2f%%", raw_value, percentage);
    
    
    return percentage;
}

void Potentiometer::start() {
    if (running_) {
        ESP_LOGW(tag_, "Monitoring task already running");
        return;
    }

    running_ = true;
       
    // Create a task to monitor the potentiometer value
    BaseType_t ret = xTaskCreate(
        taskFunction,
        config_.name.c_str(),
        3072,  // Increased from 2048 to prevent stack overflow
        this,
        tskIDLE_PRIORITY + 1,
        &task_handle_
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(tag_, "Failed to create monitoring task");
        running_ = false;
        return;
    }
    
    ESP_LOGI(tag_, "Monitoring task started");
}

void Potentiometer::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
        ESP_LOGI(tag_, "Monitoring task stopped");
    }
}

void Potentiometer::taskFunction(void* arg) {
    Potentiometer* pot = static_cast<Potentiometer*>(arg);
    pot->pollTask();
}

void Potentiometer::pollTask() {
    uint32_t prev_value = getRawValue();
    last_raw_value_ = prev_value;
    
    // Use direct thresholds without any scaling adjustments
    // Using the class constant for consistency
    uint32_t min_threshold = config_.change_threshold;
    uint32_t max_threshold = MAX_ADC_VALUE - config_.change_threshold;
    
    bool was_min = (prev_value < min_threshold);
    bool was_max = (prev_value > max_threshold);
    bool was_center = false;
    
    if (config_.enable_center_event) {
        const uint32_t center_value = MAX_ADC_VALUE / 2;
        was_center = (prev_value > (center_value - config_.center_threshold) && 
                      prev_value < (center_value + config_.center_threshold));
    }
    
    while (running_) {
        // Read the current value
        uint32_t current_value = getRawValue();
        last_raw_value_ = current_value;
        
        // Calculate percentage
        float percentage = getPercentage();

        // Check for significant change
        int32_t diff = abs(static_cast<int32_t>(current_value) - static_cast<int32_t>(prev_value));
        if (diff >= static_cast<int32_t>(config_.change_threshold)) {
            if (callback_) {
                callback_(Event::VALUE_CHANGED, current_value, percentage);
            }
            prev_value = current_value;
        }
        
        // Check for min/max/center position events using our adjusted thresholds
        bool is_min = (current_value < min_threshold);
        bool is_max = (current_value > max_threshold);
        
        if (is_min && !was_min) {
            if (callback_) {
                callback_(Event::MIN_REACHED, current_value, percentage);
            }
        }
        
        if (is_max && !was_max) {
            if (callback_) {
                callback_(Event::MAX_REACHED, current_value, percentage);
            }
        }
        
        was_min = is_min;
        was_max = is_max;
        
        // Handle center position if enabled
        if (config_.enable_center_event) {
            const uint32_t center_value = MAX_ADC_VALUE / 2;
            bool is_center = (current_value > (center_value - config_.center_threshold) && 
                             current_value < (center_value + config_.center_threshold));
                             
            if (is_center && !was_center) {
                if (callback_) {
                    callback_(Event::CENTER_REACHED, current_value, percentage);
                }
            }
            
            was_center = is_center;
        }
        
        // Delay before next reading
        vTaskDelay(pdMS_TO_TICKS(config_.poll_interval_ms));
    }
}

adc_atten_t Potentiometer::getESPAttenuation() {
    switch (config_.attenuation) {
        case Attenuation::DB_0:
            return ADC_ATTEN_DB_0;
        case Attenuation::DB_2_5:
            return ADC_ATTEN_DB_2_5;
        case Attenuation::DB_6:
            return ADC_ATTEN_DB_6;
        case Attenuation::DB_12:
            return ADC_ATTEN_DB_12;
        default:
            return ADC_ATTEN_DB_12; // Default to highest attenuation
    }
}
