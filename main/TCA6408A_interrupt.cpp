#include "TCA6408A.h"
#include "esp_log.h"

// IRAM_ATTR ensures the function is placed in RAM for fast execution
void IRAM_ATTR TCA6408A::isrHandler(void* arg)
{
    // This is the ISR that gets called when the INT pin changes
    // We need to keep this minimal and just notify the task
    
    // The arg parameter contains the TCA6408A instance pointer
    TCA6408A* instance = static_cast<TCA6408A*>(arg);
    
    // Check if we have a valid instance
    if (instance == nullptr) {
        return;
    }

    // Signal the interrupt task
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (instance->interruptTaskHandle_ != nullptr) {
        vTaskNotifyGiveFromISR(instance->interruptTaskHandle_, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

esp_err_t TCA6408A::setupInterrupt()
{
    if (config_.int_pin < 0) {
        ESP_LOGW(tag_.c_str(), "No interrupt pin configured");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(tag_.c_str(), "Setting up interrupt on GPIO %d", config_.int_pin);

    // Configure GPIO pin for interrupt
    gpio_config_t int_config = {};
    int_config.pin_bit_mask = (1ULL << config_.int_pin);
    int_config.mode = GPIO_MODE_INPUT;
    int_config.pull_up_en = GPIO_PULLUP_ENABLE;  // INT is open-drain, needs pull-up
    int_config.intr_type = GPIO_INTR_NEGEDGE;    // Active-low interrupt
    
    esp_err_t ret = gpio_config(&int_config);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_.c_str(), "Failed to configure interrupt pin: %s", esp_err_to_name(ret));
        return ret;
    }

    // Install the interrupt service
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means ISR service is already installed, which is fine
        ESP_LOGE(tag_.c_str(), "Failed to install ISR service: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add our handler for this specific GPIO
    // Pass the instance pointer (this) as the argument to the ISR handler
    ret = gpio_isr_handler_add(static_cast<gpio_num_t>(config_.int_pin), isrHandler, this);
    if (ret != ESP_OK) {
        ESP_LOGE(tag_.c_str(), "Failed to add ISR handler: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create the interrupt handling task
    interruptRunning_ = true;
    BaseType_t task_ret = xTaskCreate(
        interruptTask,
        "tca6408a_int",
        3072, // Stack size
        this,  // Parameter
        configMAX_PRIORITIES - 1,  // High priority for interrupt handling
        &interruptTaskHandle_);

    if (task_ret != pdPASS) {
        ESP_LOGE(tag_.c_str(), "Failed to create interrupt task");
        gpio_isr_handler_remove(static_cast<gpio_num_t>(config_.int_pin));
        interruptRunning_ = false;
        return ESP_FAIL;
    }

    // Mark as enabled
    interruptEnabled_ = true;
    ESP_LOGI(tag_.c_str(), "Interrupt handler successfully set up");
    
    // Read the input register once to clear any pending interrupt
    uint8_t dummy;
    ret = readRegister(REG_INPUT, &dummy);
    if (ret != ESP_OK) {
        ESP_LOGW(tag_.c_str(), "Failed to clear initial interrupt state: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

esp_err_t TCA6408A::cleanupInterrupt()
{
    if (!interruptEnabled_ || config_.int_pin < 0) {
        return ESP_OK;
    }

    // Stop the interrupt task
    interruptRunning_ = false;
    
    // Give the task time to exit
    vTaskDelay(pdMS_TO_TICKS(100));

    // Remove the ISR handler
    gpio_isr_handler_remove(static_cast<gpio_num_t>(config_.int_pin));

    // Delete the task if it hasn't exited
    if (interruptTaskHandle_ != nullptr) {
        vTaskDelete(interruptTaskHandle_);
        interruptTaskHandle_ = nullptr;
    }

    interruptEnabled_ = false;
    ESP_LOGI(tag_.c_str(), "Interrupt handler cleaned up");
    return ESP_OK;
}

void TCA6408A::interruptTask(void* arg)
{
    TCA6408A* self = static_cast<TCA6408A*>(arg);
    ESP_LOGI(self->tag_.c_str(), "Interrupt task started");

    while (self->interruptRunning_) {
        // Wait for notification from ISR - max 500ms timeout to allow clean exit
        uint32_t notification = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        
        if (notification > 0) {
            // We got a notification from the ISR
            ESP_LOGD(self->tag_.c_str(), "Received interrupt notification");
            
            // Process the pin changes - this will read the input register and clear the INT pin
            self->processPinChanges();
        }
    }

    ESP_LOGI(self->tag_.c_str(), "Interrupt task exiting");
    vTaskDelete(NULL);
}
