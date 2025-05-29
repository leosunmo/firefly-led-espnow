#include "Receiver.h"
#include "Messages.h"
#include "Manager.h"
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
#include "freertos/queue.h"
#include <vector>
#include <memory>
#include <new>
#include <unordered_map>

static const char *TAG = "Receiver";

// receiveQueue expects a MessageEnvelope
static QueueHandle_t receiveQueue = nullptr;
std::unordered_map<std::string, uint16_t> Receiver::peerLastSequenceNumbers; // Last received sequence numbers per peer
bool volatile Receiver::isRegistered = false; // Registration status
// Initialize to current time to avoid immediate timeout
static uint32_t lastKeepaliveTime = 0; // Track the last keepalive time

void Receiver::init() {
    esp_log_level_set(TAG, RECEIVER_LOG_LEVEL);
    ESP_LOGI(TAG, "Initializing ESPNOW Receiver");
    
    // Initialize lastKeepaliveTime to the current time to prevent immediate timeouts
    lastKeepaliveTime = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Create a queue for MessageEnvelope.
    receiveQueue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(MessageEnvelope));
    if (!receiveQueue) {
        ESP_LOGE(TAG, "Failed to create receive queue");
        return;
    }

    ESP_LOGI(TAG, "ESPNOW initialized successfully");

    // Register receive callback
    ESP_ERROR_CHECK( esp_now_register_recv_cb(recvCallback) );

    ESP_LOGI(TAG, "Receive callback registered successfully");

    // Increase stack size for recvLoop task
    xTaskCreate(recvLoop, "recvLoop", 4096, nullptr, 4, nullptr);
    ESP_LOGI(TAG, "Receive loop task started");

#if USE_POINT_TO_POINT
    // Start the broadcast registration task
    xTaskCreate(broadcastRegistration, "broadcastRegistration", 2048, nullptr, 4, nullptr);
#endif

    // Start the keepalive monitoring task
    xTaskCreate(checkKeepalive, "checkKeepalive", 2048, nullptr, 2, nullptr);
}

void Receiver::recvCallback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (!recv_info || !data || len <= 0) {
        ESP_LOGE(TAG, "Receive callback error: invalid arguments (recv_info=%p, data=%p, len=%d)", recv_info, data, len);
        return;
    }

    if (len > ESP_NOW_MAX_DATA_LEN_V2) { // Use the maximum allowed data length
        ESP_LOGE(TAG, "Received data length exceeds buffer size: len=%d", len);
        return;
    }

    ESP_LOGI(TAG, "Received ESPNOW data from MAC= " MACSTR ", len=%d",
             MAC2STR(recv_info->src_addr), len);

    // Create a MessageEnvelope object with the correct data length
    MessageEnvelope *receivedEnvelope = new MessageEnvelope(len);
    if (!receivedEnvelope) {
        ESP_LOGE(TAG, "Failed to allocate memory for MessageEnvelope");
        return;
    }

    std::memcpy(receivedEnvelope->src_mac, recv_info->src_addr, ESP_NOW_ETH_ALEN);
    std::memcpy(receivedEnvelope->data, data, len);

    // Send the message to the queue
    if (xQueueSend(receiveQueue, &receivedEnvelope, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue received message");
        delete receivedEnvelope;
    }
}

void Receiver::recvLoop(void *pvParameter) {
    ESP_LOGI(TAG, "Receive loop task started");

    while (true) {
        MessageEnvelope *recvMsg;
        if (xQueueReceive(receiveQueue, &recvMsg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Processing received data from MAC= " MACSTR ", len=%d",
                     MAC2STR(recvMsg->src_mac), recvMsg->data_len);

            // Create a new Message object
            Message* message = new Message();
            if (!message) {
                ESP_LOGE(TAG, "Failed to allocate memory for Message");
                delete recvMsg; // Free the received message
                continue;
            }

            // Set the message type based on the received MAC address
            message->type = IS_BROADCAST_ADDR(recvMsg->src_mac) ? ESPNOW_DATA_BROADCAST : ESPNOW_DATA_UNICAST;

            // Parse the received data
            int type = parseESPNOWData(recvMsg->data, recvMsg->data_len, recvMsg->src_mac, message);
            if (type < 0) {
                ESP_LOGE(TAG, "Failed to parse ESPNOW data");
                delete message;
                delete recvMsg; // Free the received message
                continue;
            }

            if (message->payload_type == PayloadType::RegisterRequest) {
                // Handle registration request
                ESP_LOGI(TAG, "Received registration request from MAC= " MACSTR,
                         MAC2STR(recvMsg->src_mac));
                isRegistered = true; // Set registration status
                lastKeepaliveTime = xTaskGetTickCount() * portTICK_PERIOD_MS; // Update keepalive time
            }
            
            if (message->payload_type == PayloadType::RegistrationSuccessful) {
                // Handle registration successful response
                ESP_LOGI(TAG, "Received registration successful from MAC= " MACSTR,
                         MAC2STR(recvMsg->src_mac));
                
                // If we weren't registered before, reset sequence numbers for clean start
                if (!isRegistered) {
                    peerLastSequenceNumbers.clear();
                    ESP_LOGI(TAG, "Cleared sequence number tracking on new registration");
                }
                
                // Create a key for this sender's MAC address
                std::string peerKey(reinterpret_cast<const char *>(recvMsg->src_mac), ESP_NOW_ETH_ALEN);
                
                // Reset the sequence number for this specific peer
                peerLastSequenceNumbers[peerKey] = 0;
                ESP_LOGI(TAG, "Reset sequence number for peer: " MACSTR, MAC2STR(recvMsg->src_mac));
                
                isRegistered = true; // Set registration status
                lastKeepaliveTime = xTaskGetTickCount() * portTICK_PERIOD_MS; // Update keepalive time
            }

            if (message->payload_type == PayloadType::Keepalive) {
                ESP_LOGD(TAG, "Received keepalive message from MAC= " MACSTR, MAC2STR(recvMsg->src_mac));
                lastKeepaliveTime = xTaskGetTickCount() * portTICK_PERIOD_MS; // Update the last keepalive time
                
                // Treat keepalive as confirmation of registration
                if (!isRegistered) {
                    ESP_LOGI(TAG, "Received keepalive, setting isRegistered to true");
                    isRegistered = true;
                }
                
                delete message; // No further processing needed for keepalive
                delete recvMsg;
                continue;
            }
            
            // Update keepalive time for any valid message from a known peer
            if (message->type == ESPNOW_DATA_UNICAST) {
                lastKeepaliveTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
                
                if (!isRegistered) {
                    ESP_LOGI(TAG, "Received unicast message, setting isRegistered to true");
                    isRegistered = true;
                }
            }

            // TODO: Process the parsed message
            ESP_LOGI(TAG, "Parsed ESPNOW message: type=%d",
                     static_cast<int>(message->payload_type));

            // Free the allocated message and received message
            delete message;
            delete recvMsg;
        }
    }
}

int Receiver::parseESPNOWData(const uint8_t *data, uint16_t data_len, const uint8_t *src_addr, Message *message) {
    if (!data || data_len < sizeof(MessageData) || !message) {
        ESP_LOGE(TAG, "Received ESPNOW data too short, null, or invalid message pointer, len:%d", data_len);
        return -1;
    }

    // Cast the raw data to MessageData
    const MessageData *rawMessage = reinterpret_cast<const MessageData *>(data);
      // Cast rawMessage->payload_type to PayloadType for type-safe comparison
    PayloadType payloadType = static_cast<PayloadType>(rawMessage->payload_type);

    // Determine the expected payload size based on the payload type
    size_t expectedPayloadSize = 0;
    switch (payloadType) {
        case PayloadType::ChangePattern:
            expectedPayloadSize = 1; // At least 1 byte for a non-empty pattern name
            break;
        case PayloadType::ChangeBrightness:
            expectedPayloadSize = sizeof(uint8_t); // A single byte for brightness level
            break;
        case PayloadType::ChangeSpeed:
            expectedPayloadSize = sizeof(uint8_t); // A single byte for speed level
            break;
        case PayloadType::RegistrationSuccessful:
            expectedPayloadSize = 0; // Empty payload is fine
            break;
        case PayloadType::Keepalive:
            expectedPayloadSize = 0; // Keepalive has no additional payload
            break;
        default:
            ESP_LOGE(TAG, "Unhandled payload type in switch: %d", static_cast<int>(payloadType));
            return -1;
    }

    // Populate the Message with the payload type
    message->payload_type = payloadType;

    // Validate the total data length
    if (data_len < sizeof(MessageData) + expectedPayloadSize) {
        ESP_LOGE(TAG, "Data length is insufficient for payload type: %d", static_cast<int>(payloadType));
        return -1;
    }

    // Allocate memory for messageCopy to include the payload
    MessageData *messageCopy = reinterpret_cast<MessageData *>(malloc(data_len));
    if (!messageCopy) {
        ESP_LOGE(TAG, "Failed to allocate memory for messageCopy");
        return -1;
    }

    // Copy the entire rawMessage (header + payload) into messageCopy
    memcpy(messageCopy, rawMessage, data_len);

    // Set the CRC field to 0 before calculating the CRC
    messageCopy->crc = 0;

    // Calculate CRC over the entire message, including the payload
    uint16_t calculatedCrc = esp_crc16_le(UINT16_MAX, reinterpret_cast<const uint8_t *>(messageCopy), data_len);

    // Free the allocated memory for messageCopy
    free(messageCopy);

    if (calculatedCrc != rawMessage->crc) {
        ESP_LOGE(TAG, "CRC mismatch: calculated %04X, received %04X", calculatedCrc, rawMessage->crc);
        return -1;
    }

    // Check for sequence number wrap-around
    std::string peerKey(reinterpret_cast<const char *>(src_addr), ESP_NOW_ETH_ALEN);
    
    // Use the find method to check if this is a new peer
    auto seqNumIt = peerLastSequenceNumbers.find(peerKey);
    bool isNewPeer = (seqNumIt == peerLastSequenceNumbers.end());
    
    // If it's a new peer or we're handling RegistrationSuccessful, accept any sequence number
    if (isNewPeer || payloadType == PayloadType::RegistrationSuccessful) {
        ESP_LOGI(TAG, "New peer or registration message, accepting seq_num=%d", rawMessage->seq_num);
        peerLastSequenceNumbers[peerKey] = rawMessage->seq_num;
    } else {
        uint16_t lastSeqNum = seqNumIt->second;
        
        // Check if this is a valid next sequence number or wrap-around
        if ((rawMessage->seq_num > lastSeqNum) ||
            (lastSeqNum > 200 && rawMessage->seq_num < 50)) { // Handle wrap-around
            // Valid sequence number, update our tracking
            peerLastSequenceNumbers[peerKey] = rawMessage->seq_num;
            ESP_LOGD(TAG, "Valid sequence: prev=%d, current=%d", lastSeqNum, rawMessage->seq_num);
        } else {
            // Special case: if sequence starts from 0/1, it could be a restarted sender
            if (rawMessage->seq_num <= 1 && lastSeqNum > 100) {
                ESP_LOGI(TAG, "Detected sender restart: prev=%d, current=%d. Accepting message.", 
                        lastSeqNum, rawMessage->seq_num);
                peerLastSequenceNumbers[peerKey] = rawMessage->seq_num;
            } else {
                ESP_LOGW(TAG, "Ignoring duplicate or out-of-order message: seq_num=%d, lastSeqNum=%d",
                        rawMessage->seq_num, lastSeqNum);
                return -1; // Ignore the message
            }
        }
    }

    // Set the message type based on the source address
    message->type = IS_BROADCAST_ADDR(src_addr) ? ESPNOW_DATA_BROADCAST : ESPNOW_DATA_UNICAST;


    // Parse the payload based on the payload type
    size_t payloadSize = data_len - sizeof(MessageData);
    switch (message->payload_type) {
        case PayloadType::ChangePattern: {
            if (payloadSize < sizeof(uint8_t)) {
                ESP_LOGE(TAG, "Empty payload for ChangePatternPayload");
                return -1;
            }
            // Deserialize the payload properly
            ChangePatternPayload payload;
            payload.patternType = static_cast<PatternType>(rawMessage->payload[0]);
            message->parsed_payload = payload;
            break;
        }
        case PayloadType::ChangeBrightness: {
            if (payloadSize < sizeof(uint8_t)) {
                ESP_LOGE(TAG, "Payload size mismatch for ChangeBrightnessPayload");
                return -1;
            }
            ChangeBrightnessPayload payload;
            payload.brightnessLevel = rawMessage->payload[0];
            message->parsed_payload = payload;
            break;
        }
        case PayloadType::ChangeSpeed: {
            if (payloadSize < sizeof(uint8_t)) {
                ESP_LOGE(TAG, "Payload size mismatch for ChangeSpeedPayload");
                return -1;
            }
            ChangeSpeedPayload payload;
            payload.speedLevel = rawMessage->payload[0];
            message->parsed_payload = payload;
            break;
        }
        case PayloadType::RegistrationSuccessful: {
            // RegistrationSuccessful doesn't need payload data
            RegistrationSuccessfulPayload payload;
            message->parsed_payload = payload;
            break;
        }
        case PayloadType::Keepalive: {
            // Keepalive doesn't need payload data
            KeepalivePayload payload;
            message->parsed_payload = payload;
            break;
        }
        default:
            ESP_LOGE(TAG, "Unknown payload type: %d", static_cast<int>(message->payload_type));
            return -1;
    }

    return 0;
}

// Add a task to broadcast registration requests
void Receiver::broadcastRegistration(void *pvParameter) {
    ESP_LOGI(TAG, "Broadcast registration task started");

    // Register the broadcast MAC address
    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = CONFIG_ESPNOW_CHANNEL;
    peerInfo.ifidx = static_cast<wifi_interface_t>(ESPNOW_WIFI_IF);
    peerInfo.encrypt = false;
    std::memcpy(peerInfo.peer_addr, broadcastMac, ESP_NOW_ETH_ALEN);
    esp_err_t result = esp_now_add_peer(&peerInfo);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(result));
        vTaskDelete(nullptr); // Delete the task if adding the peer fails
        return;
    }

    MessageData registrationRequest = {};
    registrationRequest.seq_num = 0;
    registrationRequest.payload_type = static_cast<uint8_t>(PayloadType::RegisterRequest);
    registrationRequest.crc = 0; // CRC will be calculated by the sender

    while (!isRegistered) { // Check registration status before broadcasting
        esp_err_t result = esp_now_send(broadcastMac, reinterpret_cast<uint8_t *>(&registrationRequest), sizeof(registrationRequest));
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Broadcasted registration request");
        } else {
            ESP_LOGE(TAG, "Failed to broadcast registration request: %s", esp_err_to_name(result));
        }

        // Delay for 1 second before broadcasting again
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    // Unregister the broadcast peer after successful registration
    result = esp_now_del_peer(broadcastMac);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete broadcast peer: %s", esp_err_to_name(result));
    } else {
        ESP_LOGI(TAG, "Deleted broadcast peer successfully");
    }

    ESP_LOGI(TAG, "Registration successful, stopping broadcast task");
    vTaskDelete(nullptr); // Delete the task once registration is complete
}

void Receiver::checkKeepalive(void *pvParameter) {
    ESP_LOGI(TAG, "Keepalive monitoring task started");

    while (true) {
        uint32_t currentTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t timeSinceLastKeepalive = currentTime - lastKeepaliveTime;

        // Add more debugging at ESP_LOGD level
        ESP_LOGD(TAG, "Keepalive check: isRegistered=%d, timeSinceLastKeepalive=%lu ms", 
                 isRegistered ? 1 : 0, timeSinceLastKeepalive);

        if (isRegistered && (timeSinceLastKeepalive > 10000)) { // 10-second timeout
            ESP_LOGW(TAG, "Keepalive timeout after %lu ms. Restarting registration broadcast.", 
                     timeSinceLastKeepalive);
            
            // Reset registration status
            isRegistered = false;
            
            // Clear the sequence number tracking to accept messages from restarted sender
            peerLastSequenceNumbers.clear();
            ESP_LOGI(TAG, "Cleared sequence number tracking for all peers");
            
            // Start registration broadcast
            xTaskCreate(broadcastRegistration, "broadcastRegistration", 2048, nullptr, 4, nullptr);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS); // Check every second
    }
}
