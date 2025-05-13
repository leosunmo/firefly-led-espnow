#ifndef EXAMPLE_ESPNOW_SENDER_H
#define EXAMPLE_ESPNOW_SENDER_H

#include <cstdint>
#include <unordered_map>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_now.h"
#include "ESPNOWMessages.h"
#include "ESPNOWManager.h"

class ESPNOWSender {
public:
    static esp_err_t init();

private:
    static void sendLoop(void *pvParameter);
    static void sendCallback(const uint8_t *mac_addr, esp_now_send_status_t status);
    static void recvCallback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
    static void prepareSendParams(ESPNOWSendParams &sendParams, const uint8_t *payload, size_t payload_len, ESPNOWPayloadType payload_type);
    static void processQueue(void *pvParameter);
    static uint16_t getNextSequenceNumber(const uint8_t *mac_addr);
};

#endif // EXAMPLE_ESPNOW_SENDER_H