#include "InputManager.h"
#include "Button.h"
#include "config.h"
#include "Encoder.h"
#include "esp_log.h"

// Singleton instance implementation
InputManager& InputManager::getInstance() {
    static InputManager instance; // Created only once on first access
    return instance;
}

InputManager::InputManager() {
    ESP_LOGI(TAG, "InputManager singleton instance created");
}

InputManager::~InputManager() {
    ESP_LOGW(TAG, "InputManager singleton instance destroyed");
}

esp_err_t InputManager::init() {
    ESP_LOGI(TAG, "Initializing InputManager");
    
    // Initialize buttons
    ButtonInfo blueButtonInfo;
    blueButtonInfo.name = "BlueButton";
    blueButtonInfo.gpio_num = BUTTONBLUE_GPIO_NUM;
    blueButtonInfo.active_low = true;
    blueButtonInfo.long_press_time_ms = CONFIG_BUTTON_LONG_PRESS_TIME_MS;
    blueButtonInfo.short_press_time_ms = CONFIG_BUTTON_SHORT_PRESS_TIME_MS;
    blueButtonInfo.button = nullptr;
    blueButtonInfo.generalHandler = nullptr;
    
    ButtonInfo redButtonInfo;
    redButtonInfo.name = "RedButton";
    redButtonInfo.gpio_num = BUTTONRED_GPIO_NUM;
    redButtonInfo.active_low = true;
    redButtonInfo.long_press_time_ms = CONFIG_BUTTON_LONG_PRESS_TIME_MS;
    redButtonInfo.short_press_time_ms = CONFIG_BUTTON_SHORT_PRESS_TIME_MS;
    redButtonInfo.button = nullptr;
    redButtonInfo.generalHandler = nullptr;
    
    // Add buttons to map
    buttons[ButtonId::BLUE_BUTTON] = std::move(blueButtonInfo);
    buttons[ButtonId::RED_BUTTON] = std::move(redButtonInfo);
    
    // Create and initialize all buttons
    for (auto& [buttonId, buttonInfo] : buttons) {
        // We can directly use buttonInfo as Button::Config
        buttonInfo.button = std::make_unique<Button>(buttonInfo);
        
        // Register callback for this button
        buttonInfo.button->registerCallback([this, buttonId](Button::Event event) {
            this->handleButtonEvent(buttonId, event);
        });
        
        ESP_LOGI(TAG, "Initialized button '%s' on GPIO %ld", 
                 buttonInfo.name.c_str(), buttonInfo.gpio_num);
    }
    ESP_LOGI(TAG, "Buttons initialized successfully");
    
    // Initialize potentiometers
    PotInfo brightnessPotInfo;
    brightnessPotInfo.name = "BrightnessPot";
    brightnessPotInfo.gpio_num = POT_BRIGHTNESS_GPIO_NUM;
    brightnessPotInfo.adc_unit = ADC_UNIT_1;
    brightnessPotInfo.adc_channel = ADC_CHANNEL_4;
    brightnessPotInfo.attenuation = Potentiometer::Attenuation::DB_12;
    brightnessPotInfo.poll_interval_ms = POT_POLL_INTERVAL_MS;
    brightnessPotInfo.change_threshold = POT_CHANGE_THRESHOLD;
    brightnessPotInfo.enable_center_event = true;
    brightnessPotInfo.center_threshold = POT_CENTER_THRESHOLD;
    brightnessPotInfo.pot = nullptr;
    brightnessPotInfo.generalHandler = nullptr;
    
    PotInfo speedPotInfo;
    speedPotInfo.name = "SpeedPot";
    speedPotInfo.gpio_num = POT_SPEED_GPIO_NUM;
    speedPotInfo.adc_unit = ADC_UNIT_1;
    speedPotInfo.adc_channel = ADC_CHANNEL_7;
    speedPotInfo.attenuation = Potentiometer::Attenuation::DB_12;
    speedPotInfo.poll_interval_ms = POT_POLL_INTERVAL_MS;
    speedPotInfo.change_threshold = POT_CHANGE_THRESHOLD;
    speedPotInfo.enable_center_event = true;
    speedPotInfo.center_threshold = POT_CENTER_THRESHOLD;
    speedPotInfo.pot = nullptr;
    speedPotInfo.generalHandler = nullptr;
    
    // Add potentiometers to map
    potentiometers[PotentiometerId::BRIGHTNESS_POT] = std::move(brightnessPotInfo);
    potentiometers[PotentiometerId::SPEED_POT] = std::move(speedPotInfo);
    
    // Create and initialize the brightness potentiometer (required)
    auto& brightnessPot = potentiometers[PotentiometerId::BRIGHTNESS_POT];
    
    // Now we can directly use brightnessPot as a Potentiometer::Config
    brightnessPot.pot = std::make_unique<Potentiometer>(brightnessPot);
    
    if (!brightnessPot.pot->init()) {
        ESP_LOGE(TAG, "Failed to initialize brightness potentiometer");
        return ESP_FAIL;
    }
    
    // Register callback for the brightness potentiometer
    brightnessPot.pot->registerCallback([this](Potentiometer::Event event, uint32_t value, float percentage) {
        this->handlePotEvent(PotentiometerId::BRIGHTNESS_POT, event, value, percentage);
    });
    
    // Start monitoring brightness potentiometer values
    brightnessPot.pot->start();
    
    ESP_LOGI(TAG, "Initialized potentiometer '%s' on GPIO %d", 
             brightnessPot.name.c_str(), brightnessPot.gpio_num);
    
    
    // Initialize encoders
    EncoderInfo colorEncoderInfo;
    colorEncoderInfo.name = "ColorEncoder";
    colorEncoderInfo.a_pin = ENCODER_COLOR_A_PIN;
    colorEncoderInfo.b_pin = ENCODER_COLOR_B_PIN;
    colorEncoderInfo.btn_pin = ENCODER_COLOR_BTN_PIN;  // Save button pin for button creation
    colorEncoderInfo.poll_interval_ms = ENCODER_POLL_INTERVAL_MS;
    colorEncoderInfo.encoder = nullptr;
    colorEncoderInfo.generalHandler = nullptr;
    
    EncoderInfo patternEncoderInfo;
    patternEncoderInfo.name = "PatternEncoder";
    patternEncoderInfo.a_pin = ENCODER_PATTERN_A_PIN;
    patternEncoderInfo.b_pin = ENCODER_PATTERN_B_PIN;
    patternEncoderInfo.btn_pin = ENCODER_PATTERN_BTN_PIN;  // Save button pin for button creation
    patternEncoderInfo.poll_interval_ms = ENCODER_POLL_INTERVAL_MS;
    patternEncoderInfo.encoder = nullptr;
    patternEncoderInfo.generalHandler = nullptr;
    
    // Add encoders to map
    encoders[EncoderId::COLOR_ENCODER] = std::move(colorEncoderInfo);
    encoders[EncoderId::PATTERN_ENCODER] = std::move(patternEncoderInfo);
    
    // Create encoder button configuration
    ButtonInfo colorEncoderButtonInfo;
    colorEncoderButtonInfo.name = "ColorEncoderButton";
    colorEncoderButtonInfo.gpio_num = ENCODER_COLOR_BTN_PIN;
    colorEncoderButtonInfo.active_low = true;
    colorEncoderButtonInfo.long_press_time_ms = ENCODER_LONG_PRESS_MS;
    colorEncoderButtonInfo.short_press_time_ms = ENCODER_DEBOUNCE_TIME_MS;
    colorEncoderButtonInfo.button = nullptr;
    colorEncoderButtonInfo.generalHandler = nullptr;
    
    ButtonInfo patternEncoderButtonInfo;
    patternEncoderButtonInfo.name = "PatternEncoderButton";
    patternEncoderButtonInfo.gpio_num = ENCODER_PATTERN_BTN_PIN;
    patternEncoderButtonInfo.active_low = true;
    patternEncoderButtonInfo.long_press_time_ms = ENCODER_LONG_PRESS_MS;
    patternEncoderButtonInfo.short_press_time_ms = ENCODER_DEBOUNCE_TIME_MS;
    patternEncoderButtonInfo.button = nullptr;
    patternEncoderButtonInfo.generalHandler = nullptr;
    
    // Add encoder buttons to the buttons map
    buttons[ButtonId::COLOR_ENCODER_BUTTON] = std::move(colorEncoderButtonInfo);
    buttons[ButtonId::PATTERN_ENCODER_BUTTON] = std::move(patternEncoderButtonInfo);
    
    // Create and initialize the encoder buttons
    auto& colorEncoderButton = buttons[ButtonId::COLOR_ENCODER_BUTTON];
    colorEncoderButton.button = std::make_unique<Button>(colorEncoderButton);
    
    // Register callback for this button
    colorEncoderButton.button->registerCallback([this, buttonId = ButtonId::COLOR_ENCODER_BUTTON](Button::Event event) {
        this->handleButtonEvent(buttonId, event);
    });
    ESP_LOGI(TAG, "Initialized encoder button '%s' on GPIO %ld", 
            colorEncoderButton.name.c_str(), colorEncoderButton.gpio_num);
    
    auto& patternEncoderButton = buttons[ButtonId::PATTERN_ENCODER_BUTTON];
    patternEncoderButton.button = std::make_unique<Button>(patternEncoderButton);
    
    // Register callback for this button
    patternEncoderButton.button->registerCallback([this, buttonId = ButtonId::PATTERN_ENCODER_BUTTON](Button::Event event) {
        this->handleButtonEvent(buttonId, event);
    });
    ESP_LOGI(TAG, "Initialized encoder button '%s' on GPIO %ld", 
            patternEncoderButton.name.c_str(), patternEncoderButton.gpio_num);
    
    // Create and initialize encoders
    auto& colorEncoder = encoders[EncoderId::COLOR_ENCODER];
    
    // Create a properly configured Encoder::Config
    Encoder::Config colorEncoderConfig;
    colorEncoderConfig.name = colorEncoder.name;
    colorEncoderConfig.a_pin = colorEncoder.a_pin;
    colorEncoderConfig.b_pin = colorEncoder.b_pin;
    colorEncoderConfig.poll_interval_ms = colorEncoder.poll_interval_ms;
    
    // Create the encoder
    colorEncoder.encoder = std::make_unique<Encoder>(colorEncoderConfig);
    
    if (colorEncoder.encoder->init()) {
        // Register callback for encoder
        colorEncoder.encoder->registerCallback([this](Encoder::Event event, int32_t position) {
            this->handleEncoderEvent(EncoderId::COLOR_ENCODER, event, position);
        });
        
        // Start monitoring encoder
        colorEncoder.encoder->start();
        
        ESP_LOGI(TAG, "Initialized encoder '%s' on GPIO A:%ld B:%ld", 
                 colorEncoder.name.c_str(), colorEncoder.a_pin, colorEncoder.b_pin);
    } else {
        ESP_LOGE(TAG, "Failed to initialize color encoder");
    }
    
    // Initialize pattern encoder
    auto& patternEncoder = encoders[EncoderId::PATTERN_ENCODER];
    
    // Create a properly configured Encoder::Config
    Encoder::Config patternEncoderConfig;
    patternEncoderConfig.name = patternEncoder.name;
    patternEncoderConfig.a_pin = patternEncoder.a_pin;
    patternEncoderConfig.b_pin = patternEncoder.b_pin;
    patternEncoderConfig.poll_interval_ms = patternEncoder.poll_interval_ms;
    
    // Create the encoder
    patternEncoder.encoder = std::make_unique<Encoder>(patternEncoderConfig);
    
    if (patternEncoder.encoder->init()) {
        // Register callback for encoder
        patternEncoder.encoder->registerCallback([this](Encoder::Event event, int32_t position) {
            this->handleEncoderEvent(EncoderId::PATTERN_ENCODER, event, position);
        });
        
        // Start monitoring encoder
        patternEncoder.encoder->start();
        
        ESP_LOGI(TAG, "Initialized encoder '%s' on GPIO A:%ld B:%ld", 
                 patternEncoder.name.c_str(), patternEncoder.a_pin, patternEncoder.b_pin);
    } else {
        ESP_LOGE(TAG, "Failed to initialize pattern encoder");
    }
    
    ESP_LOGI(TAG, "InputManager initialized successfully");
    return ESP_OK;
}

void InputManager::shutdown() {
    ESP_LOGI(TAG, "Shutting down InputManager");
    
    // Stop potentiometers first
    for (auto& [potId, potInfo] : potentiometers) {
        if (potInfo.pot) {
            ESP_LOGI(TAG, "Stopping potentiometer '%s'", potInfo.name.c_str());
            potInfo.pot->stop();
        }
    }
    
    // Stop encoders
    for (auto& [encoderId, encoderInfo] : encoders) {
        if (encoderInfo.encoder) {
            ESP_LOGI(TAG, "Stopping encoder '%s'", encoderInfo.name.c_str());
            encoderInfo.encoder->stop();
        }
    }
    
    // Buttons don't need explicit shutdown as they use smart pointers
    
    ESP_LOGI(TAG, "InputManager shutdown complete");
}

//=== Button Methods ===//

void InputManager::registerButtonHandler(ButtonId buttonId, Button::Event event, 
                                      std::function<void()> callback) {
    if (buttons.find(buttonId) == buttons.end()) {
        ESP_LOGE(TAG, "Cannot register handler for unknown button ID %s", 
                 buttonIdToString(buttonId));
        return;
    }
    
    buttons[buttonId].eventHandlers[event] = callback;
    ESP_LOGI(TAG, "Registered handler for button '%s', event %d", 
             buttons[buttonId].name.c_str(), static_cast<int>(event));
}

void InputManager::registerButtonHandler(ButtonId buttonId, 
                                      std::function<void(Button::Event)> callback) {
    if (buttons.find(buttonId) == buttons.end()) {
        ESP_LOGE(TAG, "Cannot register handler for unknown button ID %s", 
                 buttonIdToString(buttonId));
        return;
    }
    
    buttons[buttonId].generalHandler = callback;
    ESP_LOGI(TAG, "Registered general handler for button '%s'", 
             buttons[buttonId].name.c_str());
}

Button* InputManager::getButton(ButtonId buttonId) {
    if (buttons.find(buttonId) == buttons.end()) {
        ESP_LOGE(TAG, "Button ID %s not found", buttonIdToString(buttonId));
        return nullptr;
    }
    
    return buttons[buttonId].button.get();
}

void InputManager::handleButtonEvent(ButtonId buttonId, Button::Event event) {
    if (buttons.find(buttonId) == buttons.end()) {
        ESP_LOGE(TAG, "Event for unknown button ID %s", buttonIdToString(buttonId));
        return;
    }
    
    const char* eventNames[] = {
        "PRESSED", "DOUBLE_PRESSED", "LONG_PRESSED", "RELEASED"
    };
    
    ESP_LOGI(TAG, "Button event: %s - %s", 
             buttons[buttonId].name.c_str(), 
             eventNames[static_cast<int>(event)]);
    
    // Check for specific event handler
    auto& buttonInfo = buttons[buttonId];
    auto it = buttonInfo.eventHandlers.find(event);
    if (it != buttonInfo.eventHandlers.end() && it->second) {
        // Call the specific event handler
        it->second();
    } 
    // Fall back to general handler if no specific handler was found/called
    else if (buttonInfo.generalHandler) {
        buttonInfo.generalHandler(event);
    }
}

//=== Potentiometer Methods ===//

void InputManager::registerPotHandler(PotentiometerId potId, Potentiometer::Event event,
                                   std::function<void(uint32_t, float)> callback) {
    if (potentiometers.find(potId) == potentiometers.end()) {
        ESP_LOGE(TAG, "Cannot register handler for unknown potentiometer ID %s", 
                 potIdToString(potId));
        return;
    }
    
    potentiometers[potId].eventHandlers[event] = callback;
    ESP_LOGI(TAG, "Registered handler for potentiometer '%s', event %d", 
             potentiometers[potId].name.c_str(), static_cast<int>(event));
}

void InputManager::registerPotHandler(PotentiometerId potId,
                                   std::function<void(Potentiometer::Event, uint32_t, float)> callback) {
    if (potentiometers.find(potId) == potentiometers.end()) {
        ESP_LOGE(TAG, "Cannot register handler for unknown potentiometer ID %s", 
                 potIdToString(potId));
        return;
    }
    
    potentiometers[potId].generalHandler = callback;
    ESP_LOGI(TAG, "Registered general handler for potentiometer '%s'", 
             potentiometers[potId].name.c_str());
}

Potentiometer* InputManager::getPotentiometer(PotentiometerId potId) {
    if (potentiometers.find(potId) == potentiometers.end()) {
        ESP_LOGE(TAG, "Potentiometer ID %s not found", potIdToString(potId));
        return nullptr;
    }
    
    return potentiometers[potId].pot.get();
}

float InputManager::getPotPercentage(PotentiometerId potId) {
    auto* pot = getPotentiometer(potId);
    if (!pot) {
        return -1.0f;
    }
    return pot->getPercentage();
}

uint32_t InputManager::getPotRaw(PotentiometerId potId) {
    auto* pot = getPotentiometer(potId);
    if (!pot) {
        return 0;
    }
    return pot->getRawValue();
}

void InputManager::handlePotEvent(PotentiometerId potId, Potentiometer::Event event, 
                                uint32_t value, float percentage) {
    if (potentiometers.find(potId) == potentiometers.end()) {
        ESP_LOGE(TAG, "Event for unknown potentiometer ID %s", potIdToString(potId));
        return;
    }
    
    const char* eventNames[] = {
        "VALUE_CHANGED", "MIN_REACHED", "MAX_REACHED", "CENTER_REACHED"
    };
    
    ESP_LOGI(TAG, "Potentiometer event: %s - %s, Value: %lu (%.1f%%)", 
             potentiometers[potId].name.c_str(), 
             eventNames[static_cast<int>(event)],
             value, percentage);
    
    // Check for specific event handler
    auto& potInfo = potentiometers[potId];
    auto it = potInfo.eventHandlers.find(event);
    if (it != potInfo.eventHandlers.end() && it->second) {
        // Call the specific event handler
        it->second(value, percentage);
    } 
    // Fall back to general handler if no specific handler was found/called
    else if (potInfo.generalHandler) {
        potInfo.generalHandler(event, value, percentage);
    }
}

//=== Helper Methods ===//

const char* InputManager::buttonIdToString(ButtonId id) {
    switch (id) {
        case ButtonId::BLUE_BUTTON:
            return "BLUE_BUTTON";
        case ButtonId::RED_BUTTON:
            return "RED_BUTTON";
        case ButtonId::COLOR_ENCODER_BUTTON:
            return "COLOR_ENCODER_BUTTON";
        case ButtonId::PATTERN_ENCODER_BUTTON:
            return "PATTERN_ENCODER_BUTTON";
        default:
            return "UNKNOWN";
    }
}

const char* InputManager::potIdToString(PotentiometerId id) {
    switch (id) {
        case PotentiometerId::BRIGHTNESS_POT:
            return "BRIGHTNESS_POT";
        case PotentiometerId::SPEED_POT:
            return "SPEED_POT";
        default:
            return "UNKNOWN";
    }
}

const char* InputManager::encoderIdToString(EncoderId id) {
    switch (id) {
        case EncoderId::COLOR_ENCODER:
            return "COLOR_ENCODER";
        case EncoderId::PATTERN_ENCODER:
            return "PATTERN_ENCODER";
        default:
            return "UNKNOWN";
    }
}

//=== Encoder Methods ===//

void InputManager::registerEncoderHandler(EncoderId encoderId, Encoder::Event event, 
                                        std::function<void(int32_t)> callback) {
    if (encoders.find(encoderId) == encoders.end()) {
        ESP_LOGE(TAG, "Cannot register handler for unknown encoder ID %s", 
                 encoderIdToString(encoderId));
        return;
    }
    
    encoders[encoderId].eventHandlers[event] = callback;
    ESP_LOGI(TAG, "Registered handler for encoder '%s', event %d", 
             encoders[encoderId].name.c_str(), static_cast<int>(event));
}

void InputManager::registerEncoderHandler(EncoderId encoderId, 
                                        std::function<void(Encoder::Event, int32_t)> callback) {
    if (encoders.find(encoderId) == encoders.end()) {
        ESP_LOGE(TAG, "Cannot register handler for unknown encoder ID %s", 
                 encoderIdToString(encoderId));
        return;
    }
    
    encoders[encoderId].generalHandler = callback;
    ESP_LOGI(TAG, "Registered general handler for encoder '%s'", 
             encoders[encoderId].name.c_str());
}

Encoder* InputManager::getEncoder(EncoderId encoderId) {
    if (encoders.find(encoderId) == encoders.end()) {
        ESP_LOGE(TAG, "Encoder ID %s not found", encoderIdToString(encoderId));
        return nullptr;
    }
    
    return encoders[encoderId].encoder.get();
}

int32_t InputManager::getEncoderPosition(EncoderId encoderId) {
    auto* encoder = getEncoder(encoderId);
    if (!encoder) {
        return 0;
    }
    return encoder->getPosition();
}

void InputManager::resetEncoderPosition(EncoderId encoderId) {
    auto* encoder = getEncoder(encoderId);
    if (encoder) {
        encoder->reset();
        ESP_LOGI(TAG, "Reset position for encoder %s", encoderIdToString(encoderId));
    }
}

bool InputManager::isEncoderButtonPressed(EncoderId encoderId) {
    ButtonId buttonId;
    
    // Map encoder ID to corresponding button ID
    switch (encoderId) {
        case EncoderId::COLOR_ENCODER:
            buttonId = ButtonId::COLOR_ENCODER_BUTTON;
            break;
        case EncoderId::PATTERN_ENCODER:
            buttonId = ButtonId::PATTERN_ENCODER_BUTTON;
            break;
        default:
            ESP_LOGE(TAG, "Unknown encoder ID %s", encoderIdToString(encoderId));
            return false;
    }
    
    auto* button = getButton(buttonId);
    if (!button) {
        return false;
    }
    return button->isPressed();
}

void InputManager::handleEncoderEvent(EncoderId encoderId, Encoder::Event event, int32_t position) {
    if (encoders.find(encoderId) == encoders.end()) {
        ESP_LOGE(TAG, "Event for unknown encoder ID %s", encoderIdToString(encoderId));
        return;
    }
    
    const char* eventNames[] = {
        "CLOCKWISE", "COUNTER_CLOCKWISE"
    };
    
    ESP_LOGI(TAG, "Encoder event: %s - %s, Position: %ld", 
             encoders[encoderId].name.c_str(), 
             eventNames[static_cast<int>(event)],
             position);
    
    // Check for specific event handler
    auto& encoderInfo = encoders[encoderId];
    auto it = encoderInfo.eventHandlers.find(event);
    if (it != encoderInfo.eventHandlers.end() && it->second) {
        // Call the specific event handler
        it->second(position);
    } 
    // Fall back to general handler if no specific handler was found/called
    else if (encoderInfo.generalHandler) {
        encoderInfo.generalHandler(event, position);
    }
}
