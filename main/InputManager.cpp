#include "InputManager.h"
#include "I2CButton.h"
#include "TCA6408A.h"
#include "config.h"
#include "Encoder.h"
#include "esp_log.h"

// Singleton instance implementation
InputManager &InputManager::getInstance()
{
    static InputManager instance; // Created only once on first access
    return instance;
}

InputManager::InputManager()
{
    ESP_LOGI(TAG, "InputManager singleton instance created");
}

InputManager::~InputManager()
{
    ESP_LOGW(TAG, "InputManager singleton instance destroyed");
}

esp_err_t InputManager::init()
{
    ESP_LOGI(TAG, "Initializing InputManager");

    // Initialize TCA6408A I2C GPIO expander
    TCA6408A::Config tca_config = {
        .i2c_address = TCA6408A_I2C_ADDRESS, // I2C address from config.h
        .sda_pin = I2C_SDA_PIN,              // I2C SDA pin
        .scl_pin = I2C_SCL_PIN,              // I2C SCL pin
        .i2c_freq_hz = 100000,               // 100kHz I2C frequency for maximum reliability
        .timeout_ms = 1000,                  // 1 second timeout (generous for debugging)
        .poll_period_ms = 10,                // 10ms polling period (only if interrupt disabled)
        .int_pin = TCA6408A_INT_PIN,         // GPIO pin connected to TCA6408A INT pin
    };

    i2cExpander_ = std::make_shared<TCA6408A>(tca_config);
    esp_err_t ret = i2cExpander_->init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TCA6408A: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TCA6408A initialized successfully");

    // Initialize buttons

    ButtonInfo redButtonInfo;
    redButtonInfo.name = "RedButton";
    redButtonInfo.pin = 0; // TCA6408A pin 0
    redButtonInfo.active_low = true;
    redButtonInfo.debounce_time_ms = CONFIG_BUTTON_DEBOUNCE_TIME_MS;
    redButtonInfo.button = nullptr;
    redButtonInfo.generalHandler = nullptr;

    ButtonInfo blueButtonInfo;
    blueButtonInfo.name = "BlueButton";
    blueButtonInfo.pin = 1; // TCA6408A pin 1
    blueButtonInfo.active_low = true;
    blueButtonInfo.debounce_time_ms = CONFIG_BUTTON_DEBOUNCE_TIME_MS;
    blueButtonInfo.button = nullptr;
    blueButtonInfo.generalHandler = nullptr;

    // Add buttons to map
    buttons[ButtonId::BLUE_BUTTON] = std::move(blueButtonInfo);
    buttons[ButtonId::RED_BUTTON] = std::move(redButtonInfo);

    // Create and initialize all buttons
    for (auto &[buttonId, buttonInfo] : buttons)
    {
        // Create button configuration
        Button::Config config;
        config.name = buttonInfo.name;
        config.i2c_expander = i2cExpander_;
        config.pin = buttonInfo.pin;
        config.active_low = buttonInfo.active_low;
        config.debounce_time_ms = buttonInfo.debounce_time_ms;

        // Create button
        buttonInfo.button = std::make_unique<Button>(config);

        // Initialize the button
        esp_err_t ret = buttonInfo.button->init();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize button '%s': %s",
                     buttonInfo.name.c_str(), esp_err_to_name(ret));
            continue;
        }

        // Register callback for this button
        buttonInfo.button->registerCallback([this, buttonId](Button::Event event)
                                            { this->handleButtonEvent(buttonId, event); });
    }
    ESP_LOGI(TAG, "Buttons initialized successfully");

    // Initialize potentiometers
    PotInfo brightnessPotInfo;
    brightnessPotInfo.name = "BrightnessPot";
    brightnessPotInfo.gpio_num = POT_BRIGHTNESS_GPIO_NUM;
    brightnessPotInfo.adc_unit = ADC_UNIT_1;
    brightnessPotInfo.adc_channel = ADC_CHANNEL_3; // ADC1_CH3 corresponds to GPIO 3
    brightnessPotInfo.attenuation = Potentiometer::Attenuation::DB_12;
    brightnessPotInfo.poll_interval_ms = POT_POLL_INTERVAL_MS;
    brightnessPotInfo.change_threshold = POT_CHANGE_THRESHOLD;
    brightnessPotInfo.enable_center_event = true;
    brightnessPotInfo.center_threshold = POT_CENTER_THRESHOLD;
    brightnessPotInfo.pot = nullptr;
    brightnessPotInfo.generalHandler = nullptr;

    // PotInfo speedPotInfo;
    // speedPotInfo.name = "SpeedPot";
    // speedPotInfo.gpio_num = POT_SPEED_GPIO_NUM;
    // speedPotInfo.adc_unit = ADC_UNIT_1;
    // speedPotInfo.adc_channel = ADC_CHANNEL_7;
    // speedPotInfo.attenuation = Potentiometer::Attenuation::DB_12;
    // speedPotInfo.poll_interval_ms = POT_POLL_INTERVAL_MS;
    // speedPotInfo.change_threshold = POT_CHANGE_THRESHOLD;
    // speedPotInfo.enable_center_event = true;
    // speedPotInfo.center_threshold = POT_CENTER_THRESHOLD;
    // speedPotInfo.pot = nullptr;
    // speedPotInfo.generalHandler = nullptr;

    // Add potentiometers to map
    potentiometers[PotentiometerId::BRIGHTNESS_POT] = std::move(brightnessPotInfo);
    // potentiometers[PotentiometerId::SPEED_POT] = std::move(speedPotInfo);

    // Create and initialize the brightness potentiometer (required)
    auto &brightnessPot = potentiometers[PotentiometerId::BRIGHTNESS_POT];

    // Now we can directly use brightnessPot as a Potentiometer::Config
    brightnessPot.pot = std::make_unique<Potentiometer>(brightnessPot);

    if (!brightnessPot.pot->init())
    {
        ESP_LOGE(TAG, "Failed to initialize brightness potentiometer");
        return ESP_FAIL;
    }

    // Register callback for the brightness potentiometer
    brightnessPot.pot->registerCallback([this](Potentiometer::Event event, uint32_t value, float percentage)
                                        { this->handlePotEvent(PotentiometerId::BRIGHTNESS_POT, event, value, percentage); });

    // Start monitoring brightness potentiometer values
    brightnessPot.pot->start();

    ESP_LOGI(TAG, "Initialized potentiometer '%s' on GPIO %d",
             brightnessPot.name.c_str(), brightnessPot.gpio_num);

    // Initialize encoders
    EncoderInfo colorEncoderInfo;
    colorEncoderInfo.name = "ColorEncoder";
    colorEncoderInfo.a_pin = ENCODER_COLOR_A_PIN;
    colorEncoderInfo.b_pin = ENCODER_COLOR_B_PIN;
    colorEncoderInfo.encoder = nullptr;
    colorEncoderInfo.generalHandler = nullptr;

    // EncoderInfo patternEncoderInfo;
    // patternEncoderInfo.name = "PatternEncoder";
    // patternEncoderInfo.a_pin = ENCODER_PATTERN_A_PIN;
    // patternEncoderInfo.b_pin = ENCODER_PATTERN_B_PIN;
    // patternEncoderInfo.poll_interval_ms = ENCODER_POLL_INTERVAL_MS;
    // patternEncoderInfo.encoder = nullptr;
    // patternEncoderInfo.generalHandler = nullptr;

    // Add encoders to map
    encoders[EncoderId::COLOR_ENCODER] = std::move(colorEncoderInfo);
    // encoders[EncoderId::PATTERN_ENCODER] = std::move(patternEncoderInfo);

    // Create and initialize encoders
    auto& colorEncoder = encoders[EncoderId::COLOR_ENCODER];

    // Create a properly configured Encoder::Config
    Encoder::Config colorEncoderConfig;
    colorEncoderConfig.name = colorEncoder.name;
    colorEncoderConfig.debounce_ms = ENCODER_DEBOUNCE_MS; // Use a default debounce time
    colorEncoderConfig.a_pin = colorEncoder.a_pin;
    colorEncoderConfig.b_pin = colorEncoder.b_pin;

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

    // // Initialize pattern encoder
    // auto& patternEncoder = encoders[EncoderId::PATTERN_ENCODER];

    // // Create a properly configured Encoder::Config
    // Encoder::Config patternEncoderConfig;
    // patternEncoderConfig.name = patternEncoder.name;
    // patternEncoderConfig.a_pin = patternEncoder.a_pin;
    // patternEncoderConfig.b_pin = patternEncoder.b_pin;
    // patternEncoderConfig.poll_interval_ms = patternEncoder.poll_interval_ms;

    // // Create the encoder
    // patternEncoder.encoder = std::make_unique<Encoder>(patternEncoderConfig);

    // if (patternEncoder.encoder->init()) {
    //     // Register callback for encoder
    //     patternEncoder.encoder->registerCallback([this](Encoder::Event event, int32_t position) {
    //         this->handleEncoderEvent(EncoderId::PATTERN_ENCODER, event, position);
    //     });

    //     // Start monitoring encoder
    //     patternEncoder.encoder->start();

    //     ESP_LOGI(TAG, "Initialized encoder '%s' on GPIO A:%ld B:%ld",
    //              patternEncoder.name.c_str(), patternEncoder.a_pin, patternEncoder.b_pin);
    // } else {
    //     ESP_LOGE(TAG, "Failed to initialize pattern encoder");
    // }

    ESP_LOGI(TAG, "InputManager initialized successfully");
    return ESP_OK;
}

void InputManager::shutdown()
{
    ESP_LOGI(TAG, "Shutting down InputManager");

    // Stop potentiometers first
    for (auto &[potId, potInfo] : potentiometers)
    {
        if (potInfo.pot)
        {
            ESP_LOGI(TAG, "Stopping potentiometer '%s'", potInfo.name.c_str());
            potInfo.pot->stop();
        }
    }

    // Stop encoders
    for (auto &[encoderId, encoderInfo] : encoders)
    {
        if (encoderInfo.encoder)
        {
            ESP_LOGI(TAG, "Stopping encoder '%s'", encoderInfo.name.c_str());
            encoderInfo.encoder->stop();
        }
    }

    // Buttons don't need explicit shutdown as they use smart pointers

    ESP_LOGI(TAG, "InputManager shutdown complete");
}

//=== Button Methods ===//

void InputManager::registerButtonHandler(ButtonId buttonId, Button::Event event,
                                         std::function<void()> callback)
{
    if (buttons.find(buttonId) == buttons.end())
    {
        ESP_LOGE(TAG, "Cannot register handler for unknown button ID %s",
                 buttonIdToString(buttonId));
        return;
    }

    buttons[buttonId].eventHandlers[event] = callback;
    ESP_LOGI(TAG, "Registered handler for button '%s', event %d",
             buttons[buttonId].name.c_str(), static_cast<int>(event));
}

void InputManager::registerButtonHandler(ButtonId buttonId,
                                         std::function<void(Button::Event)> callback)
{
    if (buttons.find(buttonId) == buttons.end())
    {
        ESP_LOGE(TAG, "Cannot register handler for unknown button ID %s",
                 buttonIdToString(buttonId));
        return;
    }

    buttons[buttonId].generalHandler = callback;
    ESP_LOGI(TAG, "Registered general handler for button '%s'",
             buttons[buttonId].name.c_str());
}

Button *InputManager::getButton(ButtonId buttonId)
{
    if (buttons.find(buttonId) == buttons.end())
    {
        ESP_LOGE(TAG, "Button ID %s not found", buttonIdToString(buttonId));
        return nullptr;
    }

    return buttons[buttonId].button.get();
}

void InputManager::handleButtonEvent(ButtonId buttonId, Button::Event event)
{
    if (buttons.find(buttonId) == buttons.end())
    {
        ESP_LOGE(TAG, "Event for unknown button ID %s", buttonIdToString(buttonId));
        return;
    }

    const char *eventNames[] = {
        "PRESSED", "RELEASED"};

    ESP_LOGI(TAG, "Button event: %s - %s",
             buttons[buttonId].name.c_str(),
             eventNames[static_cast<int>(event)]);

    // Check for specific event handler
    auto &buttonInfo = buttons[buttonId];
    auto it = buttonInfo.eventHandlers.find(event);
    if (it != buttonInfo.eventHandlers.end() && it->second)
    {
        // Call the specific event handler
        it->second();
    }
    // Fall back to general handler if no specific handler was found/called
    else if (buttonInfo.generalHandler)
    {
        buttonInfo.generalHandler(event);
    }
}

//=== Potentiometer Methods ===//

void InputManager::registerPotHandler(PotentiometerId potId, Potentiometer::Event event,
                                      std::function<void(uint32_t, float)> callback)
{
    if (potentiometers.find(potId) == potentiometers.end())
    {
        ESP_LOGE(TAG, "Cannot register handler for unknown potentiometer ID %s",
                 potIdToString(potId));
        return;
    }

    potentiometers[potId].eventHandlers[event] = callback;
    ESP_LOGI(TAG, "Registered handler for potentiometer '%s', event %d",
             potentiometers[potId].name.c_str(), static_cast<int>(event));
}

void InputManager::registerPotHandler(PotentiometerId potId,
                                      std::function<void(Potentiometer::Event, uint32_t, float)> callback)
{
    if (potentiometers.find(potId) == potentiometers.end())
    {
        ESP_LOGE(TAG, "Cannot register handler for unknown potentiometer ID %s",
                 potIdToString(potId));
        return;
    }

    potentiometers[potId].generalHandler = callback;
    ESP_LOGI(TAG, "Registered general handler for potentiometer '%s'",
             potentiometers[potId].name.c_str());
}

Potentiometer *InputManager::getPotentiometer(PotentiometerId potId)
{
    if (potentiometers.find(potId) == potentiometers.end())
    {
        ESP_LOGE(TAG, "Potentiometer ID %s not found", potIdToString(potId));
        return nullptr;
    }

    return potentiometers[potId].pot.get();
}

float InputManager::getPotPercentage(PotentiometerId potId)
{
    auto *pot = getPotentiometer(potId);
    if (!pot)
    {
        return -1.0f;
    }
    return pot->getPercentage();
}

uint32_t InputManager::getPotRaw(PotentiometerId potId)
{
    auto *pot = getPotentiometer(potId);
    if (!pot)
    {
        return 0;
    }
    return pot->getRawValue();
}

void InputManager::handlePotEvent(PotentiometerId potId, Potentiometer::Event event,
                                  uint32_t value, float percentage)
{
    if (potentiometers.find(potId) == potentiometers.end())
    {
        ESP_LOGE(TAG, "Event for unknown potentiometer ID %s", potIdToString(potId));
        return;
    }

    const char *eventNames[] = {
        "VALUE_CHANGED", "MIN_REACHED", "MAX_REACHED", "CENTER_REACHED"};

    ESP_LOGI(TAG, "Potentiometer event: %s - %s, Value: %lu (%.1f%%)",
             potentiometers[potId].name.c_str(),
             eventNames[static_cast<int>(event)],
             value, percentage);

    // Check for specific event handler
    auto &potInfo = potentiometers[potId];
    auto it = potInfo.eventHandlers.find(event);
    if (it != potInfo.eventHandlers.end() && it->second)
    {
        // Call the specific event handler
        it->second(value, percentage);
    }
    // Fall back to general handler if no specific handler was found/called
    else if (potInfo.generalHandler)
    {
        potInfo.generalHandler(event, value, percentage);
    }
}

//=== Helper Methods ===//

const char *InputManager::buttonIdToString(ButtonId id)
{
    switch (id)
    {
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

const char *InputManager::potIdToString(PotentiometerId id)
{
    switch (id)
    {
    case PotentiometerId::BRIGHTNESS_POT:
        return "BRIGHTNESS_POT";
    case PotentiometerId::SPEED_POT:
        return "SPEED_POT";
    default:
        return "UNKNOWN";
    }
}

const char *InputManager::encoderIdToString(EncoderId id)
{
    switch (id)
    {
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
                                          std::function<void(int32_t)> callback)
{
    if (encoders.find(encoderId) == encoders.end())
    {
        ESP_LOGE(TAG, "Cannot register handler for unknown encoder ID %s",
                 encoderIdToString(encoderId));
        return;
    }

    encoders[encoderId].eventHandlers[event] = callback;
    ESP_LOGI(TAG, "Registered handler for encoder '%s', event %d",
             encoders[encoderId].name.c_str(), static_cast<int>(event));
}

void InputManager::registerEncoderHandler(EncoderId encoderId,
                                          std::function<void(Encoder::Event, int32_t)> callback)
{
    if (encoders.find(encoderId) == encoders.end())
    {
        ESP_LOGE(TAG, "Cannot register handler for unknown encoder ID %s",
                 encoderIdToString(encoderId));
        return;
    }

    encoders[encoderId].generalHandler = callback;
    ESP_LOGI(TAG, "Registered general handler for encoder '%s'",
             encoders[encoderId].name.c_str());
}

Encoder *InputManager::getEncoder(EncoderId encoderId)
{
    if (encoders.find(encoderId) == encoders.end())
    {
        ESP_LOGE(TAG, "Encoder ID %s not found", encoderIdToString(encoderId));
        return nullptr;
    }

    return encoders[encoderId].encoder.get();
}

int32_t InputManager::getEncoderPosition(EncoderId encoderId)
{
    auto *encoder = getEncoder(encoderId);
    if (!encoder)
    {
        return 0;
    }
    return encoder->getPosition();
}

void InputManager::resetEncoderPosition(EncoderId encoderId)
{
    auto *encoder = getEncoder(encoderId);
    if (encoder)
    {
        encoder->reset();
        ESP_LOGI(TAG, "Reset position for encoder %s", encoderIdToString(encoderId));
    }
}

void InputManager::handleEncoderEvent(EncoderId encoderId, Encoder::Event event, int32_t position)
{
    if (encoders.find(encoderId) == encoders.end())
    {
        ESP_LOGE(TAG, "Event for unknown encoder ID %s", encoderIdToString(encoderId));
        return;
    }

    const char *eventNames[] = {
        "CLOCKWISE", "COUNTER_CLOCKWISE"};

    ESP_LOGI(TAG, "Encoder event: %s - %s, Position: %ld",
             encoders[encoderId].name.c_str(),
             eventNames[static_cast<int>(event)],
             position);

    // Check for specific event handler
    auto &encoderInfo = encoders[encoderId];
    auto it = encoderInfo.eventHandlers.find(event);
    if (it != encoderInfo.eventHandlers.end() && it->second)
    {
        // Call the specific event handler
        it->second(position);
    }
    // Fall back to general handler if no specific handler was found/called
    else if (encoderInfo.generalHandler)
    {
        encoderInfo.generalHandler(event, position);
    }
}
