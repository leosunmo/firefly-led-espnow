#ifndef MESSAGES_H
#define MESSAGES_H

#include <cstdint>
#include <cstring>
#include <algorithm>
#include "esp_now.h"
#include "esp_log.h"
#include "Manager.h"

// Maximum number of peers supported by ESP-NOW (limited by ESP-NOW library)
#define MAX_ESP_NOW_PEERS 20

// Maximum length for pattern name
#define MAX_PATTERN_NAME_LENGTH 16

// Define the payload types
struct ChangePatternPayload {
    char patternName[MAX_PATTERN_NAME_LENGTH]; // Name of the pattern to change to (fixed size)
};

struct ChangeBrightnessPayload {
    uint8_t brightnessLevel; // Brightness level (0-255)
};

// Payload type that receivers broadcast to register themselves with the sender
struct RegisterRequestPayload {
    // Empty struct, just a marker
};

struct RegistrationSuccessfulPayload {
    // Empty struct, just a marker
};

struct KeepalivePayload {
    // Empty struct, just a marker
};

static constexpr uint8_t broadcastMac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define IS_BROADCAST_ADDR(addr) (memcmp(addr, broadcastMac, ESP_NOW_ETH_ALEN) == 0)

enum {
    ESPNOW_DATA_BROADCAST,
    ESPNOW_DATA_UNICAST,
    ESPNOW_DATA_MAX,
};

// Define payload types as an enum
enum class PayloadType : uint8_t {
    Unspecified,       // Used as a default/sentinel value
    RegisterPeer,
    ChangePattern,
    ChangeBrightness,
    RegisterRequest,
    RegistrationSuccessful,
    Keepalive
};

// More efficient union-based payload instead of std::variant
union PayloadUnion {
    ChangePatternPayload changePattern;
    ChangeBrightnessPayload changeBrightness;
    RegisterRequestPayload registerRequest;
    RegistrationSuccessfulPayload registrationSuccessful;
    KeepalivePayload keepalive;

    // Default constructor needed for unions with non-trivial members
    PayloadUnion() {
        // Initialize with zeros to ensure clean state
        memset(this, 0, sizeof(PayloadUnion));
    }
};

// Message contains the payload type and the parsed payload.
// This struct is used both for sending and receiving messages
struct Message {
    uint8_t type;                // Type of the message (e.g., broadcast or unicast)
    PayloadType payload_type;    // Type of the payload
    PayloadUnion payload;        // The actual payload data as a union
};

// Maximum payload size for ESP-NOW
#define MAX_ESP_NOW_PAYLOAD_SIZE (250 - sizeof(uint16_t) - sizeof(uint16_t) - sizeof(uint8_t))

// MessageData is the raw message going over the wire/air.
struct MessageData {
    uint16_t seq_num;       // Sequence number of ESPNOW data
    uint16_t crc;           // CRC16 value of ESPNOW data
    uint8_t payload_type;   // Payload type of ESPNOW data
    uint8_t payload[MAX_ESP_NOW_PAYLOAD_SIZE]; // Fixed-size buffer for payload
} __attribute__((packed));
// The __attribute__((packed)) directive is used to ensure that the struct is packed without padding

struct MessageEnvelope {
    uint8_t src_mac[ESP_NOW_ETH_ALEN];     // MAC address of the source device
    uint8_t data[ESP_NOW_MAX_DATA_LEN_V2]; // Fixed-size buffer for data (eliminates dynamic allocation)
    size_t data_len;                       // Actual length of the received data
    
    // Simple constructor without dynamic allocation
    MessageEnvelope() : data_len(0) {
        memset(src_mac, 0, ESP_NOW_ETH_ALEN);
        memset(data, 0, ESP_NOW_MAX_DATA_LEN_V2);
    }
};

// Simple typedef for MAC address to improve readability
typedef uint8_t mac_addr_t[ESP_NOW_ETH_ALEN];

// Helper function to copy a MAC address
inline void copy_mac_addr(mac_addr_t dest, const uint8_t* src) {
    memcpy(dest, src, ESP_NOW_ETH_ALEN);
}

// Helper function to compare MAC addresses
inline bool mac_addr_equal(const mac_addr_t addr1, const mac_addr_t addr2) {
    return memcmp(addr1, addr2, ESP_NOW_ETH_ALEN) == 0;
}

// Structure for storing prepared messages with precalculated CRCs and sequence numbers
struct PreparedMessage {
    mac_addr_t dest;                         // Destination MAC address
    uint8_t data[ESP_NOW_MAX_DATA_LEN_V2];   // Fixed buffer for message data
    size_t data_len;                         // Actual length of data
    
    // Default constructor
    PreparedMessage() : data_len(0) {
        memset(dest, 0, ESP_NOW_ETH_ALEN);
        memset(data, 0, ESP_NOW_MAX_DATA_LEN_V2);
    }
    
    // Initialize with destination and data
    void init(const uint8_t* destination, const uint8_t* message_data, size_t len) {
        copy_mac_addr(dest, destination);
        if (len <= ESP_NOW_MAX_DATA_LEN_V2) {
            memcpy(data, message_data, len);
            data_len = len;
        }
    }
};

struct SendParams {
    mac_addr_t dest_macs[MAX_ESP_NOW_PEERS];      // Fixed array of destination MAC addresses
    PreparedMessage prepared_messages[MAX_ESP_NOW_PEERS]; // Fixed array of prepared messages
    uint8_t dest_count;                           // Number of destinations
    uint8_t message_count;                        // Number of prepared messages
    PayloadType payload_type;                     // Type of payload in this message batch
    
    SendParams() : dest_count(0), message_count(0), payload_type(PayloadType::Unspecified) {}
    
    // Add a destination MAC to the list
    bool addDestination(const uint8_t mac_addr[ESP_NOW_ETH_ALEN]) {
        if (dest_count < MAX_ESP_NOW_PEERS) {
            copy_mac_addr(dest_macs[dest_count++], mac_addr);
            return true;
        }
        return false;
    }
    
    // Add all currently registered peers as destinations
    void addAllPeers() {
        esp_now_peer_info_t peerInfo = {};
        bool from_head = true;
        dest_count = 0;
        
        while (esp_now_fetch_peer(from_head, &peerInfo) == ESP_OK && dest_count < MAX_ESP_NOW_PEERS) {
            copy_mac_addr(dest_macs[dest_count++], peerInfo.peer_addr);
            from_head = false;
        }
    }
    
    // Add a prepared message
    bool addPreparedMessage(const PreparedMessage& msg) {
        if (message_count < MAX_ESP_NOW_PEERS) {
            memcpy(&prepared_messages[message_count++], &msg, sizeof(PreparedMessage));
            return true;
        }
        return false;
    }
};

#endif // MESSAGES_H