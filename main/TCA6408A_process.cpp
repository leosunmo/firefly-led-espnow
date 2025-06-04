#include "TCA6408A.h"
#include "esp_log.h"

esp_err_t TCA6408A::processPinChanges()
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Read the input register - this will clear the INT signal
    uint8_t newInputState;
    esp_err_t ret = readRegister(REG_INPUT, &newInputState);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read input register: %s", esp_err_to_name(ret));
        return ret;
    }

    // Check for changes
    uint8_t changedBits = newInputState ^ inputState_;
    
    if (changedBits != 0) {
        ESP_LOGD(TAG, "Input state changed: 0x%02x -> 0x%02x (changed: 0x%02x)",
                inputState_, newInputState, changedBits);
                
        // Update input state first
        inputState_ = newInputState;
        
        // Process all changed pins
        for (uint8_t pin = 0; pin < 8; pin++) {
            if (changedBits & (1 << pin)) {
                uint8_t level = (newInputState & (1 << pin)) ? 1 : 0;

                auto it = callbacks_.find(pin);
                if (it != callbacks_.end()) {
                    // Account for active low if configured
                    if (it->second.activeLow) {
                        level = !level;
                    }
                    
                    // Create a pin event and enqueue it
                    PinEvent event;
                    event.pin = pin;
                    event.level = level;
                    
                    // Use non-blocking send with short timeout - don't wait too long
                    BaseType_t result = xQueueSend(eventQueue_, &event, pdMS_TO_TICKS(10));
                    if (result != pdTRUE) {
                        ESP_LOGE(TAG, "Failed to enqueue pin event for pin %d, queue might be full", pin);
                    } else {
                        ESP_LOGD(TAG, "Enqueued event for pin %d, level %d", pin, level);
                    }
                }
            }
        }
    }
    
    return ESP_OK;
}
