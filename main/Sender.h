#ifndef SENDER_H
#define SENDER_H

#include <cstdint>
#include <unordered_map>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_now.h"
#include "Messages.h"
#include "Manager.h"
#include "Button.h"

class Sender {
public:
    // Delete copy constructor and assignment operator
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;
    
    // Static method to get the singleton instance
    static Sender& getInstance();
    
    // Initialize the sender
    esp_err_t init();

private:
    // Private constructor and destructor for singleton
    Sender();
    ~Sender();
    
    // Button event handlers
    void handleButtonEvent(const std::string& buttonName, Button::Event event);
    
    // Button instances
    std::unique_ptr<Button> blueButton;
    std::unique_ptr<Button> redButton;
    
    // Static methods for ESP-NOW
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