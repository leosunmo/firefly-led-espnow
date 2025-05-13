#ifndef ESPNOW_RECEIVER_H
#define ESPNOW_RECEIVER_H

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_now.h"
#include "ESPNOWMessages.h"
#include <unordered_map>

class ESPNOWReceiver {
public:
    static void init();

private:
    static void recvCallback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
    static void recvLoop(void *pvParameter);
    static int parseESPNOWData(const uint8_t *data, uint16_t data_len, const uint8_t *src_addr, ESPNOWMessage *message);

    static std::unordered_map<std::string, uint16_t> peerLastSequenceNumbers; // Last received sequence numbers per peer
};

#endif // ESPNOW_RECEIVER_H