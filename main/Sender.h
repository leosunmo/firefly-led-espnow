#ifndef SENDER_H
#define SENDER_H

#include <cstdint>
#include <unordered_map>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_now.h"
#include "Messages.h"
#include "Manager.h"

class Sender {
public:
    static esp_err_t init();

private:
    static void sendLoop(void *pvParameter);
    static void sendCallback(const uint8_t *mac_addr, esp_now_send_status_t status);
    static void recvCallback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
    static void prepareSendParams(SendParams &sendParams, const uint8_t *payload, size_t payload_len, PayloadType payload_type);
    static uint16_t getNextSequenceNumber(const uint8_t *mac_addr);
    static void processOutgoingMessages(void *pvParameter);
    static void logRegisteredPeers();
    static void sendKeepalive(void *pvParameter);
};

#endif // SENDER_H