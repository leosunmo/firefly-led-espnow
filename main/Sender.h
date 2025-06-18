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
#include "InputManager.h" // Use InputManager for all input devices
#include "PayloadHelper.h"

// Forward declaration for queue handle
extern QueueHandle_t outgoingMessageQueue;

class Sender {
public:
    // Delete copy constructor and assignment operator
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;
    
    // Static method to get the singleton instance
    static Sender& getInstance();
    
    // Initialize the sender
    esp_err_t init();
    
    // Helper template methods for sending payloads
    template<typename T>
    void sendPayload(const T& payload, PayloadType payloadType, const uint8_t* destMac = nullptr);
    
    // Convenience methods for sending common payloads
    void sendPatternChange(PatternType patternType, const uint8_t* destMac = nullptr);
    void sendBrightnessChange(uint8_t brightness, const uint8_t* destMac = nullptr);
    void sendHueChange(uint8_t index, uint16_t hue, const uint8_t* destMac = nullptr);
    void sendSpeedChange(uint8_t speed, const uint8_t* destMac = nullptr);
    void sendEffectPunch(uint8_t intensity, const uint8_t* destMac = nullptr);
    void sendKeepaliveMessage(const uint8_t* destMac = nullptr);
    void sendRegistrationResponse(const uint8_t* destMac);
    
    // Send all current settings to a newly connected peer
    void sendCurrentSettings(const uint8_t* destMac);
    
    // Send a message to all registered peers with correct sequence numbers
    bool sendToAllPeers(const uint8_t* payload, size_t payload_len, PayloadType payload_type);

private:
    // Private constructor and destructor for singleton
    Sender();
    ~Sender();
    
    // Set up input handlers for all devices
    void setupInputHandlers();
    
    // Static values for ESP-NOW
    static constexpr size_t ESPNOW_MAX_PEER_FAIL = 2; // Maximum failed sends before dropping a peer

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

// Template method implementation
template<typename T>
void Sender::sendPayload(const T& payload, PayloadType payloadType, const uint8_t* destMac) {
    std::vector<uint8_t> serializedPayload = PayloadHelper::serialize(payload);
    
    // If destMac is nullptr, use the special send-to-all method with proper sequencing
    if (destMac == nullptr) {
        sendToAllPeers(serializedPayload.data(), serializedPayload.size(), payloadType);
    } else {
        // Otherwise, use the standard message queue approach for a specific peer
        auto* params = new SendParams;
        std::memcpy(params->dest_mac, destMac, ESP_NOW_ETH_ALEN);
        prepareSendParams(*params, serializedPayload.data(), serializedPayload.size(), payloadType);
        xQueueSend(outgoingMessageQueue, &params, portMAX_DELAY);
    }
}

#endif // SENDER_H