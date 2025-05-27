#ifndef RECEIVER_H
#define RECEIVER_H

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_now.h"
#include "Messages.h"
#include <unordered_map>

class Receiver {
public:
    static void init();
    static void broadcastRegistration(void *pvParameter);

private:
    static void recvCallback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
    static void recvLoop(void *pvParameter);
    static int parseESPNOWData(const uint8_t *data, uint16_t data_len, const uint8_t *src_addr, Message *message);
    static void checkKeepalive(void *pvParameter);

    static std::unordered_map<std::string, uint16_t> peerLastSequenceNumbers; // Last received sequence numbers per peer
    static volatile bool isRegistered;
};

#endif // RECEIVER_H