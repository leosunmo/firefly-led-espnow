#pragma once

#include <map>
#include <memory>
#include <functional>
#include <string>
#include "esp_err.h"
#include "I2CButton.h"
#include "Potentiometer.h"
#include "I2CPotentiometer.h"
#include "Encoder.h"
#include "ADS1015.h"
#include "LEDManager.h"
#include "Messages.h"

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
    GREEN_BUTTON,
    WHITE_BUTTON,
    HUE_A_ENCODER_BUTTON,
    HUE_B_ENCODER_BUTTON,
    // Add new buttons here
};

// Map a pattern to its corresponding button
struct PatternState {
    ButtonId buttonId;
    PatternType patternType;
    bool enableEncoders;
};

// Potentiometer ID enum for type-safe potentiometer identification
enum class PotentiometerId {
    BRIGHTNESS_POT,
    SPEED_POT,
    // Add new potentiometers here
};

// Encoder ID enum for future encoder support
enum class EncoderId {
    HUE_A_ENCODER,
    HUE_B_ENCODER,
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
    
    //=== Pattern State Management ===//
    
    /**
     * @brief Set the active pattern state (and associated button)
     * @param buttonId The button that was pressed
     * @param patternType The pattern type to activate
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t setActivePattern(ButtonId buttonId, PatternType patternType);
    
    /**
     * @brief Get the currently active pattern
     * @return PatternType The active pattern
     */
    PatternType getActivePattern() const;
    
    /**
     * @brief Get the button ID associated with the active pattern
     * @return ButtonId The active button ID
     */
    ButtonId getActiveButton() const;
    
    /**
     * @brief Check if encoders should be enabled for the current pattern
     * @return bool True if encoders are enabled
     */
    bool areEncodersEnabled() const;
    
    /**
     * @brief Enable or disable the encoders (both LED and events)
     * @param enable True to enable, false to disable
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t setEncodersEnabled(bool enable);
    
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
     * @brief Get access to the underlying Potentiometer object (for direct GPIO potentiometers)
     * @param potId The potentiometer identifier
     * @return Pointer to the Potentiometer object or nullptr if not found or not a GPIO pot
     */
    Potentiometer* getPotentiometer(PotentiometerId potId);
    
    /**
     * @brief Get access to the underlying I2CPotentiometer object (for I2C potentiometers)
     * @param potId The potentiometer identifier
     * @return Pointer to the I2CPotentiometer object or nullptr if not found or not an I2C pot
     */
    I2CPotentiometer* getI2CPotentiometer(PotentiometerId potId);
    
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
     * @brief Get the I2C bus handle for other components to use
     * @return I2C bus handle or nullptr if not initialized
     */
    i2c_master_bus_handle_t getI2CBus() const { return i2c_bus_; }
    
    /**
     * @brief Button LED control methods
     */
    
    /**
     * @brief Set the state of a button's LED
     * @param buttonId The button identifier
     * @param state true to turn on, false to turn off
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t setButtonLED(ButtonId buttonId, bool state);
    
    /**
     * @brief Configure a pin on the TCA6408A_B as a button LED output
     * @param buttonId The button identifier
     * @param pin The TCA6408A_B pin number (0-7)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t setButtonLEDPin(ButtonId buttonId, uint8_t pin);
    
    /**
     * @brief Configure all button LED pins as outputs
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t configureButtonLEDPins();
    
    /**
     * @brief Turn off all button LEDs
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t clearAllButtonLEDs();
    
    /**
     * @brief Run a startup diagnostic/demo sequence on button LEDs
     * @param duration_ms Duration for each LED to be lit in milliseconds
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t runButtonLEDDiagnostic(uint32_t duration_ms = 500);

    /**
     * @brief Run a startup diagnostic/demo sequence on encoder RGB LEDs
     * @param duration_ms Total duration for the hue cycling animation in milliseconds
     * @param steps Number of hue steps to cycle through
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t runEncoderLEDHueDiagnostic(uint32_t duration_ms = 1500, uint16_t steps = 60);
    
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
    
    // Init helper functions
    esp_err_t initLEDManager();
    esp_err_t initI2C(); // New function to initialize and configure the I2C bus
    esp_err_t initI2CExpanders();
    esp_err_t initADC(i2c_master_bus_handle_t i2c_bus);
    esp_err_t initButtons();
    esp_err_t initPotentiometers();
    esp_err_t initEncoders();
    
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
    
    // Type of potentiometer
    enum class PotType {
        DIRECT_GPIO,  // Direct connection to GPIO/ADC
        I2C_ADS1015   // Connected through ADS1015 I2C ADC
    };
    
    // Potentiometer related storage
    struct PotInfo {
        std::string name;                   // Potentiometer name
        PotType type;                       // Type of potentiometer
        
        // Direct GPIO potentiometer (if type == DIRECT_GPIO)
        int gpio_num;                       // GPIO number for ADC input
        adc_unit_t adc_unit;                // ADC unit
        adc_channel_t adc_channel;          // ADC channel
        Potentiometer::Attenuation attenuation; // Signal attenuation
        std::unique_ptr<Potentiometer> pot; // Potentiometer instance
        
        // I2C potentiometer (if type == I2C_ADS1015)
        ADS1015::Channel i2c_channel;       // ADS1015 channel number
        ADS1015::Gain i2c_gain;             // ADS1015 gain setting
        std::unique_ptr<I2CPotentiometer> i2c_pot; // I2C Potentiometer instance
        
        // Common settings
        uint32_t poll_interval_ms;          // Interval in ms between ADC readings
        uint32_t change_threshold;          // Minimum change to trigger event
        bool enable_center_event;           // Whether to trigger center events
        uint32_t center_threshold;          // Threshold around center position
        
        // Handlers
        std::function<void(Potentiometer::Event, uint32_t, float)> generalHandler;
        std::map<Potentiometer::Event, std::function<void(uint32_t, float)>> eventHandlers;
    };
    
    // Encoder related storage
    struct EncoderInfo : public Encoder::Config {
        std::unique_ptr<Encoder> encoder;
        std::function<void(Encoder::Event, int32_t)> generalHandler;
        std::map<Encoder::Event, std::function<void(int32_t)>> eventHandlers;
    };
    
    // I2C devices
    std::shared_ptr<TCA6408A> i2cExpanderA_;  // TCA6408A_A I2C GPIO expander for regular buttons (with pullups)
    std::shared_ptr<TCA6408A> i2cExpanderB_;  // TCA6408A_B I2C GPIO expander for encoder buttons and button LEDs
    std::shared_ptr<ADS1015> adsAdc_;        // ADS1015 I2C ADC for potentiometers
    i2c_master_bus_handle_t i2c_bus_ = nullptr; // Centralized I2C bus handle
    
    // Storage for buttons, potentiometers and encoders
    std::map<ButtonId, ButtonInfo> buttons;
    std::map<PotentiometerId, PotInfo> potentiometers;
    std::map<EncoderId, EncoderInfo> encoders;
    
    // Map of ButtonId to LED pin number on TCA6408A_B 
    std::map<ButtonId, uint8_t> buttonLedPins;
    
    // Pattern state tracking
    ButtonId activeButtonId_ = ButtonId::RED_BUTTON; // Default to RED button
    PatternType activePattern_ = PatternType::CHROMA_WAVE; // Default to CHROMA_WAVE
    bool encodersEnabled_ = true; // Default to enabled
    
    // Encoder LED color state tracking for persistence
    uint16_t lastEncoderAHue_ = 60;  // Default to yellow (60°)
    uint16_t lastEncoderBHue_ = 180; // Default to cyan (180°)
    
    // Log tag for ESP logging
    static constexpr const char* TAG = "InputManager";
};
