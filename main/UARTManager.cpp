#include "UARTManager.h"

const char* UARTManager::TAG = "UARTManager";

UARTManager& UARTManager::getInstance() {
    static UARTManager instance;
    return instance;
}

esp_err_t UARTManager::init() {
    esp_log_level_set(TAG, UART_LOG_LEVEL);
    ESP_LOGI(TAG, "Initializing UART Manager on UART%d (RX=%d, TX=%d)", UART_NUM, RX_PIN, TX_PIN);
    
    // Make sure we're not using UART0, which is typically used for logging
    if (UART_NUM == UART_NUM_0) {
        ESP_LOGW(TAG, "Warning: UART0 is typically used for console output. "
                      "This may cause conflicts with logging messages.");
    }
    
    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // Install UART driver
    esp_err_t err = uart_driver_install(UART_NUM, UART_BUFFER_SIZE * 2, UART_BUFFER_SIZE * 2, 
                                        UART_QUEUE_SIZE, &uartQueue, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver installation failed: %s", esp_err_to_name(err));
        return err;
    }
    
    // Configure UART parameters
    err = uart_param_config(UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART parameter configuration failed: %s", esp_err_to_name(err));
        return err;
    }

    // Set UART pins
    err = uart_set_pin(UART_NUM, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART pin configuration failed: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "UART initialized successfully");
    
    // Start the receive task
    startReceiveTask();
    
    return ESP_OK;
}

void UARTManager::sendMessage(CommandType cmd, uint32_t value) {
    // Create a properly formatted UARTMessage structure
    UARTMessage msg;
    msg.start = MESSAGE_START;
    msg.cmdType = cmd;
    msg.value = value;
    msg.end = MESSAGE_END;
    
    ESP_LOGI(TAG, "Sending UART message: cmd=0x%02X, value=0x%08lX", 
             static_cast<uint8_t>(cmd), static_cast<unsigned long>(value));
    
    // Send the message as a single block - now that baud rate is fixed
    int bytes_written = uart_write_bytes(UART_NUM, (const char*)&msg, sizeof(UARTMessage));
    if (bytes_written < 0) {
        ESP_LOGE(TAG, "Failed to send UART message");
        return;
    }
    
    if (bytes_written != sizeof(UARTMessage)) {
        ESP_LOGW(TAG, "Incomplete UART message sent: %d of %zu bytes", 
                 bytes_written, sizeof(UARTMessage));
    }
    
    // Make sure all bytes are transmitted
    uart_wait_tx_done(UART_NUM, 100 / portTICK_PERIOD_MS);  // 100ms timeout
    
    ESP_LOGI(TAG, "Sent UART message: cmd=0x%02x, value=0x%08lx", 
             static_cast<uint8_t>(cmd), static_cast<unsigned long>(value));
}

void UARTManager::sendBrightnessCommand(uint8_t brightness) {
    sendMessage(CommandType::BRIGHTNESS, brightness);
}

void UARTManager::sendPatternCommand(PatternType pattern) {
    uint32_t patternValue = static_cast<uint32_t>(pattern);
    sendMessage(CommandType::PATTERN, patternValue);
}

void UARTManager::sendSpeedCommand(uint8_t speed) {
    sendMessage(CommandType::SPEED, speed);
}

void UARTManager::sendDebugMessage(uint32_t value) {
    sendMessage(CommandType::DEBUG, value);
}

void UARTManager::sendHueCommand(uint8_t index, uint16_t hue) {
    // Pack the index and hue value into a single uint32_t
    // index in the most significant byte, hue in the lower 16 bits
    uint32_t packedValue = (static_cast<uint32_t>(index) << 16) | static_cast<uint32_t>(hue);
    sendMessage(CommandType::HUE, packedValue);
    ESP_LOGI(TAG, "Sent hue command: index=%d, hue=%d°", index, hue);
}

void UARTManager::sendPunchCommand(uint8_t intensity) {
    // Use the existing sendMessage function to send the punch effect intensity
    sendMessage(CommandType::PUNCH, static_cast<uint32_t>(intensity));
}

void UARTManager::processESPNOWMessage(const Message* message) {
    if (!message) {
        ESP_LOGE(TAG, "Received null message");
        return;
    }
    
    // Process the message based on its payload type
    switch (message->payload_type) {
        case PayloadType::ChangePattern: {
            const ChangePatternPayload& payload = std::get<ChangePatternPayload>(message->parsed_payload);
            PatternType patternType = payload.patternType;
            ESP_LOGI(TAG, "Forwarding pattern change to RP2040: pattern=%d", static_cast<int>(patternType));
            sendPatternCommand(patternType);
            break;
        }
        case PayloadType::ChangeBrightness: {
            const ChangeBrightnessPayload& payload = std::get<ChangeBrightnessPayload>(message->parsed_payload);
            uint8_t brightness = payload.brightnessLevel;
            ESP_LOGI(TAG, "Forwarding brightness change to RP2040: brightness=%d%%", brightness);
            sendBrightnessCommand(brightness);
            break;
        }
        case PayloadType::ChangeHue: {
            const ChangeHuePayload& payload = std::get<ChangeHuePayload>(message->parsed_payload);
            uint8_t index = payload.index;
            uint16_t hue = payload.hueVal;
            ESP_LOGI(TAG, "Forwarding hue change to RP2040: index=%d, hue=%d°", index, hue);
            sendHueCommand(index, hue);
            break;
        }
        case PayloadType::EffectPunch: {
            const EffectPunchPayload& payload = std::get<EffectPunchPayload>(message->parsed_payload);
            uint8_t intensity = payload.intensity;
            ESP_LOGI(TAG, "Forwarding punch effect to RP2040: intensity=%d%%", intensity);
            sendPunchCommand(intensity);
            break;
        }
        case PayloadType::ChangeSpeed: {
            const ChangeSpeedPayload& payload = std::get<ChangeSpeedPayload>(message->parsed_payload);
            uint8_t speed = payload.speedLevel;
            ESP_LOGI(TAG, "Forwarding speed change to RP2040: speed=%d%%", speed);
            sendSpeedCommand(speed);
            break;
        }
        case PayloadType::RegisterRequest:
        case PayloadType::RegistrationSuccessful:
        case PayloadType::Keepalive:
            // These messages don't need to be forwarded to the RP2040
            ESP_LOGD(TAG, "Ignoring protocol message type %d", static_cast<int>(message->payload_type));
            break;
        default:
            ESP_LOGW(TAG, "Unknown payload type: %d", static_cast<int>(message->payload_type));
            break;
    }
}

void UARTManager::startReceiveTask() {
    // Create a task to handle UART reception
    xTaskCreate(uartReceiveTask, "uart_rx_task", 2048, this, 12, NULL);
    ESP_LOGI(TAG, "UART receive task started");
}

void UARTManager::uartReceiveTask(void* pvParameters) {
    UARTManager* self = static_cast<UARTManager*>(pvParameters);
    const char* TAG = UARTManager::TAG;
    
    // Buffer for receiving data
    uint8_t* data = (uint8_t*) malloc(UART_BUFFER_SIZE);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate memory for UART receive buffer");
        vTaskDelete(NULL);
        return;
    }
    
    // Event handling structure
    uart_event_t event;
   
    while (1) {
        // Wait for UART events
        if (xQueueReceive(self->uartQueue, (void*)&event, portMAX_DELAY)) {
            bzero(data, UART_BUFFER_SIZE);
            
            switch (event.type) {
                case UART_DATA:
                    // Read the received data
                    uart_read_bytes(UART_NUM, data, event.size, portMAX_DELAY);
                    ESP_LOGI(TAG, "Received %d bytes from UART", event.size);
                    
                    // Parse the received message
                    self->parseReceivedMessage(data, event.size);
                    break;
                    
                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "UART FIFO overflow detected");
                    uart_flush_input(UART_NUM);
                    xQueueReset(self->uartQueue);
                    break;
                    
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "UART buffer full");
                    uart_flush_input(UART_NUM);
                    xQueueReset(self->uartQueue);
                    break;
                    
                case UART_BREAK:
                    ESP_LOGW(TAG, "UART break detected");
                    break;
                    
                case UART_PARITY_ERR:
                    ESP_LOGW(TAG, "UART parity error");
                    break;
                    
                case UART_FRAME_ERR:
                    ESP_LOGW(TAG, "UART frame error");
                    break;
                    
                default:
                    ESP_LOGW(TAG, "UART event: %d", event.type);
                    break;
            }
        }
    }
    
    // This should never be reached
    free(data);
    vTaskDelete(NULL);
}

void UARTManager::parseReceivedMessage(const uint8_t* data, size_t length) {
    // Check if the data is long enough to contain a UARTMessage
    if (length < sizeof(UARTMessage)) {
        ESP_LOGW(TAG, "Received data too short to be a valid message (%d bytes)", length);
        return;
    }
    
    // Look for message start marker
    for (size_t i = 0; i <= length - sizeof(UARTMessage); i++) {
        if (data[i] == MESSAGE_START) {
            // Check if this could be the start of a message
            const UARTMessage* msg = reinterpret_cast<const UARTMessage*>(data + i);
            
            // Validate end marker
            if (msg->end == MESSAGE_END) {
                ESP_LOGI(TAG, "Valid message received: cmd=0x%02x, value=0x%08lx", 
                         static_cast<uint8_t>(msg->cmdType), static_cast<unsigned long>(msg->value));
                
                // Process the message based on command type
                switch (msg->cmdType) {
                    case CommandType::GET_STATUS:
                        // Send back status information
                        sendDebugMessage(0xAA55); // Just a test response
                        break;
                        
                    case CommandType::DEBUG:
                        ESP_LOGI(TAG, "Debug message from RP2040: 0x%08lx", static_cast<unsigned long>(msg->value));
                        break;
                        
                    default:
                        ESP_LOGW(TAG, "Unhandled command from RP2040: 0x%02x", static_cast<uint8_t>(msg->cmdType));
                        break;
                }
                
                // Skip ahead to avoid processing the same message multiple times
                i += sizeof(UARTMessage) - 1;
            }
        }
    }
}
