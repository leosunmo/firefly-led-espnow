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
                    size_t totalSize = sizeof(MessageData);

                    auto *response = reinterpret_cast<MessageData *>(malloc(totalSize));
                    if (!response) {
                        ESP_LOGE(TAG, "Failed to allocate memory for Registration Successful message");
                        return;
                    }

                    response->seq_num = getNextSequenceNumber(recv_info->src_addr);
                    response->payload_type = static_cast<uint8_t>(PayloadType::RegistrationSuccessful);

                    // Enqueue the response instead of sending it directly
                    auto *responseParams = new SendParams;
                    memcpy(responseParams->dest_mac, recv_info->src_addr, ESP_NOW_ETH_ALEN);
                    memcpy(responseParams->raw_data, response, totalSize);
                    responseParams->data_len = totalSize;

                    if (xQueueSend(outgoingMessageQueue, &responseParams, portMAX_DELAY) != pdTRUE) {
                        ESP_LOGE(TAG, "Failed to enqueue Registration Successful message");
                        delete responseParams;
                    }

                    free(response);
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

            // Get number of peers registered
            esp_err_t result = esp_now_send(nullptr, sendParams->raw_data, sendParams->data_len);
            if (result == ESP_OK) {
                ESP_LOGI(TAG, "Message sent successfully to %d receivers", peerCount.total_num);
            } else {
                ESP_LOGE(TAG, "Failed to send message error=%s", esp_err_to_name(result));
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
        prepareSendParams(*sendParams, payload, sizeof(payload), PayloadType::ChangePattern);

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
        prepareSendParams(*sendParams, keepalivePayload, sizeof(keepalivePayload), PayloadType::Keepalive);

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
