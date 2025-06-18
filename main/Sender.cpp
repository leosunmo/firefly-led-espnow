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
#include <algorithm> // For min/max
#include "InputManager.h" // Centralized input management
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
    ESP_LOGI(TAG, "Sent pattern change: %s (index: %d)", 
            getPatternName(patternType), static_cast<int>(patternType));
}

void Sender::sendHueChange(uint8_t index, uint16_t hue, const uint8_t* destMac) {
    ChangeHuePayload payload;
    payload.index = index;
    payload.hueVal = hue;
    sendPayload(payload, PayloadType::ChangeHue, destMac);
    ESP_LOGI(TAG, "Sent hue change: index=%d, hue=%u°", index, hue);
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

void Sender::sendEffectPunch(uint8_t intensity, const uint8_t* destMac) {
    EffectPunchPayload payload;
    payload.intensity = intensity;
    sendPayload(payload, PayloadType::EffectPunch, destMac);
    ESP_LOGI(TAG, "Sent effect punch: %u%% intensity", intensity);
}

void Sender::sendKeepaliveMessage(const uint8_t* destMac) {
    KeepalivePayload payload;
    
    // If destMac is provided, send to that specific peer
    if (destMac) {
        sendPayload(payload, PayloadType::Keepalive, destMac);
        ESP_LOGI(TAG, "Sent keepalive message to specific peer");
    } else {
        // For broadcast keepalives, use the special method to ensure proper sequence numbers
        std::vector<uint8_t> serializedPayload = PayloadHelper::serialize(payload);
        sendToAllPeers(serializedPayload.data(), serializedPayload.size(), PayloadType::Keepalive);
        ESP_LOGI(TAG, "Sent keepalive message to all peers");
    }
}

void Sender::sendCurrentSettings(const uint8_t* destMac) {
    if (!destMac) {
        ESP_LOGE(TAG, "Cannot send current settings to null MAC address");
        return;
    }
    
    ESP_LOGI(TAG, "Sending all current settings to newly connected peer: " MACSTR, MAC2STR(destMac));
    
    // Get all current input states from InputManager
    InputManager& inputManager = InputManager::getInstance();
    
    // 1. Send current active pattern
    PatternType currentPattern = inputManager.getActivePattern();
    sendPatternChange(currentPattern, destMac);
    
    // 2. Send current brightness level
    float brightnessPercentage = inputManager.getPotPercentage(PotentiometerId::BRIGHTNESS_POT);
    if (brightnessPercentage >= 0) {
        sendBrightnessChange(static_cast<uint8_t>(brightnessPercentage), destMac);
    }
    
    // 3. Send current speed level
    float speedPercentage = inputManager.getPotPercentage(PotentiometerId::SPEED_POT);
    if (speedPercentage >= 0) {
        sendSpeedChange(static_cast<uint8_t>(speedPercentage), destMac);
    }
    
    // 4. If encoders are enabled, send current hue values
    if (inputManager.areEncodersEnabled()) {
        // For CHROMA_WAVE pattern, we need to send both encoder hues
        LEDManager& ledManager = LEDManager::getInstance();
        
        // Get encoder A hue (primary/index 1)
        auto hueA = ledManager.getCurrentColorHSV(LEDManager::LEDId::ENCODER_A_RGB);
        if (hueA.v > 0) { // Only send if the LED is active
            sendHueChange(1, hueA.h, destMac);
        }
        
        // Get encoder B hue (secondary/index 0)
        auto hueB = ledManager.getCurrentColorHSV(LEDManager::LEDId::ENCODER_B_RGB);
        if (hueB.v > 0) { // Only send if the LED is active
            sendHueChange(0, hueB.h, destMac);
        }
    }
    
    ESP_LOGI(TAG, "Finished sending all current settings to peer");
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
Sender::Sender() {
    // Constructor implementation
    ESP_LOGI(TAG, "Sender singleton instance created");
}

Sender::~Sender() {
    // Destructor implementation - Input management now handled by InputManager
    ESP_LOGW(TAG, "Sender singleton instance destroyed");
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

    // Initialize the input manager (handles buttons, potentiometers, etc.)
    esp_err_t input_err = InputManager::getInstance().init();
    if (input_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize InputManager: %s", esp_err_to_name(input_err));
        // Continue anyway, other functionality might still work
    } else {
        // Set up input handlers with the input manager
        setupInputHandlers();
        ESP_LOGI(TAG, "Input handlers set up successfully");
        
        // Take an initial reading to ensure we send the correct brightness value
        float initialBrightnessPercentage = InputManager::getInstance().getPotPercentage(PotentiometerId::BRIGHTNESS_POT);
        if (initialBrightnessPercentage >= 0) {
            this->sendBrightnessChange(static_cast<uint8_t>(initialBrightnessPercentage));
            ESP_LOGI(TAG, "Initial brightness value sent: %.1f%%", initialBrightnessPercentage);
        }
    }

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
        ESP_LOGD(TAG, "Initialized sequence number for peer " MACSTR " to 0", MAC2STR(mac_addr));
    }

    // Increment and return the next sequence number, wrapping around at 255
    uint16_t current = peerSequenceNumbers[peerKey];
    peerSequenceNumbers[peerKey] = (current + 1) % 256;
    
    // Log the sequence number for debugging
    if (esp_log_level_get(TAG) >= ESP_LOG_DEBUG) {
        ESP_LOGD(TAG, "Next sequence number for peer " MACSTR ": %d (was %d)", 
                 MAC2STR(mac_addr), peerSequenceNumbers[peerKey], current);
    }
    
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
            }
            
            // Reset sequence number for this peer
            std::string peerKey(reinterpret_cast<const char *>(recv_info->src_addr), ESP_NOW_ETH_ALEN);
            peerSequenceNumbers[peerKey] = 0;
            ESP_LOGI(TAG, "Reset sequence number for peer: " MACSTR, MAC2STR(recv_info->src_addr));
            
            // Reset failed send count
            peerFailedSend[peerKey] = 0;
            
           
            // Always send registration response regardless of whether the peer is new or existing
            Sender::getInstance().sendRegistrationResponse(recv_info->src_addr);
            
            // Send all current settings to the newly connected peer
            Sender::getInstance().sendCurrentSettings(recv_info->src_addr);
            break;
        }
        
        case PayloadType::StateRequest: {
            ESP_LOGI(TAG, "Received State Request from MAC=" MACSTR, MAC2STR(recv_info->src_addr));
            
            // Ensure the peer is registered
            if (!esp_now_is_peer_exist(recv_info->src_addr)) {
                ESP_LOGI(TAG, "Peer not registered yet, adding: MAC=" MACSTR, MAC2STR(recv_info->src_addr));
                esp_now_peer_info_t peerInfo = {};
                peerInfo.channel = CONFIG_ESPNOW_CHANNEL;
                peerInfo.ifidx = static_cast<wifi_interface_t>(ESPNOW_WIFI_IF);
                peerInfo.encrypt = false;
                std::memcpy(peerInfo.peer_addr, recv_info->src_addr, ESP_NOW_ETH_ALEN);

                if (esp_now_add_peer(&peerInfo) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to add peer: MAC=" MACSTR, MAC2STR(recv_info->src_addr));
                    break;
                }
            }
            
            // Create a key for this peer
            std::string peerKey(reinterpret_cast<const char *>(recv_info->src_addr), ESP_NOW_ETH_ALEN);
            
            // Reset sequence number for this peer
            // This is important to prevent sequence number mismatch issues
            peerSequenceNumbers[peerKey] = 0;
            ESP_LOGI(TAG, "Reset sequence number for peer: " MACSTR, MAC2STR(recv_info->src_addr));
            
            // Reset failed send count
            peerFailedSend[peerKey] = 0;
            
            // Send registration response and current settings
            Sender::getInstance().sendRegistrationResponse(recv_info->src_addr);
            Sender::getInstance().sendCurrentSettings(recv_info->src_addr);
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
                ESP_LOGD(TAG, "No registered peers. Skipping message send.");
                delete sendParams;
                continue;
            }

            // If we're sending to a specific peer, use that MAC address
            const uint8_t* dest_mac = nullptr;
            if (sendParams->dest_mac[0] != 0 || sendParams->dest_mac[1] != 0) {
                dest_mac = sendParams->dest_mac;
            }
            
            // Send the message
            esp_err_t result = esp_now_send(dest_mac, sendParams->raw_data, sendParams->data_len);
            if (result == ESP_OK) {
                ESP_LOGI(TAG, "Message sent successfully to %s", 
                         dest_mac ? "specific peer" : "all registered peers");
            } else {
                ESP_LOGE(TAG, "Failed to send message error=%s", esp_err_to_name(result));
                if (dest_mac) {
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

    // If dest_mac is null, we're sending to all peers, so we need to use a special sequence number
    uint16_t seq_num = 0;
    if (sendParams.dest_mac[0] == 0 && sendParams.dest_mac[1] == 0) {
        // When dest_mac is empty, esp_now_send will send individually to each registered peer
        // Each peer needs its own sequence number - but this should rarely happen now
        // since we use sendToAllPeers for broadcasting
        
        // Generate a sequence number for a null MAC (common counter for broadcast-style operations)
        seq_num = getNextSequenceNumber(broadcastMac);
        ESP_LOGW(TAG, "Using fallback broadcast sequence number: %d - This path should rarely be taken!", seq_num);
    } else {
        // Specific peer, use its sequence counter
        seq_num = getNextSequenceNumber(sendParams.dest_mac);
        ESP_LOGI(TAG, "Preparing message for peer " MACSTR " with sequence number %d and payload type %d", 
                 MAC2STR(sendParams.dest_mac), seq_num, static_cast<int>(payload_type));
    }

    // Initialize the fixed fields of MessageData
    messageData->seq_num = seq_num;
    messageData->payload_type = static_cast<uint8_t>(payload_type);

    ESP_LOGD(TAG, "Preparing to send payload type: %d", messageData->payload_type);

    // Copy the payload into the flexible array member
    memcpy(messageData->payload, payload, payload_len);

    // Set the CRC field to 0 before calculating the CRC
    messageData->crc = 0;

    // Calculate CRC over the entire copied structure
    uint16_t calculatedCrc = esp_crc16_le(UINT16_MAX, reinterpret_cast<const uint8_t *>(messageData), messageDataSize);

    messageData->crc = calculatedCrc;

    ESP_LOGD(TAG, "Calculated CRC: %04X", messageData->crc);

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

    ESP_LOGD(TAG, "Total registered peers: %d", peerCount.total_num);

    if (peerCount.total_num > 0) {
        esp_now_peer_info_t peerInfo = {};

        for (int i = 0; i < peerCount.total_num; i++) {
            if (esp_now_fetch_peer(true, &peerInfo) == ESP_OK) {
                ESP_LOGD(TAG, "Peer %d: MAC=" MACSTR, i, MAC2STR(peerInfo.peer_addr));
            } else {
                ESP_LOGE(TAG, "Failed to fetch info for peer %d", i);
            }
        }
    }
}

bool Sender::sendToAllPeers(const uint8_t* payload, size_t payload_len, PayloadType payload_type) {
    esp_now_peer_num_t peerCount = {};
    esp_now_get_peer_num(&peerCount);
    
    if (peerCount.total_num == 0) {
        ESP_LOGW(TAG, "No registered peers to send to");
        return false;
    }
    
    ESP_LOGI(TAG, "Sending to all %d registered peers with individual sequence numbers using queue", peerCount.total_num);
    
    bool allQueued = true;
    esp_now_peer_info_t peerInfo = {};
    
    // Reset the peer fetch context to fetch peers from the beginning
    for (int i = 0; i < peerCount.total_num; i++) {
        if (esp_now_fetch_peer(i == 0, &peerInfo) == ESP_OK) {
            // Create a new SendParams object for this peer
            auto* params = new SendParams;
            if (!params) {
                ESP_LOGE(TAG, "Failed to allocate memory for SendParams");
                allQueued = false;
                continue;
            }
            
            // Copy the peer's MAC address
            std::memcpy(params->dest_mac, peerInfo.peer_addr, ESP_NOW_ETH_ALEN);
            
            // Prepare the send parameters with the correct sequence number for this peer
            prepareSendParams(*params, payload, payload_len, payload_type);
            
            ESP_LOGD(TAG, "Queuing message to peer " MACSTR " with payload type %d", 
                    MAC2STR(peerInfo.peer_addr), static_cast<int>(payload_type));
            
            // Add to the outgoing message queue
            if (xQueueSend(outgoingMessageQueue, &params, portMAX_DELAY) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to queue message for peer " MACSTR, MAC2STR(peerInfo.peer_addr));
                delete params;
                allQueued = false;
            } else {
                ESP_LOGD(TAG, "Successfully queued message to peer " MACSTR, MAC2STR(peerInfo.peer_addr));
            }
        } else {
            ESP_LOGE(TAG, "Failed to fetch info for peer %d", i);
            allQueued = false;
        }
    }
    
    return allQueued;
}
