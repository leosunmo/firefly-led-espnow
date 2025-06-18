#pragma once

#include "Messages.h"
#include <memory>
#include <vector>
#include <variant>

/**
 * @brief Helper class for payload serialization and deserialization
 */
class PayloadHelper {
public:
    /**
     * @brief Serialize any payload type into a byte vector
     * 
     * @tparam T The payload type
     * @param payload The payload to serialize
     * @return std::vector<uint8_t> The serialized payload as bytes
     */
    template<typename T>
    static std::vector<uint8_t> serialize(const T& payload) {
        return payload.serialize();
    }
    
    /**
     * @brief Deserialize a byte array into a ChangePatternPayload
     * 
     * @param data Pointer to the serialized data
     * @param len Length of the serialized data
     * @return ChangePatternPayload The deserialized payload
     */
    static ChangePatternPayload deserializePatternPayload(const uint8_t* data, size_t len) {
        ChangePatternPayload payload;
        if (data && len > 0) {
            payload.patternType = static_cast<PatternType>(data[0]);
        } else {
            payload.patternType = PatternType::NONE;
        }
        return payload;
    }
    
    /**
     * @brief Deserialize a byte array into a ChangeBrightnessPayload
     * 
     * @param data Pointer to the serialized data
     * @param len Length of the serialized data
     * @return ChangeBrightnessPayload The deserialized payload
     */
    static ChangeBrightnessPayload deserializeBrightnessPayload(const uint8_t* data, size_t len) {
        ChangeBrightnessPayload payload;
        if (data && len > 0) {
            payload.brightnessLevel = data[0];
        }
        return payload;
    }
    
    /**
     * @brief Deserialize a byte array into a ChangeSpeedPayload
     * 
     * @param data Pointer to the serialized data
     * @param len Length of the serialized data
     * @return ChangeSpeedPayload The deserialized payload
     */
    static ChangeSpeedPayload deserializeSpeedPayload(const uint8_t* data, size_t len) {
        ChangeSpeedPayload payload;
        if (data && len > 0) {
            payload.speedLevel = data[0];
        }
        return payload;
    }
    
    /**
     * @brief Deserialize a payload based on its type
     * 
     * @param type The payload type
     * @param data Pointer to the serialized data
     * @param len Length of the serialized data
     * @return std::variant<ChangePatternPayload, ChangeBrightnessPayload, ChangeSpeedPayload> The deserialized payload
     */
    static Payload deserializePayload(PayloadType type, const uint8_t* data, size_t len) {
        switch (type) {
            case PayloadType::ChangePattern:
                return deserializePatternPayload(data, len);
            case PayloadType::ChangeBrightness:
                return deserializeBrightnessPayload(data, len);
            case PayloadType::ChangeSpeed:
                return deserializeSpeedPayload(data, len);
            case PayloadType::RegisterRequest:
                return RegisterRequestPayload();
            case PayloadType::RegistrationSuccessful:
                return RegistrationSuccessfulPayload();
            case PayloadType::Keepalive:
                return KeepalivePayload();
            default:
                // Return empty pattern payload for unknown types
                return ChangePatternPayload{};
        }
    }
    
    /**
     * @brief Parse a received message into a proper Message structure
     * 
     * @param data Raw data received from ESP-NOW
     * @param len Length of the received data
     * @param src_mac Source MAC address 
     * @return std::unique_ptr<Message> Parsed message or nullptr if parsing failed
     */
    static std::unique_ptr<Message> parseMessage(const uint8_t* data, int len, const uint8_t* src_mac) {
        if (!data || len < sizeof(MessageData)) {
            return nullptr;
        }

        auto* messageData = reinterpret_cast<const MessageData*>(data);
        auto message = std::make_unique<Message>();
        
        // Set the message type based on the source MAC
        message->type = IS_BROADCAST_ADDR(src_mac) ? ESPNOW_DATA_BROADCAST : ESPNOW_DATA_UNICAST;
        
        // Set the payload type
        message->payload_type = static_cast<PayloadType>(messageData->payload_type);
        
        // Get payload data and length
        const uint8_t* payloadData = messageData->payload;
        size_t payloadLen = len - sizeof(MessageData);
        
        // Parse the payload based on its type
        message->parsed_payload = deserializePayload(message->payload_type, payloadData, payloadLen);
        
        return message;
    }
};
