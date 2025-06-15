#ifndef MESSAGES_H
#define MESSAGES_H

#include <cstdint>
#include <memory>
#include <cstring>
#include "esp_now.h"
#include "esp_log.h"
#include <variant>
#include <vector>
#include "Manager.h"
#include <string>

// Define pattern types as enums
enum class PatternType : uint8_t {
    NoPattern = 0,
    FullBarPattern = 1,
    FullBarBallPattern = 2,
    BallPattern = 3,
    RainbowPattern = 4,
    FirePattern = 5,
    SparklePattern = 6,
    WavePattern = 7,
    // Add more patterns as needed
};

// Define the payload types
struct ChangePatternPayload {
    PatternType patternType; // Type of pattern to change to
    
    // Serialize the payload into a vector of bytes
    std::vector<uint8_t> serialize() const {
        // Simply serialize the enum value as a single byte
        return {static_cast<uint8_t>(patternType)};
    }
};

struct ChangeBrightnessPayload {
    uint8_t brightnessLevel; // Brightness level (0-100)
    
    // Serialize the payload into a vector of bytes
    std::vector<uint8_t> serialize() const {
        return {brightnessLevel};
    }
};

struct ChangeHuePayload {
    uint8_t index; // Index of the hue to change (0-7)
    uint16_t hueVal; // hue value 0-360
    
    // Serialize the payload into a vector of bytes
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> result;
        result.push_back(index);                 // Index byte
        result.push_back(hueVal & 0xFF);         // Low byte of hue
        result.push_back((hueVal >> 8) & 0xFF);  // High byte of hue
        return result;
    }
};

struct ChangeSpeedPayload {
    uint8_t speedLevel; // Speed level (0-100)
    
    // Serialize the payload into a vector of bytes
    std::vector<uint8_t> serialize() const {
        return {speedLevel};
    }
};

struct EffectPunchPayload {
    uint8_t intensity; // Punch effect intensity (0-100)
    
    // Serialize the payload into a vector of bytes
    std::vector<uint8_t> serialize() const {
        return {intensity};
    }
};

// Payload type that receivers broadcast to register themselves with the sender
struct RegisterRequestPayload {
    // Serialize the payload into a vector of bytes
    std::vector<uint8_t> serialize() const {
        // Empty payload, return minimum required byte
        return {0};
    }
};

struct RegistrationSuccessfulPayload {
    // Serialize the payload into a vector of bytes
    std::vector<uint8_t> serialize() const {
        // Empty payload, return minimum required byte
        return {0};
    }
};

struct KeepalivePayload {
    // Serialize the payload into a vector of bytes
    std::vector<uint8_t> serialize() const {
        // Empty payload, return minimum required byte
        return {0};
    }
}; // Minimal payload for keepalive messages

static constexpr uint8_t broadcastMac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define IS_BROADCAST_ADDR(addr) (memcmp(addr, broadcastMac, ESP_NOW_ETH_ALEN) == 0)

enum {
    ESPNOW_DATA_BROADCAST,
    ESPNOW_DATA_UNICAST,
    ESPNOW_DATA_MAX,
};

// Define a variant to hold different payload types
using Payload = std::variant<EffectPunchPayload, ChangePatternPayload, ChangeBrightnessPayload, ChangeHuePayload, ChangeSpeedPayload, RegisterRequestPayload, RegistrationSuccessfulPayload, KeepalivePayload>;

enum class PayloadType : uint8_t {
    RegisterPeer,
    EffectPunch,
    ChangePattern,
    ChangeBrightness,
    ChangeHue,
    ChangeSpeed,
    RegisterRequest,
    RegistrationSuccessful,
    Keepalive
};

// Message contains the payload as well as potentially the parsed payload.
// This struct is used both for sending and receiving messages and various fields
// might be empty depending on the context. 
struct Message {
    uint8_t type;                    // Type of the message (e.g., broadcast or unicast)
    PayloadType payload_type;  // Type of the payload
    Payload parsed_payload;    // Parsed payload as a variant
};

// MessageData is the raw message going over the wire/air.
struct MessageData {
    uint16_t seq_num;                     //Sequence number of ESPNOW data.
    uint16_t crc;                         //CRC16 value of ESPNOW data.
    uint8_t payload_type;                  //Payload type of ESPNOW data.
    uint8_t payload[];                    //Real payload of ESPNOW data.
} __attribute__((packed));
// The __attribute__((packed)) directive is used to ensure that the struct is packed without padding

struct MessageEnvelope {
    uint8_t src_mac[ESP_NOW_ETH_ALEN]; // MAC address of the source device
    uint8_t *data;                     // Raw received data
    size_t data_len;                   // Actual length of the received data

    // Constructor to allocate memory for data
    MessageEnvelope(size_t len) : data_len(len) {
        data = new uint8_t[len];
    }

    // Destructor to free allocated memory
    ~MessageEnvelope() {
        delete[] data;
    }
};

struct SendParams {
    uint8_t raw_data[ESP_NOW_MAX_DATA_LEN_V2]; // Store raw data directly
     // Destination MAC address. Only used if we're sending to a specific peer. It will be
     // empty in most cases.
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];      
    size_t data_len;                        // Actual length of the data

    SendParams() : raw_data{0}, dest_mac{0}, data_len(0) {}
};

#endif // MESSAGES_H