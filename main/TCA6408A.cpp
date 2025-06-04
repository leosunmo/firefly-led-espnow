#include "TCA6408A.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <algorithm>
#include <vector>  // For std::vector in callback handling

// Initialize static instance pointer for ISR
TCA6408A* TCA6408A::isrInstance_ = nullptr;

TCA6408A::TCA6408A(const Config &config) : config_(config)
{
    ESP_LOGI(TAG, "Creating TCA6408A instance with I2C address 0x%02x", config.i2c_address);
    i2c_device_ = nullptr;
    
    // Create the event queue (holds up to 32 pin events)
    eventQueue_ = xQueueCreate(32, sizeof(PinEvent));
    if (eventQueue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create event queue");
    } else {
        ESP_LOGI(TAG, "Event queue created successfully");
    }
    
    // Check if interrupt pin is configured
    if (config_.int_pin >= 0) {
        ESP_LOGI(TAG, "Interrupt pin configured: GPIO %d", config_.int_pin);
    } else {
        ESP_LOGI(TAG, "No interrupt pin configured, using polling mode");
    }
}

TCA6408A::~TCA6408A()
{
    stopMonitoring();

    // Deinitialize I2C device handle
    if (i2c_device_ != nullptr)
    {
        i2c_master_bus_rm_device(i2c_device_);
        i2c_device_ = nullptr;
    }

    // Delete I2C bus if we created it
    if (i2c_bus_ != nullptr)
    {
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
    }
    
    // Delete the event queue
    if (eventQueue_ != nullptr)
    {
        vQueueDelete(eventQueue_);
        eventQueue_ = nullptr;
    }
}

esp_err_t TCA6408A::init()
{
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "Initializing TCA6408A on SDA:%d, SCL:%d",
             config_.sda_pin, config_.scl_pin);

    // Configure I2C bus
    i2c_master_bus_config_t bus_config = {
        // .i2c_port = I2C_NUM_0,
        .i2c_port = -1, // Automatically select the first available I2C port
        .sda_io_num = static_cast<gpio_num_t>(config_.sda_pin),
        .scl_io_num = static_cast<gpio_num_t>(config_.scl_pin),
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };

    // Set internal pullup flag separately
    // Don't use internal pullups, the chip has them built-in
    bus_config.flags.enable_internal_pullup = false;

    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure I2C device - TCA6408A works best at 100kHz (standard mode)
    // Limit speed to 100kHz for maximum reliability
    config_.i2c_freq_hz = (config_.i2c_freq_hz > 100000) ? 100000 : config_.i2c_freq_hz;
    
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config_.i2c_address,
        .scl_speed_hz = config_.i2c_freq_hz,
    };
    
    ESP_LOGI(TAG, "Configuring I2C device with address 0x%02x at %lu Hz", 
             config_.i2c_address, config_.i2c_freq_hz);

    ret = i2c_master_bus_add_device(i2c_bus_, &device_config, &i2c_device_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
        return ret;
    }

    // Add a retry mechanism to verify device presence
    ESP_LOGI(TAG, "Checking if TCA6408A is present by reading input register...");

    const int max_retries = 5;
    int retry_count = 0;
    bool device_found = false;

    while (retry_count < max_retries && !device_found)
    {
        ret = readRegister(REG_INPUT, &inputState_);
        if (ret == ESP_OK)
        {
            device_found = true;
            ESP_LOGI(TAG, "TCA6408A device found after %d attempts", retry_count + 1);
            break;
        }

        ESP_LOGW(TAG, "Attempt %d: Failed to read from TCA6408A: %s",
                 retry_count + 1, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(100)); // Wait 100ms between retries
        retry_count++;
    }

    if (!device_found)
    {
        ESP_LOGE(TAG, "TCA6408A device not found after %d attempts", max_retries);
        i2c_master_bus_rm_device(i2c_device_);
        i2c_device_ = nullptr;
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
        return ESP_ERR_NOT_FOUND;
    }

    // Read output register state
    ret = readRegister(REG_OUTPUT, &outputState_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read output register: %s", esp_err_to_name(ret));
        return ret;
    }

    // Read config register state
    ret = readRegister(REG_CONFIG, &configState_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read config register: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TCA6408A initialized successfully");
    ESP_LOGI(TAG, "  Input state:  0x%02x", inputState_);
    ESP_LOGI(TAG, "  Output state: 0x%02x", outputState_);
    ESP_LOGI(TAG, "  Config state: 0x%02x", configState_);

    return ESP_OK;
}

esp_err_t TCA6408A::configurePin(uint8_t pin, bool isOutput)
{
    if (!isValidPin(pin))
    {
        ESP_LOGE(TAG, "Invalid pin number: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // In TCA6408A: 1 = input, 0 = output in the config register
    uint8_t newConfig = configState_;

    if (isOutput)
    {
        newConfig &= ~(1 << pin); // Clear bit for output
    }
    else
    {
        newConfig |= (1 << pin); // Set bit for input
    }

    if (newConfig != configState_)
    {
        ESP_LOGD(TAG, "Configuring pin %d as %s", pin, isOutput ? "output" : "input");
        ESP_LOGD(TAG, "New config: 0x%02x", newConfig);

        esp_err_t ret = writeRegister(REG_CONFIG, newConfig);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to write config register: %s", esp_err_to_name(ret));
            return ret;
        }

        configState_ = newConfig;
    }

    return ESP_OK;
}

esp_err_t TCA6408A::readPin(uint8_t pin, uint8_t *level)
{
    if (!isValidPin(pin) || level == nullptr)
    {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // For output pins, read the output state register
    // For input pins, read the input state register
    bool isInput = (configState_ & (1 << pin)) != 0;

    uint8_t state;
    esp_err_t ret;

    if (isInput)
    {
        ret = readRegister(REG_INPUT, &state);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to read input register: %s", esp_err_to_name(ret));
            return ret;
        }
        inputState_ = state;
    }
    else
    {
        // For output pins, we could read the actual output register or use cached value
        state = outputState_;
    }

    *level = (state & (1 << pin)) ? 1 : 0;
    return ESP_OK;
}

esp_err_t TCA6408A::writePin(uint8_t pin, uint8_t level)
{
    if (!isValidPin(pin))
    {
        ESP_LOGE(TAG, "Invalid pin number: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Check if the pin is configured as output
    if ((configState_ & (1 << pin)) != 0)
    {
        ESP_LOGE(TAG, "Cannot write to input pin %d", pin);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t newOutput = outputState_;

    if (level)
    {
        newOutput |= (1 << pin); // Set bit
    }
    else
    {
        newOutput &= ~(1 << pin); // Clear bit
    }

    if (newOutput != outputState_)
    {
        ESP_LOGD(TAG, "Setting pin %d to %d", pin, level);

        esp_err_t ret = writeRegister(REG_OUTPUT, newOutput);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to write output register: %s", esp_err_to_name(ret));
            return ret;
        }

        outputState_ = newOutput;
    }

    return ESP_OK;
}

esp_err_t TCA6408A::registerCallback(uint8_t pin, PinChangeCallback callback, bool activeLow)
{
    if (!isValidPin(pin) || !callback)
    {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Store the callback
    callbacks_[pin] = {callback, activeLow};

    // Ensure the pin is configured as input
    if ((configState_ & (1 << pin)) == 0)
    {
        ESP_LOGW(TAG, "Pin %d is not configured as input, changing configuration", pin);

        uint8_t newConfig = configState_ | (1 << pin);
        esp_err_t ret = writeRegister(REG_CONFIG, newConfig);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to write config register: %s", esp_err_to_name(ret));
            return ret;
        }

        configState_ = newConfig;
    }

    return ESP_OK;
}

esp_err_t TCA6408A::startMonitoring()
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if the queue was successfully created
    if (eventQueue_ == nullptr)
    {
        ESP_LOGE(TAG, "Event queue not initialized, cannot start monitoring");
        return ESP_ERR_INVALID_STATE;
    }

    // Make sure we're not already running
    if (pollTaskHandle_ != nullptr || callbackTaskHandle_ != nullptr || interruptTaskHandle_ != nullptr)
    {
        ESP_LOGW(TAG, "Monitoring is already running");
        return ESP_OK;
    }

    // First, create the callback task with a larger stack size
    callbackRunning_ = true;
    BaseType_t cb_ret = xTaskCreate(
        callbackTask,
        "tca6408a_cb",
        4096, // Larger stack size for callbacks
        this, // Parameter
        5,    // Priority
        &callbackTaskHandle_);

    if (cb_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create callback task");
        callbackRunning_ = false;
        return ESP_FAIL;
    }

    // Determine whether to use interrupt or polling mode
    if (config_.int_pin >= 0) {
        // Setup interrupt-based monitoring
        esp_err_t ret = setupInterrupt();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to setup interrupt monitoring, falling back to polling");
            
            // Fall back to polling mode if interrupt setup fails
            config_.int_pin = -1;
        } else {
            ESP_LOGI(TAG, "Started interrupt-based monitoring on GPIO %d", config_.int_pin);
            return ESP_OK;
        }
    }

    // If we got here, either no interrupt pin was specified or setup failed
    // Create polling task as fallback
    pollRunning_ = true;
    BaseType_t poll_ret = xTaskCreate(
        pollTask,
        "tca6408a_poll",
        3072, // Increased stack size for poll task
        this, // Parameter
        5,    // Priority
        &pollTaskHandle_);

    if (poll_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create polling task");
        pollRunning_ = false;
        
        // Clean up the callback task since poll task creation failed
        callbackRunning_ = false;
        vTaskDelay(pdMS_TO_TICKS(100));
        if (callbackTaskHandle_ != nullptr)
        {
            vTaskDelete(callbackTaskHandle_);
            callbackTaskHandle_ = nullptr;
        }
        
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Started polling-based monitoring with period %lu ms", config_.poll_period_ms);
    return ESP_OK;
}

esp_err_t TCA6408A::stopMonitoring()
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if all tasks are not running
    if (pollTaskHandle_ == nullptr && callbackTaskHandle_ == nullptr && interruptTaskHandle_ == nullptr)
    {
        return ESP_OK;
    }

    // Stop all tasks
    pollRunning_ = false;
    callbackRunning_ = false;
    interruptRunning_ = false;

    // Clean up the interrupt handler
    cleanupInterrupt();

    // Empty the event queue to prevent blocking on shutdown
    if (eventQueue_ != nullptr)
    {
        xQueueReset(eventQueue_);
    }

    // Give the tasks time to exit
    vTaskDelay(pdMS_TO_TICKS(100));

    // Delete the tasks if they haven't exited
    if (pollTaskHandle_ != nullptr)
    {
        vTaskDelete(pollTaskHandle_);
        pollTaskHandle_ = nullptr;
    }
    
    if (callbackTaskHandle_ != nullptr)
    {
        vTaskDelete(callbackTaskHandle_);
        callbackTaskHandle_ = nullptr;
    }
    
    if (interruptTaskHandle_ != nullptr)
    {
        vTaskDelete(interruptTaskHandle_);
        interruptTaskHandle_ = nullptr;
    }

    ESP_LOGI(TAG, "Stopped monitoring TCA6408A inputs");
    return ESP_OK;
}

esp_err_t TCA6408A::readPort(uint8_t *portValue)
{
    if (portValue == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    esp_err_t ret = readRegister(REG_INPUT, portValue);
    if (ret == ESP_OK)
    {
        inputState_ = *portValue;
    }
    return ret;
}

esp_err_t TCA6408A::writePort(uint8_t portValue)
{
    std::lock_guard<std::mutex> lock(mutex_);
    esp_err_t ret = writeRegister(REG_OUTPUT, portValue);
    if (ret == ESP_OK)
    {
        outputState_ = portValue;
    }
    return ret;
}

esp_err_t TCA6408A::readRegister(uint8_t reg, uint8_t *data)
{
    if (data == nullptr || i2c_device_ == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Set timeout for the operation - default 100ms if not specified
    uint32_t timeout_ms = config_.timeout_ms > 0 ? config_.timeout_ms : 100;

    // Simple read operation without retries, as the I2C driver has internal handling
    esp_err_t ret = i2c_master_transmit_receive(
        i2c_device_,     // Device handle
        &reg,            // Write buffer (register address)
        1,               // Write length
        data,            // Read buffer
        1,               // Read length
        timeout_ms       // Timeout in milliseconds
    );

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to read register 0x%02x: %s", 
                 reg, esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t TCA6408A::writeRegister(uint8_t reg, uint8_t data)
{
    if (i2c_device_ == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Set timeout for the operation
    uint32_t timeout_ms = config_.timeout_ms > 0 ? config_.timeout_ms : 100; // Default to 100ms

    // Prepare write buffer with register address and data
    uint8_t write_buffer[2] = {reg, data};

    // Write to the device
    esp_err_t ret = i2c_master_transmit(
        i2c_device_,          // Device handle
        write_buffer,         // Write buffer
        sizeof(write_buffer), // Write length
        timeout_ms            // Timeout in milliseconds
    );

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to write register 0x%02x: %s", reg, esp_err_to_name(ret));
    }

    return ret;
}

void TCA6408A::pollTask(void *arg)
{
    TCA6408A *self = static_cast<TCA6408A *>(arg);
    ESP_LOGI(self->TAG, "Polling task started");

    while (self->pollRunning_)
    {
        {
            std::lock_guard<std::mutex> lock(self->mutex_);
            esp_err_t result = self->processPinChanges();
            
            if (result != ESP_OK) {
                ESP_LOGW(self->TAG, "Poll failed, will retry on next cycle");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(self->config_.poll_period_ms));
    }

    ESP_LOGI(self->TAG, "Polling task exiting");

    // Clear task handle when exiting
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->pollTaskHandle_ = nullptr;
    }

    vTaskDelete(NULL);
}

void TCA6408A::callbackTask(void *arg)
{
    TCA6408A *self = static_cast<TCA6408A *>(arg);
    ESP_LOGI(self->TAG, "Callback task started");
    
    PinEvent event;
    
    while (self->callbackRunning_)
    {
        // Wait for pin events with timeout - allows for clean task termination
        if (xQueueReceive(self->eventQueue_, &event, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            // Get the callback - need the mutex to safely access callbacks_
            PinChangeCallback callback = nullptr;
            {
                std::lock_guard<std::mutex> lock(self->mutex_);
                auto it = self->callbacks_.find(event.pin);
                if (it != self->callbacks_.end()) {
                    callback = it->second.callback;
                }
            }
            
            // Call the callback outside the mutex lock
            if (callback)
            {
                ESP_LOGD(self->TAG, "Executing callback for pin %d, level %d", event.pin, event.level);
                callback(event.pin, event.level);
            }
        }
    }
    
    ESP_LOGI(self->TAG, "Callback task exiting");
    
    // Clear task handle when exiting
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->callbackTaskHandle_ = nullptr;
    }
    
    vTaskDelete(NULL);
}
