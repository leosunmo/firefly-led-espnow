#ifndef ESPNOW_MESSAGES_H
#define ESPNOW_MESSAGES_H

#include <cstdint>
#include <memory>
#include <cstring>
#include "esp_now.h"
#include "esp_log.h"
#include <variant>
#include <vector>
#include "ESPNOWManager.h"
#include <string>

// Define the payload types
struct ChangePatternPayload {
    std::string patternName; // Name of the pattern to change to
};

struct ChangeBrightnessPayload {
    uint8_t brightnessLevel; // Brightness level (0-255)
};

static constexpr uint8_t broadcastMac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define IS_BROADCAST_ADDR(addr) (memcmp(addr, broadcastMac, ESP_NOW_ETH_ALEN) == 0)

enum {
    ESPNOW_DATA_BROADCAST,
    ESPNOW_DATA_UNICAST,
    ESPNOW_DATA_MAX,
};

// Define a variant to hold different payload types
using ESPNOWPayload = std::variant<ChangePatternPayload, ChangeBrightnessPayload>;

enum class ESPNOWPayloadType {
    ChangePattern,
    ChangeBrightness
};

// ESPNOWMessage contains the payload as well as potentially the parsed payload.
// This struct is used both for sending and receiving messages and various fields
// might be empty depending on the context. 
struct ESPNOWMessage {
    uint8_t type;                   // Type of the message (e.g., broadcast or unicast)
    ESPNOWPayloadType payload_type;  // Type of the payload
    ESPNOWPayload parsed_payload;    // Parsed payload as a variant
};

// ESPNOWMessageData is the raw message going over the wire/air.
struct ESPNOWMessageData {
    uint16_t seq_num;                     //Sequence number of ESPNOW data.
    uint16_t crc;                         //CRC16 value of ESPNOW data.
    uint8_t payload_type;                  //Payload type of ESPNOW data.
    uint8_t payload[];                    //Real payload of ESPNOW data.
} __attribute__((packed));
// The __attribute__((packed)) directive is used to ensure that the struct is packed without padding

struct ESPNOWMessageEnvelope {
    uint8_t src_mac[ESP_NOW_ETH_ALEN]; // MAC address of the source device
    uint8_t *data;                     // Raw received data
    size_t data_len;                   // Actual length of the received data

    // Constructor to allocate memory for data
    ESPNOWMessageEnvelope(size_t len) : data_len(len) {
        data = new uint8_t[len];
    }

    // Destructor to free allocated memory
    ~ESPNOWMessageEnvelope() {
        delete[] data;
    }
};

struct ESPNOWSendParams {
    uint8_t raw_data[ESP_NOW_MAX_DATA_LEN_V2]; // Store raw data directly
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];
    size_t data_len;                        // Actual length of the data

    ESPNOWSendParams() : raw_data{0}, dest_mac{0}, data_len(0) {}
};

#endif // ESPNOW_MESSAGES_H