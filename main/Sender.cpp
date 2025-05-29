#include "Sender.h"
#include "config.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "esp_crc.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include "Button.h"
#include "Potentiometer.h"
#include "PayloadHelper.h"

static const char *TAG = "Sender";

// Make outgoingMessageQueue global so it can be accessed by template methods
QueueHandle_t outgoingMessageQueue = nullptr;
static std::unordered_map<std::string, uint16_t> peerSequenceNumbers; // Sequence numbers per peer
// MAC addresses of registered peers with number of failed sends.
// Used for dropping registered peers that fail to respond.
static std::unordered_map<std::string, uint8_t> peerFailedSend;

// Singleton instance implementation
Sender& Sender::getInstance() {
    static Sender instance; // Created only once on first access
    return instance;
}

// Specialized helper methods for sending common payload types
void Sender::sendPatternChange(PatternType patternType, const uint8_t* destMac) {
    ChangePatternPayload payload;
    payload.patternType = patternType;
    sendPayload(payload, PayloadType::ChangePattern, destMac);
    ESP_LOGI(TAG, "Sent pattern change: %d", static_cast<int>(patternType));
}

void Sender::sendBrightnessChange(uint8_t brightness, const uint8_t* destMac) {
    ChangeBrightnessPayload payload;
    payload.brightnessLevel = brightness;
    sendPayload(payload, PayloadType::ChangeBrightness, destMac);
    ESP_LOGI(TAG, "Sent brightness change: %u%%%%", brightness);
}

void Sender::sendSpeedChange(uint8_t speed, const uint8_t* destMac) {
    ChangeSpeedPayload payload;
    payload.speedLevel = speed;
    sendPayload(payload, PayloadType::ChangeSpeed, destMac);
    ESP_LOGI(TAG, "Sent speed change: %u%%%%", speed);
}

void Sender::sendKeepaliveMessage(const uint8_t* destMac) {
    KeepalivePayload payload;
    sendPayload(payload, PayloadType::Keepalive, destMac);
    ESP_LOGI(TAG, "Sent keepalive message");
}

void Sender::sendRegistrationResponse(const uint8_t* destMac) {
    if (!destMac) {
        ESP_LOGE(TAG, "Cannot send registration response to null MAC address");
        return;
    }
    
    RegistrationSuccessfulPayload payload;
    sendPayload(payload, PayloadType::RegistrationSuccessful, destMac);
    ESP_LOGI(TAG, "Sent registration response to MAC=" MACSTR, MAC2STR(destMac));
}

// Constructor and destructor implementation
Sender::Sender() : blueButton(nullptr), redButton(nullptr), 
                  brightnessPot(nullptr), speedPot(nullptr) {
    // Constructor implementation
    ESP_LOGI(TAG, "Sender singleton instance created");
}

Sender::~Sender() {
    // Stop potentiometer tasks first
    if (brightnessPot) {
        brightnessPot->stop();
    }
    
    if (speedPot) {
        speedPot->stop();
    }
    
    // Destructor implementation - Smart pointer objects will be automatically deleted
    ESP_LOGW(TAG, "Sender singleton instance destroyed");
}

// Button event handler implementation as class member
void Sender::handleButtonEvent(const std::string& buttonName, Button::Event event) {
    // Descriptive strings for each event type
    const char* eventNames[] = {
        "PRESSED",
        "DOUBLE_PRESSED", 
        "LONG_PRESSED", 
        "RELEASED"
    };
    
    // Log the button event with detailed information
    ESP_LOGI(TAG, "Button Event: %s - %s", 
             buttonName.c_str(), 
             eventNames[static_cast<int>(event)]);
    
    // Button-specific actions
    switch (event) {
        case Button::Event::PRESSED:
            if (buttonName == "BlueButton") {
                ESP_LOGI(TAG, "Blue button action: Sending blue pattern command");
                sendPatternChange(PatternType::BluePattern);
            } 
            else if (buttonName == "RedButton") {
                ESP_LOGI(TAG, "Red button action: Sending red pattern command");
                sendPatternChange(PatternType::RedPattern);
            }
            break;
            
        case Button::Event::DOUBLE_PRESSED:
            // Example double press action: Toggle brightness
            ESP_LOGI(TAG, "%s double pressed: Toggle brightness", buttonName.c_str());
            break;
            
        case Button::Event::LONG_PRESSED:
            // Example long press action: Power off/on
            ESP_LOGI(TAG, "%s long pressed: Power toggle", buttonName.c_str());
            break;
            
        case Button::Event::RELEASED:
            // Usually no specific action needed on release
            break;
    }
}

// Potentiometer event handler implementation
void Sender::handlePotentiometerEvent(const std::string& potName, 
                                    Potentiometer::Event event, 
                                    uint32_t value, 
                                    float percentage) {
    // Descriptive strings for each event type
    const char* eventNames[] = {
        "VALUE_CHANGED",
        "MIN_REACHED", 
        "MAX_REACHED", 
        "CENTER_REACHED"
    };
    
    // Log the potentiometer event with detailed information
    ESP_LOGI(TAG, "Potentiometer Event: %s - %s, Value: %lu (%.1f%%)", 
             potName.c_str(), 
             eventNames[static_cast<int>(event)],
             value, percentage);
             
    // Handle specific potentiometer events
    if (potName == "BrightnessPot") {
        // Handle brightness potentiometer
        switch (event) {
            case Potentiometer::Event::VALUE_CHANGED: {
                // Send brightness change command
                ESP_LOGI(TAG, "Brightness changed to %.1f%%%%", percentage);
                sendBrightnessChange(static_cast<uint8_t>(percentage));
                break;
            }
            case Potentiometer::Event::MIN_REACHED:
                ESP_LOGI(TAG, "Brightness at minimum");
                break;
                
            case Potentiometer::Event::MAX_REACHED:
                ESP_LOGI(TAG, "Brightness at maximum");
                break;
                
            case Potentiometer::Event::CENTER_REACHED:
                ESP_LOGI(TAG, "Brightness at center (50%%%%)");
        }
    } 
    else if (potName == "SpeedPot") {
        // Handle speed potentiometer
        switch (event) {
            case Potentiometer::Event::VALUE_CHANGED: {
                // Send speed change command
                ESP_LOGI(TAG, "Speed changed to %.1f%%%%", percentage);
                sendSpeedChange(static_cast<uint8_t>(percentage));
                break;
            }
            case Potentiometer::Event::MIN_REACHED:
                ESP_LOGI(TAG, "Speed at minimum");
                break;
                
            case Potentiometer::Event::MAX_REACHED:
                ESP_LOGI(TAG, "Speed at maximum");
                break;
                
            case Potentiometer::Event::CENTER_REACHED:
                ESP_LOGI(TAG, "Speed at center (50%%%%)");
                break;
        }
    }
}

esp_err_t Sender::init() {
    esp_log_level_set(TAG, SENDER_LOG_LEVEL);
    ESP_LOGI(TAG, "Initializing ESPNOW Sender");

    // Create a queue for outgoing messages
    outgoingMessageQueue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(SendParams*));
    if (!outgoingMessageQueue) {
        ESP_LOGE(TAG, "Failed to create outgoing message queue");
        return ESP_FAIL;
    }

    Button::Config blueButtonCfg = {
        .name = "BlueButton",
        .gpio_num = BUTTONBLUE_GPIO_NUM,
        .active_low = true, // Assuming active low for the button
        .long_press_time_ms = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time_ms = CONFIG_BUTTON_SHORT_PRESS_TIME_MS
    };

    Button::Config redButtonCfg = {
        .name = "RedButton",
        .gpio_num = BUTTONRED_GPIO_NUM,
        .active_low = true, // Assuming active low for the button
        .long_press_time_ms = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time_ms = CONFIG_BUTTON_SHORT_PRESS_TIME_MS
    };

    // Create button instances as class members so they persist beyond this function
    blueButton = std::make_unique<Button>(blueButtonCfg);
    redButton = std::make_unique<Button>(redButtonCfg);

    // Create button callbacks with lambdas that capture the button name
    blueButton->registerCallback([this](Button::Event event) {
        this->handleButtonEvent("BlueButton", event);
    });

    redButton->registerCallback([this](Button::Event event) {
        this->handleButtonEvent("RedButton", event);
    });

    ESP_LOGI(TAG, "Buttons initialized successfully");
    
    // Initialize potentiometers
    Potentiometer::Config brightnessPotCfg = {
        .name = "BrightnessPot",
        .gpio_num = POT_BRIGHTNESS_GPIO_NUM,
        .adc_unit = ADC_UNIT_1,
        .adc_channel = ADC_CHANNEL_4,
        .attenuation = Potentiometer::Attenuation::DB_12, // 12dB attenuation for better range
        .poll_interval_ms = POT_POLL_INTERVAL_MS,
        .change_threshold = POT_CHANGE_THRESHOLD,
        .enable_center_event = true,
        .center_threshold = POT_CENTER_THRESHOLD
    };
    
    // Potentiometer::Config speedPotCfg = {
    //     .name = "SpeedPot",
    //     .gpio_num = POT_SPEED_GPIO_NUM,
    //     .adc_unit = ADC_UNIT_1,
    //     .adc_channel = ADC_CHANNEL_7,  // Channel for GPIO 35
    //     .poll_interval_ms = POT_POLL_INTERVAL_MS,
    //     .change_threshold = POT_CHANGE_THRESHOLD,
    //     .enable_center_event = true,
    //     .center_threshold = POT_CENTER_THRESHOLD
    // };
    
    // Create potentiometer instances
    brightnessPot = std::make_unique<Potentiometer>(brightnessPotCfg);
    // speedPot = std::make_unique<Potentiometer>(speedPotCfg);
    
    // Initialize potentiometers
    if (!brightnessPot->init()) {
        ESP_LOGE(TAG, "Failed to initialize potentiometers");
        return ESP_FAIL;
    }
    
    // Register potentiometer callbacks
    brightnessPot->registerCallback([this](Potentiometer::Event event, uint32_t value, float percentage) {
        this->handlePotentiometerEvent("BrightnessPot", event, value, percentage);
    });
    
    // speedPot->registerCallback([this](Potentiometer::Event event, uint32_t value, float percentage) {
    //     this->handlePotentiometerEvent("SpeedPot", event, value, percentage);
    // });
    
    // Start monitoring potentiometer values
    brightnessPot->start();
    // speedPot->start();

    // Take an initial reading to ensure we send the correct value
    // even if the potentiometer hasn't changed yet
    float initialBrightnessPercentage = brightnessPot->getPercentage();
    this->sendBrightnessChange(static_cast<uint8_t>(initialBrightnessPercentage));
    
    ESP_LOGI(TAG, "Potentiometers initialized successfully");

    // Register send and receive callbacks
    ESP_ERROR_CHECK(esp_now_register_send_cb(Sender::sendCallback));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(Sender::recvCallback));
    
    // If we're not using point-to-point, add a broadcast peer
#if !USE_POINT_TO_POINT
    // Add broadcast peer
    if (!esp_now_is_peer_exist(broadcastMac)) {
        esp_now_peer_info_t peerInfo = {};
        peerInfo.channel = CONFIG_ESPNOW_CHANNEL;
        peerInfo.ifidx = static_cast<wifi_interface_t>(ESPNOW_WIFI_IF);
        peerInfo.encrypt = false;
        std::memcpy(peerInfo.peer_addr, broadcastMac, ESP_NOW_ETH_ALEN);

        esp_err_t result = esp_now_add_peer(&peerInfo);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Broadcast peer added successfully: MAC=" MACSTR, MAC2STR(broadcastMac));
        } else {
            ESP_LOGE(TAG, "Failed to add broadcast peer: error=%s", esp_err_to_name(result));
        }
    } else {
        ESP_LOGW(TAG, "Broadcast peer already exists: MAC=" MACSTR, MAC2STR(broadcastMac));
    }
#endif

    // Start the testing loop task
    // xTaskCreate(sendLoop, "sendLoop", 2048, nullptr, 4, nullptr);
    xTaskCreate(processOutgoingMessages, "processOutgoingMessages", 2048, nullptr, 4, nullptr);
    xTaskCreate(sendKeepalive, "sendKeepalive", 2048, nullptr, 2, nullptr);

    return ESP_OK;
}

uint16_t Sender::getNextSequenceNumber(const uint8_t *mac_addr) {
    std::string peerKey(reinterpret_cast<const char *>(mac_addr), ESP_NOW_ETH_ALEN);

    // Check if the MAC address is already in the map
    if (peerSequenceNumbers.find(peerKey) == peerSequenceNumbers.end()) {
        // Initialize the sequence number for this MAC address
        peerSequenceNumbers[peerKey] = 0;
    }

    // Increment and return the next sequence number, wrapping around at 255
    peerSequenceNumbers[peerKey] = (peerSequenceNumbers[peerKey] + 1) % 256;
    return peerSequenceNumbers[peerKey];
}

void Sender::sendCallback(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (!mac_addr) {
        ESP_LOGE(TAG, "Send callback error: null MAC address");
        return;
    }
    ESP_LOGI(TAG, "Send callback: MAC= " MACSTR ", status=%d",
             MAC2STR(mac_addr),
             status);

    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "Send failed: MAC=" MACSTR, MAC2STR(mac_addr));
        // Increment failed send count for this peer
        std::string peerKey(reinterpret_cast<const char *>(mac_addr), ESP_NOW_ETH_ALEN);
        peerFailedSend[peerKey]++;
        ESP_LOGD(TAG, "Failed sends for peer " MACSTR ": %d", MAC2STR(mac_addr), peerFailedSend[peerKey]);
        if (peerFailedSend[peerKey] >= ESPNOW_MAX_PEER_FAIL) {
            ESP_LOGW(TAG, "Dropping peer " MACSTR " due to too many failed sends", MAC2STR(mac_addr));
            esp_now_del_peer(mac_addr);
            peerFailedSend.erase(peerKey); // Remove from failed sends
        }
    }
}

// Update recvCallback to enqueue responses to outgoingMessageQueue
void Sender::recvCallback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (!recv_info || !data || len <= 0) {
        ESP_LOGE(TAG, "Receive callback error: invalid arguments");
        return;
    }
    ESP_LOGI(TAG, "Receive callback: MAC=" MACSTR ", len=%d",
             MAC2STR(recv_info->src_addr), len);

    // Parse the received data as an MessageData
    if (len < sizeof(MessageData)) {
        ESP_LOGE(TAG, "Received data too short to be valid");
        return;
    }

    auto *messageData = reinterpret_cast<const MessageData *>(data);

    // Handle the payload type
    switch (static_cast<PayloadType>(messageData->payload_type)) {
        case PayloadType::RegisterRequest: {
            ESP_LOGI(TAG, "Received Register Request from MAC=" MACSTR, MAC2STR(recv_info->src_addr));

            // Add the peer to the ESP-NOW peer list if not already added
            if (!esp_now_is_peer_exist(recv_info->src_addr)) {
                esp_now_peer_info_t peerInfo = {};
                peerInfo.channel = CONFIG_ESPNOW_CHANNEL;
                peerInfo.ifidx = static_cast<wifi_interface_t>(ESPNOW_WIFI_IF);
                peerInfo.encrypt = false;
                std::memcpy(peerInfo.peer_addr, recv_info->src_addr, ESP_NOW_ETH_ALEN);

                if (esp_now_add_peer(&peerInfo) == ESP_OK) {
                    ESP_LOGI(TAG, "Added peer: MAC=" MACSTR, MAC2STR(recv_info->src_addr));
                } else {
                    ESP_LOGE(TAG, "Failed to add peer: MAC=" MACSTR, MAC2STR(recv_info->src_addr));
                    break;
                }
            } else {
                ESP_LOGI(TAG, "Peer already registered: MAC=" MACSTR, MAC2STR(recv_info->src_addr));
                // Reset failed send count as we got a message from this peer
                std::string peerKey(reinterpret_cast<const char *>(recv_info->src_addr), ESP_NOW_ETH_ALEN);
                peerFailedSend[peerKey] = 0;
            }
            
            // Always send registration response regardless of whether the peer is new or existing
            Sender::getInstance().sendRegistrationResponse(recv_info->src_addr);
            break;
        }

        default:
            ESP_LOGW(TAG, "Unhandled payload type: %d", messageData->payload_type);
            break;
    }
}

void Sender::processOutgoingMessages(void *pvParameter) {
    ESP_LOGI(TAG, "Processing queue task started");

    while (true) {
        // Log before calling xQueueReceive
        UBaseType_t queueItems = uxQueueMessagesWaiting(outgoingMessageQueue);
        ESP_LOGD(TAG, "Queue items before receive: %u", queueItems);

        SendParams *sendParams;
        if (xQueueReceive(outgoingMessageQueue, &sendParams, portMAX_DELAY) == pdTRUE) {
            if (!sendParams) {
                ESP_LOGE(TAG, "Dequeued null sendParams");
                continue;
            }

            // Check if there are any registered peers
            esp_now_peer_num_t peerCount = {};
            esp_now_get_peer_num(&peerCount);

            if (esp_log_level_get(TAG) == ESP_LOG_DEBUG) {
                ESP_LOGD(TAG, "processOutgoingMessages: Registered peers: %d", peerCount.total_num);
                logRegisteredPeers();
            }

            if (peerCount.total_num == 0) {
                ESP_LOGW(TAG, "No registered peers. Skipping message send.");
                delete sendParams;
                continue;
            }

            // Get number of peers registered
            esp_err_t result = esp_now_send(nullptr, sendParams->raw_data, sendParams->data_len);
            if (result == ESP_OK) {
                ESP_LOGI(TAG, "Message sent successfully to %d receivers", peerCount.total_num);
            } else {
                ESP_LOGE(TAG, "Failed to send message error=%s", esp_err_to_name(result));
                // Increment failed send count for this peer
                std::string peerKey(reinterpret_cast<const char *>(sendParams->dest_mac), ESP_NOW_ETH_ALEN);
                peerFailedSend[peerKey]++;
                ESP_LOGD(TAG, "Failed sends for peer " MACSTR ": %d", MAC2STR(sendParams->dest_mac), peerFailedSend[peerKey]);
                if (peerFailedSend[peerKey] >= ESPNOW_MAX_PEER_FAIL) {
                    ESP_LOGW(TAG, "Dropping peer " MACSTR " due to too many failed sends", MAC2STR(sendParams->dest_mac));
                    esp_now_del_peer(sendParams->dest_mac);
                    peerFailedSend.erase(peerKey); // Remove from failed sends
                }
            }

            delete sendParams;
        }
    }
}

void Sender::prepareSendParams(SendParams &sendParams, const uint8_t *payload, size_t payload_len, PayloadType payload_type) {
    // Log payload length and buffer sizes
    ESP_LOGD(TAG, "Payload length: %zu, raw_data size: %zu", payload_len, sizeof(sendParams.raw_data));

    // Validate payload length
    if (payload_len > ESP_NOW_MAX_DATA_LEN_V2 - sizeof(MessageData)) {
        ESP_LOGE(TAG, "Payload length exceeds maximum allowed: %zu", payload_len);
        return;
    }

    // Calculate the total size needed for MessageData and the payload
    size_t messageDataSize = sizeof(MessageData) + payload_len;

    // Dynamically allocate memory for MessageData and its payload
    MessageData *messageData = reinterpret_cast<MessageData *>(malloc(messageDataSize));
    if (!messageData) {
        ESP_LOGE(TAG, "Failed to allocate memory for MessageData");
        return;
    }

    // Initialize the fixed fields of MessageData
    messageData->seq_num = getNextSequenceNumber(sendParams.dest_mac);
    messageData->payload_type = static_cast<uint8_t>(payload_type);

    ESP_LOGI(TAG, "Preparing to send payload type: %d", messageData->payload_type);

    // Copy the payload into the flexible array member
    memcpy(messageData->payload, payload, payload_len);

    // Set the CRC field to 0 before calculating the CRC
    messageData->crc = 0;

    // Calculate CRC over the entire copied structure
    uint16_t calculatedCrc = esp_crc16_le(UINT16_MAX, reinterpret_cast<const uint8_t *>(messageData), messageDataSize);

    messageData->crc = calculatedCrc;

    ESP_LOGI(TAG, "Calculated CRC: %04X", messageData->crc);

    // Ensure raw_data buffer is large enough to hold the entire messageData
    if (messageDataSize > sizeof(sendParams.raw_data)) {
        ESP_LOGE(TAG, "raw_data buffer size is insufficient");
        free(messageData);
        return;
    }

    // Copy the entire messageData (including the payload) into raw_data
    memcpy(sendParams.raw_data, messageData, messageDataSize);
    sendParams.data_len = messageDataSize;

    // Free the allocated memory for messageData
    free(messageData);
}

void Sender::sendLoop(void *pvParameter) {
    ESP_LOGI(TAG, "Send loop task started");

    while (true) {
        // Check if there are any registered peers
        esp_now_peer_num_t peerCount = {};
        esp_now_get_peer_num(&peerCount);

        if (peerCount.total_num == 0) {
            ESP_LOGD(TAG, "No registered peers. Skipping message queueing.");
            vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay before checking again
            continue;
        }
        
        // Send a random pattern using the helper method
        PatternType randomPattern = static_cast<PatternType>(1 + (esp_random() % 7)); // Generate a random pattern type (1-7)
        Sender::getInstance().sendPatternChange(randomPattern);

        // Delay for 1 second before sending the next message
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void Sender::sendKeepalive(void *pvParameter) {
    ESP_LOGI(TAG, "Keepalive task started");

    while (true) {
        // Check if there are any registered peers
        esp_now_peer_num_t peerCount = {};
        esp_now_get_peer_num(&peerCount);

        if (peerCount.total_num == 0) {
            ESP_LOGD(TAG, "No registered peers. Skipping keepalive message.");
            vTaskDelay(5000 / portTICK_PERIOD_MS); // Delay before checking again
            continue;
        }

        // Send keepalive using the helper method
        Sender::getInstance().sendKeepaliveMessage();

        // Delay for 5 seconds before sending the next keepalive message
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void Sender::logRegisteredPeers() {
    esp_now_peer_num_t peerCount = {};
    esp_now_get_peer_num(&peerCount);

    ESP_LOGI(TAG, "Total registered peers: %d", peerCount.total_num);

    if (peerCount.total_num > 0) {
        esp_now_peer_info_t peerInfo = {};

        for (int i = 0; i < peerCount.total_num; i++) {
            if (esp_now_fetch_peer(true, &peerInfo) == ESP_OK) {
                ESP_LOGI(TAG, "Peer %d: MAC=" MACSTR, i, MAC2STR(peerInfo.peer_addr));
            } else {
                ESP_LOGE(TAG, "Failed to fetch info for peer %d", i);
            }
        }
    }
}
