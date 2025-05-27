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

static const char *TAG = "Sender";

static QueueHandle_t outgoingMessageQueue = nullptr;
static std::unordered_map<std::string, uint16_t> peerSequenceNumbers; // Sequence numbers per peer
static std::unordered_map<std::string, int> failedPeerCounts; // Track failed send attempts per peer

esp_err_t Sender::init() {
    esp_log_level_set(TAG, SENDER_LOG_LEVEL);
    ESP_LOGI(TAG, "Initializing ESPNOW Sender");

    // Create a queue for outgoing messages
    outgoingMessageQueue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(SendParams*));
    if (!outgoingMessageQueue) {
        ESP_LOGE(TAG, "Failed to create outgoing message queue");
        return ESP_FAIL;
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
    xTaskCreate(sendLoop, "sendLoop", 2048, nullptr, 4, nullptr);
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

void Sender::handleFailedPeer(const uint8_t *mac_addr) {
    std::string peerKey(reinterpret_cast<const char *>(mac_addr), ESP_NOW_ETH_ALEN);
    failedPeerCounts[peerKey]++;

    if (failedPeerCounts[peerKey] >= 3) { // Threshold for removing a peer
        ESP_LOGW(TAG, "Removing peer due to repeated send failures: MAC=" MACSTR, MAC2STR(mac_addr));
        esp_now_del_peer(mac_addr);
        failedPeerCounts.erase(peerKey);
    }
}

void Sender::sendCallback(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (!mac_addr) {
        ESP_LOGE(TAG, "Send callback error: null MAC address");
        return;
    }

    ESP_LOGI(TAG, "Send callback: MAC=" MACSTR ", status=%d", MAC2STR(mac_addr), status);

    // Reset failed count on successful send
    std::string peerKey(reinterpret_cast<const char *>(mac_addr), ESP_NOW_ETH_ALEN);
    if (status == ESP_NOW_SEND_SUCCESS) {
        failedPeerCounts.erase(peerKey);
    } else {
        ESP_LOGW(TAG, "Send failed: MAC=" MACSTR, MAC2STR(mac_addr));
        handleFailedPeer(mac_addr);
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

                    // Send a Registration Successful message back to the receiver
                    // Create an empty payload for the registration successful message
                    uint8_t emptyPayload[1] = {0};
                    
                    // Use the common prepareSendParams function to properly construct the message
                    auto *responseParams = new SendParams;
                    // Add only the specific peer that registered
                    responseParams->addDestination(recv_info->src_addr);
                    
                    // Set the payload type before preparing the message
                    responseParams->payload_type = PayloadType::RegistrationSuccessful;
                    
                    // Prepare the message with sequence number and CRC calculation
                    prepareSendParams(*responseParams, emptyPayload, 0);
                    
                    ESP_LOGI(TAG, "Preparing RegistrationSuccessful response");
                    
                    // Enqueue the response for sending
                    if (xQueueSend(outgoingMessageQueue, &responseParams, portMAX_DELAY) != pdTRUE) {
                        ESP_LOGE(TAG, "Failed to enqueue Registration Successful message");
                        delete responseParams;
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to add peer: MAC=" MACSTR, MAC2STR(recv_info->src_addr));
                }
            }
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

            // Check if we have any prepared messages
            if (sendParams->prepared_messages.empty()) {
                // No prepared messages - this could happen if destinations were added after preparation
                ESP_LOGW(TAG, "No prepared messages found. Preparing now.");
                
                // Check if destinations are specified
                if (sendParams->dest_macs.empty()) {
                    ESP_LOGW(TAG, "No destinations specified. Adding all peers.");
                    sendParams->addAllPeers();
                    
                    // If still empty, no peers are registered
                    if (sendParams->dest_macs.empty()) {
                        ESP_LOGW(TAG, "No peers to send to. Skipping message.");
                        delete sendParams;
                        continue;
                    }
                }
                
                // Since we don't have raw_data anymore, and messages are already prepared
                // or could not be prepared in the first place, we'll just log a warning and skip
                ESP_LOGW(TAG, "Cannot re-prepare messages without original payload. Skipping message send for payload type %d.", 
                         static_cast<int>(sendParams->payload_type));
                delete sendParams;
                continue;
            }
            
            // Get the message type for logging purposes - we already store it in the SendParams
            PayloadType payloadType = sendParams->payload_type;
            int sent_count = 0;
            
            // Send each pre-prepared message to its destination
            for (auto& preparedMsg : sendParams->prepared_messages) {
                // Verify the data is within ESP-NOW size limits
                if (preparedMsg.data.size() > ESP_NOW_MAX_DATA_LEN_V2) {
                    ESP_LOGE(TAG, "Message size %zu exceeds ESP-NOW limit of %d", 
                             preparedMsg.data.size(), ESP_NOW_MAX_DATA_LEN_V2);
                    continue;
                }
                
                // Send the pre-prepared message to its destination
                esp_err_t result = esp_now_send(preparedMsg.dest.address,
                                              preparedMsg.data.data(),
                                              preparedMsg.data.size());
                                              
                // Get the message info for logging
                MessageData* peerMsg = reinterpret_cast<MessageData*>(preparedMsg.data.data());
                
                if (result == ESP_OK) {
                    sent_count++;
                    ESP_LOGD(TAG, "Sent to " MACSTR ", type=%d, seq=%u", 
                             MAC2STR(preparedMsg.dest.address),
                             static_cast<int>(payloadType), 
                             peerMsg->seq_num);
                } else {
                    ESP_LOGW(TAG, "Failed to send to " MACSTR ": %s", 
                             MAC2STR(preparedMsg.dest.address),
                             esp_err_to_name(result));
                }
            }
            
            ESP_LOGI(TAG, "Sent message type %d to %d out of %zu peers", 
                     static_cast<int>(payloadType), sent_count, sendParams->prepared_messages.size());

            delete sendParams;
        }
    }
}

void Sender::prepareSendParams(SendParams &sendParams, const uint8_t *payload, size_t payload_len) {
    // Validate that a proper payload type has been specified
    if (sendParams.payload_type == PayloadType::Unspecified) {
        ESP_LOGE(TAG, "Cannot prepare messages with unspecified payload type");
        return;
    }
    
    // Log payload information
    ESP_LOGD(TAG, "Preparing messages for payload type: %d, length: %zu", static_cast<int>(sendParams.payload_type), payload_len);

    // Validate payload length
    if (payload_len > ESP_NOW_MAX_DATA_LEN_V2 - sizeof(MessageData)) {
        ESP_LOGE(TAG, "Payload length exceeds maximum allowed: %zu", payload_len);
        return;
    }

    // Calculate the total size needed for MessageData and the payload
    size_t messageDataSize = sizeof(MessageData) + payload_len;

    // Create a template message
    std::vector<uint8_t> messageTemplate(messageDataSize);
    MessageData *templateMsgData = reinterpret_cast<MessageData *>(messageTemplate.data());
    
    // Initialize template MessageData
    templateMsgData->seq_num = 0; // Will be replaced for each peer
    templateMsgData->crc = 0;     // Will be calculated for each peer
    templateMsgData->payload_type = static_cast<uint8_t>(sendParams.payload_type);
    
    ESP_LOGI(TAG, "Preparing message template for payload type: %d", templateMsgData->payload_type);

    // Copy the payload into the flexible array member
    if (payload_len > 0 && payload != nullptr) {
        memcpy(templateMsgData->payload, payload, payload_len);
    }
    
    // Clear any existing prepared messages
    sendParams.prepared_messages.clear();
    
    // Process each destination and prepare a message with sequence number and CRC
    for (const auto& dest : sendParams.dest_macs) {
        // Create a new message buffer for this destination
        std::vector<uint8_t> messageBuffer(messageTemplate);
        
        // Get a pointer to the message data for easier manipulation
        MessageData* peerMsg = reinterpret_cast<MessageData*>(messageBuffer.data());
        
        // Set the sequence number for this specific peer
        peerMsg->seq_num = getNextSequenceNumber(dest.address);
        
        // Calculate and set CRC
        peerMsg->crc = 0; // Reset CRC to 0 before calculation
        uint16_t calculatedCrc = esp_crc16_le(UINT16_MAX, 
                                             messageBuffer.data(), 
                                             messageDataSize);
        peerMsg->crc = calculatedCrc;
        
        // Log for registration successful messages before we move the buffer
        if (sendParams.payload_type == PayloadType::RegistrationSuccessful) {
            ESP_LOGI(TAG, "Prepared RegistrationSuccessful for " MACSTR " with seq=%u, CRC=%04X", 
                     MAC2STR(dest.address), peerMsg->seq_num, calculatedCrc);
        }
        
        // Create the prepared message with the destination and data
        PreparedMessage preparedMsg(dest, std::move(messageBuffer));
        
        // After this point, don't use peerMsg or messageBuffer as they've been moved

        // Add the prepared message to the vector
        sendParams.prepared_messages.push_back(std::move(preparedMsg));
    }
    
    ESP_LOGI(TAG, "Prepared %zu messages for sending", sendParams.prepared_messages.size());
}

void Sender::sendLoop(void *pvParameter) {
    ESP_LOGI(TAG, "Send loop task started");

    static uint8_t payload[128]; // Adjust size as needed

    while (true) {
        // Check if there are any registered peers
        esp_now_peer_num_t peerCount = {};
        esp_now_get_peer_num(&peerCount);

        if (peerCount.total_num == 0) {
            ESP_LOGD(TAG, "No registered peers. Skipping message queueing.");
            vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay before checking again
            continue;
        }

        esp_fill_random(payload, sizeof(payload));

        auto *sendParams = new SendParams;
        // Add all registered peers as destinations
        sendParams->addAllPeers();
        
        // Set the payload type
        sendParams->payload_type = PayloadType::ChangePattern;
        
        // This will prepare messages with proper sequence numbers and CRCs for all destinations
        prepareSendParams(*sendParams, payload, sizeof(payload));

        if (xQueueSend(outgoingMessageQueue, &sendParams, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to enqueue message");
            delete sendParams;
        }

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

        // Prepare the keepalive payload
        uint8_t keepalivePayload[1] = {0}; // Minimal payload for keepalive

        auto *sendParams = new SendParams;
        // Add all registered peers as destinations
        sendParams->addAllPeers();
        
        // Set the payload type
        sendParams->payload_type = PayloadType::Keepalive;
        
        // Prepare messages with sequence numbers and CRCs for all destinations
        prepareSendParams(*sendParams, keepalivePayload, sizeof(keepalivePayload));

        if (xQueueSend(outgoingMessageQueue, &sendParams, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to enqueue keepalive message");
            delete sendParams;
        }

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
