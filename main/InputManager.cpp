#include "InputManager.h"
#include "I2CButton.h"
#include "TCA6408A.h"
#include "config.h"
#include "Encoder.h"
#include "esp_log.h"
#include "i2c_scanner.h"
#include "LEDManager.h"


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
    esp_log_level_set(TAG, INPUTMANAGER_LOG_LEVEL); // Set log level for this module
    ESP_LOGI(TAG, "Initializing InputManager");
    
    // Initialize LED Manager first - this is critical for LED animations to work
    ESP_LOGI(TAG, "Initializing LEDManager");
    esp_err_t err = LEDManager::getInstance().init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LEDManager: %s", esp_err_to_name(err));
        // Continue anyway - we might still be able to use other input components
    } else {
        ESP_LOGI(TAG, "LEDManager initialized successfully");
    }

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
    
    // Get I2C bus handle from TCA6408A to share with ADS1015
    i2c_master_bus_handle_t shared_i2c_bus = i2cExpander_->getI2CBus();
    if (shared_i2c_bus == nullptr) {
        ESP_LOGW(TAG, "Could not get I2C bus handle from TCA6408A, ADS1015 will create its own bus");
    } else {
        ESP_LOGD(TAG, "Got I2C bus handle from TCA6408A to share with ADS1015");
        
        // Scan I2C bus to detect all connected devices (helps with debugging)
        // i2c_scan_bus(shared_i2c_bus);
    }
    
    // Initialize ADS1015 ADC
    ADS1015::Config ads_config = {
        .i2c_address = ADS1015_I2C_ADDRESS,  // I2C address from config.h
        .sda_pin = I2C_SDA_PIN,              // Same I2C pins as TCA6408A
        .scl_pin = I2C_SCL_PIN,              // Same I2C pins as TCA6408A
        .i2c_freq_hz = ADS1015_I2C_FREQ_HZ,  // I2C frequency from config.h
        .timeout_ms = ADS1015_TIMEOUT_MS,    // Timeout from config.h
        .i2c_bus = shared_i2c_bus,           // Use shared I2C bus from TCA6408A
        .manage_bus = false                  // Don't manage (delete) the bus since TCA6408A owns it
    };
    
    adsAdc_ = std::make_shared<ADS1015>(ads_config);
    ret = adsAdc_->init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to initialize ADS1015: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Continuing without ADS1015 I2C ADC support");
    }
    else
    {
        ESP_LOGI(TAG, "ADS1015 initialized successfully");
    }

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
    // First, set up the brightness potentiometer
    PotInfo brightnessPotInfo;
    brightnessPotInfo.name = "BrightnessPot";
    brightnessPotInfo.poll_interval_ms = POT_POLL_INTERVAL_MS;
    brightnessPotInfo.change_threshold = POT_CHANGE_THRESHOLD;
    brightnessPotInfo.enable_center_event = true;
    brightnessPotInfo.center_threshold = POT_CENTER_THRESHOLD;
    brightnessPotInfo.generalHandler = nullptr;
    
    // Check if we have ADS1015 available for I2C potentiometers
    if (adsAdc_ && adsAdc_->init() == ESP_OK)
    {
        ESP_LOGI(TAG, "Using ADS1015 I2C ADC for potentiometers");
        
        // Configure brightness potentiometer to use ADS1015 channel 0
        brightnessPotInfo.type = PotType::I2C_ADS1015;
        brightnessPotInfo.i2c_channel = ADS1015::Channel::CHANNEL_0;
        brightnessPotInfo.i2c_gain = ADS1015::Gain::GAIN_ONE;  // ±4.096V range
        
        // Add to potentiometer map
        potentiometers[PotentiometerId::BRIGHTNESS_POT] = std::move(brightnessPotInfo);
        
        // Create and initialize I2C potentiometer
        auto &brightnessPot = potentiometers[PotentiometerId::BRIGHTNESS_POT];
        
        // Create I2C potentiometer configuration
        I2CPotentiometer::Config i2cPotConfig;
        i2cPotConfig.name = brightnessPot.name;
        i2cPotConfig.adc = adsAdc_;
        i2cPotConfig.channel = brightnessPot.i2c_channel;
        i2cPotConfig.gain = brightnessPot.i2c_gain;
        i2cPotConfig.poll_interval_ms = brightnessPot.poll_interval_ms;
        i2cPotConfig.change_threshold = brightnessPot.change_threshold;
        i2cPotConfig.enable_center_event = brightnessPot.enable_center_event;
        i2cPotConfig.center_threshold = brightnessPot.center_threshold;
        i2cPotConfig.use_cumulative_tracking = true; // Enable cumulative change tracking for better responsiveness
        
        // Create I2C potentiometer instance
        brightnessPot.i2c_pot = std::make_unique<I2CPotentiometer>(i2cPotConfig);
        
        if (!brightnessPot.i2c_pot->init())
        {
            ESP_LOGE(TAG, "Failed to initialize I2C brightness potentiometer");
            return ESP_FAIL;
        }
        
        // Register callback
        brightnessPot.i2c_pot->registerCallback([this](I2CPotentiometer::Event event, uint32_t value, float percentage)
            { this->handlePotEvent(PotentiometerId::BRIGHTNESS_POT, 
                static_cast<Potentiometer::Event>(static_cast<int>(event)), value, percentage); });
        
        // Start monitoring
        brightnessPot.i2c_pot->start();
        
        ESP_LOGI(TAG, "Initialized I2C potentiometer '%s' on ADS1015 channel %d",
                 brightnessPot.name.c_str(), static_cast<int>(brightnessPot.i2c_channel));
    }
    else
    {
        ESP_LOGI(TAG, "Using direct GPIO/ADC for potentiometers");
        
        // Configure brightness potentiometer to use direct GPIO
        brightnessPotInfo.type = PotType::DIRECT_GPIO;
        brightnessPotInfo.gpio_num = POT_BRIGHTNESS_GPIO_NUM;
        brightnessPotInfo.adc_unit = ADC_UNIT_1;
        brightnessPotInfo.adc_channel = ADC_CHANNEL_3; // ADC1_CH3 corresponds to GPIO 3
        brightnessPotInfo.attenuation = Potentiometer::Attenuation::DB_12;
        
        // Add to potentiometer map
        potentiometers[PotentiometerId::BRIGHTNESS_POT] = std::move(brightnessPotInfo);
        
        // Create and initialize direct GPIO potentiometer
        auto &brightnessPot = potentiometers[PotentiometerId::BRIGHTNESS_POT];
        
        // Create Potentiometer configuration
        Potentiometer::Config potConfig;
        potConfig.name = brightnessPot.name;
        potConfig.gpio_num = brightnessPot.gpio_num;
        potConfig.adc_unit = brightnessPot.adc_unit;
        potConfig.adc_channel = brightnessPot.adc_channel;
        potConfig.attenuation = brightnessPot.attenuation;
        potConfig.poll_interval_ms = brightnessPot.poll_interval_ms;
        potConfig.change_threshold = brightnessPot.change_threshold;
        potConfig.enable_center_event = brightnessPot.enable_center_event;
        potConfig.center_threshold = brightnessPot.center_threshold;
        
        // Create direct GPIO potentiometer instance
        brightnessPot.pot = std::make_unique<Potentiometer>(potConfig);
        
        if (!brightnessPot.pot->init())
        {
            ESP_LOGE(TAG, "Failed to initialize GPIO brightness potentiometer");
            return ESP_FAIL;
        }
        
        // Register callback
        brightnessPot.pot->registerCallback([this](Potentiometer::Event event, uint32_t value, float percentage)
            { this->handlePotEvent(PotentiometerId::BRIGHTNESS_POT, event, value, percentage); });
        
        // Start monitoring
        brightnessPot.pot->start();
        
        ESP_LOGI(TAG, "Initialized GPIO potentiometer '%s' on GPIO %d",
                brightnessPot.name.c_str(), brightnessPot.gpio_num);
    }
    
    // TODO: Add speed potentiometer in a similar way when needed

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

    ESP_LOGI(TAG, "Initializing LED Manager example");
    
    // Initialize LED Manager
    err = LEDManager::getInstance().init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED Manager: %s", esp_err_to_name(err));
    }
    
    // Configure encoder RGB LED (common anode)
    LEDManager::RGBLEDConfig encoderLedConfig;
    encoderLedConfig.name = "EncoderLED";
    encoderLedConfig.red_pin = ENCODER_RGB_RED_PIN;    // GPIO pin for red
    encoderLedConfig.green_pin = ENCODER_RGB_GREEN_PIN;  // GPIO pin for green
    encoderLedConfig.blue_pin = ENCODER_RGB_BLUE_PIN;   // GPIO pin for blue
    encoderLedConfig.red_channel = LEDC_CHANNEL_0;    // LEDC channel for red
    encoderLedConfig.green_channel = LEDC_CHANNEL_1;  // LEDC channel for green
    encoderLedConfig.blue_channel = LEDC_CHANNEL_2;   // LEDC channel for blue
    encoderLedConfig.common_anode = true;  // Common anode RGB LED
    
    // Register the encoder LED
    err = LEDManager::getInstance().registerLED(LEDManager::LEDId::ENCODER_RGB, encoderLedConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register encoder LED: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Encoder RGB LED registered successfully");
    
    // First set a solid color so we can clearly see the transition to animation
    ESP_LOGI(TAG, "Setting to solid blue before starting animation");
    LEDManager::HSV blue_base(240, 100, 100);  // Blue color (H=240)
    LEDManager::getInstance().setHSV(LEDManager::LEDId::ENCODER_RGB, blue_base);
    vTaskDelay(pdMS_TO_TICKS(2000)); // Show solid blue for 2 seconds
    
   
    ESP_LOGI(TAG, "Starting breathing animation on encoder LED");
    LEDManager::AnimationConfig breathingConfig;
    breathingConfig.type = LEDManager::AnimationType::BREATHING;
    breathingConfig.duration_ms = 800;  // 800 ms per breathing cycle for a smooth effect
    breathingConfig.repeat_count = 0;    // Run continuously until stopped
    
    // Get current LED color to check it in the logs, but don't explicitly set it in the config
    // This ensures the animation will continue with whatever color is set by the encoder
    auto currentLedHsv = LEDManager::getInstance().getCurrentColorHSV(LEDManager::LEDId::ENCODER_RGB);
    
    ESP_LOGI(TAG, "Starting breathing animation with current LED color HSV(%u, %u, %u)", 
             currentLedHsv.h, currentLedHsv.s, currentLedHsv.v);
    
    err = LEDManager::getInstance().startAnimation(LEDManager::LEDId::ENCODER_RGB, breathingConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start breathing animation: %s", esp_err_to_name(err));
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
        if (potInfo.type == PotType::DIRECT_GPIO && potInfo.pot)
        {
            ESP_LOGI(TAG, "Stopping GPIO potentiometer '%s'", potInfo.name.c_str());
            potInfo.pot->stop();
        }
        else if (potInfo.type == PotType::I2C_ADS1015 && potInfo.i2c_pot)
        {
            ESP_LOGI(TAG, "Stopping I2C potentiometer '%s'", potInfo.name.c_str());
            potInfo.i2c_pot->stop();
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

    auto &potInfo = potentiometers[potId];
    if (potInfo.type == PotType::DIRECT_GPIO)
    {
        return potInfo.pot.get();
    }
    
    ESP_LOGW(TAG, "Potentiometer ID %s is not a GPIO potentiometer", potIdToString(potId));
    return nullptr;
}

I2CPotentiometer *InputManager::getI2CPotentiometer(PotentiometerId potId)
{
    if (potentiometers.find(potId) == potentiometers.end())
    {
        ESP_LOGE(TAG, "Potentiometer ID %s not found", potIdToString(potId));
        return nullptr;
    }

    auto &potInfo = potentiometers[potId];
    if (potInfo.type == PotType::I2C_ADS1015)
    {
        return potInfo.i2c_pot.get();
    }
    
    ESP_LOGW(TAG, "Potentiometer ID %s is not an I2C potentiometer", potIdToString(potId));
    return nullptr;
}

float InputManager::getPotPercentage(PotentiometerId potId)
{
    if (potentiometers.find(potId) == potentiometers.end())
    {
        ESP_LOGE(TAG, "Potentiometer ID %s not found", potIdToString(potId));
        return -1.0f;
    }

    auto &potInfo = potentiometers[potId];
    if (potInfo.type == PotType::DIRECT_GPIO && potInfo.pot)
    {
        return potInfo.pot->getPercentage();
    }
    else if (potInfo.type == PotType::I2C_ADS1015 && potInfo.i2c_pot)
    {
        return potInfo.i2c_pot->getPercentage();
    }
    
    return -1.0f;
}

uint32_t InputManager::getPotRaw(PotentiometerId potId)
{
    if (potentiometers.find(potId) == potentiometers.end())
    {
        ESP_LOGE(TAG, "Potentiometer ID %s not found", potIdToString(potId));
        return 0;
    }

    auto &potInfo = potentiometers[potId];
    if (potInfo.type == PotType::DIRECT_GPIO && potInfo.pot)
    {
        return potInfo.pot->getRawValue();
    }
    else if (potInfo.type == PotType::I2C_ADS1015 && potInfo.i2c_pot)
    {
        return potInfo.i2c_pot->getRawValue();
    }
    
    return 0;
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
