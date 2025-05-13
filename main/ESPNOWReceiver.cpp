#include "ESPNOWReceiver.h"
#include "ESPNOWMessages.h"
#include "ESPNOWManager.h"
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

static const char *TAG = "ESPNOWReceiver";

static QueueHandle_t espnowReceiveQueue = nullptr;
std::unordered_map<std::string, uint16_t> ESPNOWReceiver::peerLastSequenceNumbers; // Last received sequence numbers per peer

void ESPNOWReceiver::init() {
    esp_log_level_set(TAG, RECEIVER_LOG_LEVEL);
    ESP_LOGI(TAG, "Initializing ESPNOW Receiver");

    // Create a queue for ESPNOWMessageEnvelope.
    espnowReceiveQueue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(ESPNOWMessageEnvelope));
    if (!espnowReceiveQueue) {
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
}

void ESPNOWReceiver::recvCallback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (!recv_info || !data || len <= 0) {
        ESP_LOGE(TAG, "Receive callback error: invalid arguments");
        return;
    }

    if (len > ESP_NOW_MAX_DATA_LEN_V2) { // Use the maximum allowed data length
        ESP_LOGE(TAG, "Received data length exceeds buffer size: len=%d", len);
        return;
    }

    ESP_LOGI(TAG, "Received ESPNOW data from MAC= " MACSTR ", len=%d",
             MAC2STR(recv_info->src_addr), len);

    // Create a ESPNOWMessageEnvelope object with the correct data length
    auto *receivedMessage = new ESPNOWMessageEnvelope(len);
    std::memcpy(receivedMessage->src_mac, recv_info->src_addr, ESP_NOW_ETH_ALEN);
    std::memcpy(receivedMessage->data, data, len);

    // Send the message to the queue
    if (xQueueSend(espnowReceiveQueue, &receivedMessage, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue received message");
        delete receivedMessage;
    }
}

void ESPNOWReceiver::recvLoop(void *pvParameter) {
    ESP_LOGI(TAG, "Receive loop task started");

    while (true) {
        ESPNOWMessageEnvelope *recvMsg;
        if (xQueueReceive(espnowReceiveQueue, &recvMsg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Processing received data from MAC= " MACSTR ", len=%d",
                     MAC2STR(recvMsg->src_mac), recvMsg->data_len);

            // Create a new ESPNOWMessage object
            ESPNOWMessage* message = new ESPNOWMessage();
            if (!message) {
                ESP_LOGE(TAG, "Failed to allocate memory for ESPNOWMessage");
                delete recvMsg; // Free the received message
                continue;
            }

            // Parse the received data
            int type = parseESPNOWData(recvMsg->data, recvMsg->data_len, recvMsg->src_mac, message);
            if (type < 0) {
                ESP_LOGE(TAG, "Failed to parse ESPNOW data");
                delete message;
                delete recvMsg; // Free the received message
                continue;
            }

            // Set the message type based on the received MAC address
            message->type = IS_BROADCAST_ADDR(recvMsg->src_mac) ? ESPNOW_DATA_BROADCAST : ESPNOW_DATA_UNICAST;

            // TODO: Process the parsed message
            ESP_LOGI(TAG, "Parsed ESPNOW message: type=%d",
                     static_cast<int>(message->payload_type));

            // Free the allocated message and received message
            delete message;
            delete recvMsg;
        }
    }
}

int ESPNOWReceiver::parseESPNOWData(const uint8_t *data, uint16_t data_len, const uint8_t *src_addr, ESPNOWMessage *message) {
    if (!data || data_len < sizeof(ESPNOWMessageData) || !message) {
        ESP_LOGE(TAG, "Received ESPNOW data too short, null, or invalid message pointer, len:%d", data_len);
        return -1;
    }

    // Cast the raw data to ESPNOWMessageData
    const ESPNOWMessageData *rawMessage = reinterpret_cast<const ESPNOWMessageData *>(data);

    // Ensure the payload pointer is valid and within bounds
    if (data_len < sizeof(ESPNOWMessageData) + rawMessage->payload_type) {
        ESP_LOGE(TAG, "Data length is insufficient for payload");
        return -1;
    }

    // Allocate memory for messageCopy to include the payload
    ESPNOWMessageData *messageCopy = reinterpret_cast<ESPNOWMessageData *>(malloc(data_len));
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

    // Populate the ESPNOWMessage
    message->payload_type = static_cast<ESPNOWPayloadType>(rawMessage->payload_type);

    // Check the sequence number against the last received sequence number for this peer
    std::string peerKey(reinterpret_cast<const char *>(src_addr), ESP_NOW_ETH_ALEN);
    uint16_t &lastSeqNum = peerLastSequenceNumbers[peerKey];
    if (rawMessage->seq_num <= lastSeqNum) {
        ESP_LOGW(TAG, "Discarding message with sequence number %d (last: %d) from peer %s", rawMessage->seq_num, lastSeqNum, peerKey.c_str());
        return -1;
    }
    lastSeqNum = rawMessage->seq_num;

    // Parse the payload based on the payload type
    size_t payloadSize = data_len - sizeof(ESPNOWMessageData);
    switch (message->payload_type) {
        case ESPNOWPayloadType::ChangePattern: {
            if (payloadSize < sizeof(ChangePatternPayload)) {
                ESP_LOGE(TAG, "Payload size mismatch for ChangePatternPayload");
                return -1;
            }
            // Deserialize the payload properly
            ChangePatternPayload payload;
            std::string patternName(reinterpret_cast<const char *>(rawMessage->payload), payloadSize);
            payload.patternName = patternName;
            message->parsed_payload = payload;
            break;
        }
        case ESPNOWPayloadType::ChangeBrightness: {
            if (payloadSize < sizeof(ChangeBrightnessPayload)) {
                ESP_LOGE(TAG, "Payload size mismatch for ChangeBrightnessPayload");
                return -1;
            }
            ChangeBrightnessPayload payload;
            std::memcpy(&payload, rawMessage->payload, sizeof(ChangeBrightnessPayload));
            message->parsed_payload = payload;
            break;
        }
        default:
            ESP_LOGE(TAG, "Unknown payload type: %d", rawMessage->payload_type);
            return -1;
    }

    return 0;
}
