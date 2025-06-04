#pragma once

#include <map>
#include <memory>
#include <functional>
#include <string>
#include "esp_err.h"
#include "Button.h"
#include "Potentiometer.h"
#include "Encoder.h"

// Device type identifiers
enum class InputDeviceType {
    BUTTON,
    POTENTIOMETER,
    ENCODER
};

// Button ID enum for type-safe button identification
enum class ButtonId {
    BLUE_BUTTON,
    RED_BUTTON,
    COLOR_ENCODER_BUTTON,
    PATTERN_ENCODER_BUTTON,
    // Add new buttons here
};

// Potentiometer ID enum for type-safe potentiometer identification
enum class PotentiometerId {
    BRIGHTNESS_POT,
    SPEED_POT,
    // Add new potentiometers here
};

// Encoder ID enum for future encoder support
enum class EncoderId {
    COLOR_ENCODER,
    PATTERN_ENCODER,
    // Add new encoders here
};

/**
 * @brief Centralized manager for all input devices (buttons, potentiometers, encoders)
 */
class InputManager {
public:
    // Singleton access
    static InputManager& getInstance();
    
    // Delete copy constructor and assignment operator
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;
    
    /**
     * Initialize all input devices
     */
    esp_err_t init();
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    //=== Button Management ===//
    
    /**
     * @brief Register a handler for a specific button event
     * @param buttonId The button identifier
     * @param event The button event type
     * @param callback The function to call when the event occurs
     */
    void registerButtonHandler(ButtonId buttonId, Button::Event event, 
                               std::function<void()> callback);
    
    /**
     * @brief Register a general handler for all events of a button
     * @param buttonId The button identifier
     * @param callback The function to call when any event occurs, with the event as parameter
     */
    void registerButtonHandler(ButtonId buttonId, 
                               std::function<void(Button::Event)> callback);
    
    /**
     * @brief Get access to the underlying Button object
     * @param buttonId The button identifier
     * @return Pointer to the Button object or nullptr if not found
     */
    Button* getButton(ButtonId buttonId);
    
    //=== Potentiometer Management ===//
    
    /**
     * @brief Register a handler for a specific potentiometer event
     * @param potId The potentiometer identifier
     * @param event The potentiometer event type
     * @param callback The function to call when the event occurs, with value and percentage
     */
    void registerPotHandler(PotentiometerId potId, Potentiometer::Event event,
                            std::function<void(uint32_t value, float percentage)> callback);
    
    /**
     * @brief Register a general handler for all events of a potentiometer
     * @param potId The potentiometer identifier
     * @param callback The function to call when any event occurs
     */
    void registerPotHandler(PotentiometerId potId,
                          std::function<void(Potentiometer::Event, uint32_t value, float percentage)> callback);
    
    /**
     * @brief Get access to the underlying Potentiometer object
     * @param potId The potentiometer identifier
     * @return Pointer to the Potentiometer object or nullptr if not found
     */
    Potentiometer* getPotentiometer(PotentiometerId potId);
    
    /**
     * @brief Get the current percentage value of a potentiometer
     * @param potId The potentiometer identifier
     * @return The current value as a percentage (0-100) or -1 if not found
     */
    float getPotPercentage(PotentiometerId potId);
    
    /**
     * @brief Get the current raw value of a potentiometer
     * @param potId The potentiometer identifier
     * @return The current raw value or 0 if not found
     */
    uint32_t getPotRaw(PotentiometerId potId);

    //=== Encoder Management ===//
    
    /**
     * @brief Register a handler for a specific encoder event
     * @param encoderId The encoder identifier
     * @param event The encoder event type
     * @param callback The function to call when the event occurs, with position
     */
    void registerEncoderHandler(EncoderId encoderId, Encoder::Event event, 
                              std::function<void(int32_t position)> callback);
    
    /**
     * @brief Register a general handler for all events of an encoder
     * @param encoderId The encoder identifier
     * @param callback The function to call when any event occurs
     */
    void registerEncoderHandler(EncoderId encoderId, 
                              std::function<void(Encoder::Event, int32_t position)> callback);
    
    /**
     * @brief Get access to the underlying Encoder object
     * @param encoderId The encoder identifier
     * @return Pointer to the Encoder object or nullptr if not found
     */
    Encoder* getEncoder(EncoderId encoderId);
    
    /**
     * @brief Get the current position of an encoder
     * @param encoderId The encoder identifier
     * @return The current position or 0 if not found
     */
    int32_t getEncoderPosition(EncoderId encoderId);
    
    /**
     * @brief Reset the encoder position to zero
     * @param encoderId The encoder identifier
     */
    void resetEncoderPosition(EncoderId encoderId);
    
    /**
     * @brief Check if the encoder button is currently pressed
     * @param encoderId The encoder identifier
     * @return true if button is pressed, false otherwise or if no button or encoder not found
     */
    bool isEncoderButtonPressed(EncoderId encoderId);
    
    /**
     * @brief Helper for converting enum values to strings (for logging)
     */
    static const char* buttonIdToString(ButtonId id);
    static const char* potIdToString(PotentiometerId id);
    static const char* encoderIdToString(EncoderId id);
    
private:
    // Private constructor for singleton
    InputManager();
    
    // Private destructor
    ~InputManager();
    
    // General button event handler
    void handleButtonEvent(ButtonId buttonId, Button::Event event);
    
    // General potentiometer event handler
    void handlePotEvent(PotentiometerId potId, Potentiometer::Event event, 
                      uint32_t value, float percentage);
                      
    // General encoder event handler
    void handleEncoderEvent(EncoderId encoderId, Encoder::Event event, int32_t position);
    
    // Button related storage
    struct ButtonInfo {
        std::string name;                   // Button name
        uint8_t pin;                        // TCA6408A pin number (0-7)
        bool active_low;                    // true if button is active low
        uint16_t debounce_time_ms;          // Debounce time in ms
        std::unique_ptr<Button> button;     // Button instance
        std::function<void(Button::Event)> generalHandler;
        std::map<Button::Event, std::function<void()>> eventHandlers;
    };
    
    // Potentiometer related storage
    struct PotInfo : public Potentiometer::Config {
        std::unique_ptr<Potentiometer> pot;
        std::function<void(Potentiometer::Event, uint32_t, float)> generalHandler;
        std::map<Potentiometer::Event, std::function<void(uint32_t, float)>> eventHandlers;
    };
    
    // Encoder related storage
    struct EncoderInfo : public Encoder::Config {
        std::unique_ptr<Encoder> encoder;
        std::function<void(Encoder::Event, int32_t)> generalHandler;
        std::map<Encoder::Event, std::function<void(int32_t)>> eventHandlers;
        int32_t btn_pin;  // Stored separately for creating button instances
    };
    
    // I2C GPIO expander
    std::shared_ptr<TCA6408A> i2cExpander_;
    
    // Storage for buttons, potentiometers and encoders
    std::map<ButtonId, ButtonInfo> buttons;
    std::map<PotentiometerId, PotInfo> potentiometers;
    std::map<EncoderId, EncoderInfo> encoders;
    
    // Log tag for ESP logging
    static constexpr const char* TAG = "InputManager";
};
