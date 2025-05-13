#include "ESPNOWSender.h"
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

static const char *TAG = "ESPNOWSender";

static QueueHandle_t espnowSendQueue = nullptr;
static std::unordered_map<std::string, uint16_t> peerSequenceNumbers; // Sequence numbers per peer

uint16_t ESPNOWSender::getNextSequenceNumber(const uint8_t *mac_addr) {
    std::string peerKey(reinterpret_cast<const char *>(mac_addr), ESP_NOW_ETH_ALEN);
    return ++peerSequenceNumbers[peerKey];
}

esp_err_t ESPNOWSender::init() {
    esp_log_level_set(TAG, SENDER_LOG_LEVEL);
    ESP_LOGI(TAG, "Initializing ESPNOW Sender");

    // Create a queue for ESPNOW events
    espnowSendQueue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(ESPNOWSendParams*));
    if (!espnowSendQueue) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_FAIL;
    }

    // Register send and receive callbacks
    ESP_ERROR_CHECK(esp_now_register_send_cb(ESPNOWSender::sendCallback));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(ESPNOWSender::recvCallback));

    // Add broadcast peer
    if (!esp_now_is_peer_exist(broadcastMac)) {
        esp_now_peer_info_t peerInfo = {};
        peerInfo.channel = CONFIG_ESPNOW_CHANNEL;
        peerInfo.ifidx = static_cast<wifi_interface_t>(ESPNOW_WIFI_IF);
        peerInfo.encrypt = false;
        std::memcpy(peerInfo.peer_addr, broadcastMac, ESP_NOW_ETH_ALEN);
        ESP_ERROR_CHECK(esp_now_add_peer(&peerInfo));
    } else {
        ESP_LOGW(TAG, "Broadcast peer already exists");
    }

    // Start the testing loop task
    xTaskCreate(sendLoop, "sendLoop", 15000, nullptr, 4, nullptr);
    xTaskCreate(processQueue, "processQueue", 2048, nullptr, 4, nullptr);

    return ESP_OK;
}

void ESPNOWSender::sendCallback(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (!mac_addr) {
        ESP_LOGE(TAG, "Send callback error: null MAC address");
        return;
    }
    ESP_LOGI(TAG, "Send callback: MAC= " MACSTR ", status=%d",
             MAC2STR(mac_addr),
             status);
}

void ESPNOWSender::recvCallback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (!recv_info || !data || len <= 0) {
        ESP_LOGE(TAG, "Receive callback error: invalid arguments");
        return;
    }
    ESP_LOGI(TAG, "Receive callback: MAC=" MACSTR ", len=%d",
             MAC2STR(recv_info->src_addr), len);
}



void ESPNOWSender::processQueue(void *pvParameter) {
    ESP_LOGI(TAG, "Processing queue task started");

    while (true) {
        // Log before calling xQueueReceive
        UBaseType_t queueItems = uxQueueMessagesWaiting(espnowSendQueue);
        ESP_LOGD(TAG, "Queue items before receive: %u", queueItems);

        ESPNOWSendParams *sendParams;
        if (xQueueReceive(espnowSendQueue, &sendParams, portMAX_DELAY) == pdTRUE) {
            // Log immediately after dequeuing sendParams
            ESP_LOGD(TAG, "Dequeued sendParams: %p", sendParams);

            // Validate sendParams pointer
            if (!sendParams) {
                ESP_LOGE(TAG, "Dequeued null sendParams");
                continue;
            }

            // Validate sendParams and its fields
            if (!sendParams ) {
                ESP_LOGE(TAG, "Invalid sendParams");
                continue;
            }

            // Log the contents of sendParams for debugging
            ESP_LOGD(TAG, "Processing sendParams: dest_mac=" MACSTR ", data_len=%d", MAC2STR(sendParams->dest_mac), sendParams->data_len);
            ESP_LOG_BUFFER_HEXDUMP(TAG, sendParams->raw_data, sendParams->data_len > 16 ? 16 : sendParams->data_len, ESP_LOG_DEBUG);

            // Validate sendParams fields
            if (!sendParams || sendParams->data_len <= 0 || sendParams->data_len > ESP_NOW_MAX_DATA_LEN_V2) {
                ESP_LOGE(TAG, "Invalid sendParams or data_len: %d", sendParams ? sendParams->data_len : -1);
                delete sendParams;
                continue;
            }

            // Validate data length before sending
            if (sendParams->data_len <= 0 || sendParams->data_len > ESP_NOW_MAX_DATA_LEN_V2) {
                ESP_LOGE(TAG, "Invalid data length: %d", sendParams->data_len);
                delete sendParams;
                continue;
            }

            ESP_LOGI(TAG, "Sending data to MAC=" MACSTR ", len=%d",
                     MAC2STR(sendParams->dest_mac),
                     sendParams->data_len);

            esp_err_t result = esp_now_send(sendParams->dest_mac, reinterpret_cast<uint8_t *>(sendParams->raw_data), sendParams->data_len);
            if (result == ESP_OK) {
                ESP_LOGI(TAG, "Data sent successfully");
            } else {
                ESP_LOGE(TAG, "Error sending data: %s", esp_err_to_name(result));
            }

            delete sendParams; // Free the allocated memory for sendParams
        }
    }
}

void ESPNOWSender::prepareSendParams(ESPNOWSendParams &sendParams, const uint8_t *payload, size_t payload_len, ESPNOWPayloadType payload_type) {
    // Log payload length and buffer sizes
    ESP_LOGD(TAG, "Payload length: %zu, raw_data size: %zu", payload_len, sizeof(sendParams.raw_data));

    // Validate payload length
    if (payload_len > ESP_NOW_MAX_DATA_LEN_V2 - sizeof(ESPNOWMessageData)) {
        ESP_LOGE(TAG, "Payload length exceeds maximum allowed: %zu", payload_len);
        return;
    }

    // Calculate the total size needed for ESPNOWMessageData and the payload
    size_t messageDataSize = sizeof(ESPNOWMessageData) + payload_len;

    // Dynamically allocate memory for ESPNOWMessageData and its payload
    ESPNOWMessageData *messageData = reinterpret_cast<ESPNOWMessageData *>(malloc(messageDataSize));
    if (!messageData) {
        ESP_LOGE(TAG, "Failed to allocate memory for ESPNOWMessageData");
        return;
    }

    // Initialize the fixed fields of ESPNOWMessageData
    messageData->seq_num = getNextSequenceNumber(sendParams.dest_mac);
    messageData->payload_type = static_cast<uint8_t>(payload_type);

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

    // Set the destination MAC address
    memcpy(sendParams.dest_mac, broadcastMac, ESP_NOW_ETH_ALEN);
}

void ESPNOWSender::sendLoop(void *pvParameter) {
    ESP_LOGI(TAG, "Send loop task started");

    // Define a static buffer for the payload to avoid dynamic memory allocation
    static uint8_t payload[128]; // Adjust size as needed

    while (true) {
        // Fill the payload with random data
        esp_fill_random(payload, sizeof(payload));

        // Dynamically allocate sendParams to ensure it remains valid
        auto *sendParams = new ESPNOWSendParams;

        // Prepare the send parameters
        prepareSendParams(*sendParams, payload, sizeof(payload), ESPNOWPayloadType::ChangePattern);

         // Send the prepared parameters to the queue
        if (xQueueSend(espnowSendQueue, &sendParams, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send data to queue");
            delete sendParams; // Free memory if queueing fails
        } else {
            ESP_LOGD(TAG, "Data queued successfully");
        }

        // Delay for 1 second before sending the next message
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
