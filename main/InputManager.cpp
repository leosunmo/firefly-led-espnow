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
    
    // Initialize components in the correct order, handling errors
    esp_err_t err = ESP_OK;

    // 1. Initialize LED Manager first
    err = initLEDManager();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LED Manager initialization failed, continuing with other components");
    }
    
    // 2. Initialize the I2C bus
    err = initI2C();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed, cannot continue");
        return err;
    }
    
    // 3. Initialize I2C Expanders
    err = initI2CExpanders();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C Expander initialization failed, cannot continue");
        return err;
    }
    
    // 4. Initialize ADS1015 ADC for potentiometers
    err = initADC(i2c_bus_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC initialization failed, continuing without ADS1015 support");
    }
    
    // 5. Initialize Buttons
    err = initButtons();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Button initialization failed or partially failed, continuing");
    }
    
    // 6. Initialize Potentiometers
    err = initPotentiometers();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Potentiometer initialization failed or partially failed, continuing");
    }
    
    // 7. Initialize Encoders
    err = initEncoders();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Encoder initialization failed or partially failed, continuing");
    }
    
    // 8. Run the button LED diagnostic sequence
    ESP_LOGI(TAG, "Running button LED diagnostic sequence");
    err = runButtonLEDDiagnostic(500); // Light each LED for 500ms
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Button LED diagnostic failed: %s", esp_err_to_name(err));
        // Continue anyway, this is just a visual indicator
    }
    
    // 9. Run the encoder RGB LED hue cycling diagnostic
    ESP_LOGI(TAG, "Running encoder RGB LED hue cycling diagnostic sequence");
    err = runEncoderLEDHueDiagnostic(1500, 60); // Cycle through hues over 1500ms with 60 steps
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Encoder RGB LED diagnostic failed: %s", esp_err_to_name(err));
        // Continue anyway, this is just a visual indicator
    }
    
    // 10. Initialize the active pattern state (default to RED button/CHROMA_WAVE)
    ESP_LOGI(TAG, "Setting initial active pattern state");
    err = setActivePattern(ButtonId::RED_BUTTON, PatternType::CHROMA_WAVE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set initial active pattern: %s", esp_err_to_name(err));
        // Continue anyway, this will be set when a button is pressed
    }
    
    ESP_LOGI(TAG, "InputManager initialized successfully");
    return ESP_OK;
}

esp_err_t InputManager::initLEDManager()
{
    ESP_LOGI(TAG, "Initializing LEDManager");
    esp_err_t err = LEDManager::getInstance().init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LEDManager: %s", esp_err_to_name(err));
        // Continue anyway - we might still be able to use other input components
    } else {
        ESP_LOGI(TAG, "LEDManager initialized successfully");
    }
    
    return err;
}

esp_err_t InputManager::initI2C()
{
    ESP_LOGI(TAG, "Initializing central I2C bus");
    
    // Create I2C bus configuration
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = static_cast<gpio_num_t>(I2C_SDA_PIN),
        .scl_io_num = static_cast<gpio_num_t>(I2C_SCL_PIN),
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {},
    };
    
    // Install I2C master bus
    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "I2C master bus initialized successfully");
    return ESP_OK;
}

esp_err_t InputManager::initI2CExpanders()
{
    ESP_LOGI(TAG, "Initializing I2C Expanders");
    
    if (i2c_bus_ == nullptr) {
        ESP_LOGE(TAG, "Cannot initialize I2C expanders: I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Initialize TCA6408A_A I2C GPIO expander for regular buttons (with pullups)
    TCA6408A::Config tca_a_config = {
        .name = "A",                         // Name for logs (will show as TCA6408A_A)
        .i2c_address = TCA6408A_A_I2C_ADDRESS, // I2C address from config.h
        .sda_pin = I2C_SDA_PIN,              // I2C SDA pin
        .scl_pin = I2C_SCL_PIN,              // I2C SCL pin
        .i2c_freq_hz = 100000,               // 100kHz I2C frequency for maximum reliability
        .timeout_ms = 1000,                  // 1 second timeout (generous for debugging)
        .poll_period_ms = 10,                // 10ms polling period (only if interrupt disabled)
        .int_pin = TCA6408A_A_INT_PIN,       // GPIO pin connected to TCA6408A_A INT pin
        .i2c_bus = i2c_bus_,                 // Use the centralized I2C bus
        .manage_bus = false                  // Don't manage (delete) the bus since InputManager owns it
    };

    i2cExpanderA_ = std::make_shared<TCA6408A>(tca_a_config);
    esp_err_t ret = i2cExpanderA_->init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TCA6408A_A: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TCA6408A_A initialized successfully");
    
    // Initialize TCA6408A_B I2C GPIO expander for encoder buttons and button LEDs
    // Reuse the same I2C bus    
    TCA6408A::Config tca_b_config = {
        .name = "B",                         // Name for logs (will show as TCA6408A_B)
        .i2c_address = TCA6408A_B_I2C_ADDRESS, // I2C address from config.h
        .sda_pin = I2C_SDA_PIN,              // I2C SDA pin
        .scl_pin = I2C_SCL_PIN,              // I2C SCL pin
        .i2c_freq_hz = 100000,               // 100kHz I2C frequency for maximum reliability
        .timeout_ms = 1000,                  // 1 second timeout (generous for debugging)
        .poll_period_ms = 10,                // 10ms polling period (only if interrupt disabled)
        .int_pin = TCA6408A_B_INT_PIN,       // Now we can use interrupts for both TCA6408A instances
        .i2c_bus = i2c_bus_,                 // Use the centralized I2C bus
        .manage_bus = false                  // Don't manage (delete) the bus since InputManager owns it
    };

    i2cExpanderB_ = std::make_shared<TCA6408A>(tca_b_config);
    ret = i2cExpanderB_->init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TCA6408A_B: %s", esp_err_to_name(ret));
        // Continue anyway - we might still be able to use other components
    }
    else
    {
        ESP_LOGI(TAG, "TCA6408A_B initialized successfully");
        
        // Configure unused pins as outputs to prevent floating inputs and spurious interrupts
        for (uint8_t pin = 6; pin <= 7; pin++) {
            esp_err_t pin_ret = i2cExpanderB_->configurePin(pin, true); // true = output
            if (pin_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to configure unused pin %d as output: %s", pin, esp_err_to_name(pin_ret));
            } else {
                ESP_LOGI(TAG, "Configured unused pin %d as output to prevent floating input", pin);
            }
        }
        
        // Start monitoring the TCA6408A_B I/O expander
        i2cExpanderB_->startMonitoring();
    }

    return ESP_OK;
}

esp_err_t InputManager::initADC(i2c_master_bus_handle_t i2c_bus)
{
    ESP_LOGI(TAG, "Initializing ADS1015 ADC");
    
    if (i2c_bus == nullptr) {
        ESP_LOGE(TAG, "Cannot initialize ADS1015: I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Initialize ADS1015 ADC
    ADS1015::Config ads_config = {
        .i2c_address = ADS1015_I2C_ADDRESS,  // I2C address from config.h
        .sda_pin = I2C_SDA_PIN,              // Same I2C pins as TCA6408A
        .scl_pin = I2C_SCL_PIN,              // Same I2C pins as TCA6408A
        .i2c_freq_hz = ADS1015_I2C_FREQ_HZ,  // I2C frequency from config.h
        .timeout_ms = ADS1015_TIMEOUT_MS,    // Timeout from config.h
        .i2c_bus = i2c_bus,                  // Use the centralized I2C bus
        .manage_bus = false                  // Don't manage (delete) the bus since InputManager owns it
    };
    
    adsAdc_ = std::make_shared<ADS1015>(ads_config);
    esp_err_t ret = adsAdc_->init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to initialize ADS1015: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Continuing without ADS1015 I2C ADC support");
    }
    else
    {
        ESP_LOGI(TAG, "ADS1015 initialized successfully");
    }
    
    return ret;
}

esp_err_t InputManager::initButtons()
{
    ESP_LOGI(TAG, "Initializing Buttons");
    
    // Setup button information
    ButtonInfo redButtonInfo;
    redButtonInfo.name = "RedButton";
    redButtonInfo.pin = 0; // TCA6408A_A pin 0
    redButtonInfo.active_low = true;
    redButtonInfo.debounce_time_ms = CONFIG_BUTTON_DEBOUNCE_TIME_MS;
    redButtonInfo.button = nullptr;
    redButtonInfo.generalHandler = nullptr;

    ButtonInfo blueButtonInfo;
    blueButtonInfo.name = "BlueButton";
    blueButtonInfo.pin = 1; // TCA6408A_A pin 1
    blueButtonInfo.active_low = true;
    blueButtonInfo.debounce_time_ms = CONFIG_BUTTON_DEBOUNCE_TIME_MS;
    blueButtonInfo.button = nullptr;
    blueButtonInfo.generalHandler = nullptr;

    ButtonInfo greenButtonInfo;
    greenButtonInfo.name = "GreenButton";
    greenButtonInfo.pin = 2; // TCA6408A_A pin 2
    greenButtonInfo.active_low = true;
    greenButtonInfo.debounce_time_ms = CONFIG_BUTTON_DEBOUNCE_TIME_MS;
    greenButtonInfo.button = nullptr;
    greenButtonInfo.generalHandler = nullptr;

    ButtonInfo whiteButtonInfo;
    whiteButtonInfo.name = "WhiteButton";
    whiteButtonInfo.pin = 3; // TCA6408A_A pin 3
    whiteButtonInfo.active_low = true;
    whiteButtonInfo.debounce_time_ms = CONFIG_BUTTON_DEBOUNCE_TIME_MS;
    whiteButtonInfo.button = nullptr;
    whiteButtonInfo.generalHandler = nullptr;

    ButtonInfo HueAEncoderButtonInfo;
    HueAEncoderButtonInfo.name = "HueAEncoderButton";
    HueAEncoderButtonInfo.pin = 1; // TCA6408A_B pin 1
    HueAEncoderButtonInfo.active_low = false;
    HueAEncoderButtonInfo.debounce_time_ms = CONFIG_BUTTON_DEBOUNCE_TIME_MS;
    HueAEncoderButtonInfo.button = nullptr;
    HueAEncoderButtonInfo.generalHandler = nullptr;

    ButtonInfo HueBEncoderButtonInfo;
    HueBEncoderButtonInfo.name = "HueBEncoderButton";
    HueBEncoderButtonInfo.pin = 0; // TCA6408A_B pin 0
    HueBEncoderButtonInfo.active_low = false;
    HueBEncoderButtonInfo.debounce_time_ms = CONFIG_BUTTON_DEBOUNCE_TIME_MS;
    HueBEncoderButtonInfo.button = nullptr;
    HueBEncoderButtonInfo.generalHandler = nullptr;

    // Add buttons to map
    buttons[ButtonId::BLUE_BUTTON] = std::move(blueButtonInfo);
    buttons[ButtonId::RED_BUTTON] = std::move(redButtonInfo);
    buttons[ButtonId::GREEN_BUTTON] = std::move(greenButtonInfo);
    buttons[ButtonId::WHITE_BUTTON] = std::move(whiteButtonInfo);
    buttons[ButtonId::HUE_A_ENCODER_BUTTON] = std::move(HueAEncoderButtonInfo);
    buttons[ButtonId::HUE_B_ENCODER_BUTTON] = std::move(HueBEncoderButtonInfo);
    
    // Initialize the button LED map - which pins on TCA6408A_B correspond to which button LEDs
    // Regular buttons have corresponding LEDs with the same pin numbers
    // Note: Encoder buttons don't have corresponding LEDs as they share the same pins on TCA6408A_B
    buttonLedPins[ButtonId::RED_BUTTON] = 2;    // Red button LED on TCA6408A_B pin 2
    buttonLedPins[ButtonId::BLUE_BUTTON] = 3;   // Blue button LED on TCA6408A_B pin 3
    buttonLedPins[ButtonId::GREEN_BUTTON] = 4; // Green button LED on TCA6408A_B pin 4
    buttonLedPins[ButtonId::WHITE_BUTTON] = 5; // White button LED on TCA6408A_B pin 5
    
    ESP_LOGI(TAG, "Button LED map initialized: RED_BUTTON -> pin %d, BLUE_BUTTON -> pin %d", 
             buttonLedPins[ButtonId::RED_BUTTON], buttonLedPins[ButtonId::BLUE_BUTTON]);
             
    // Configure all button LED pins as outputs
    esp_err_t led_config_ret = configureButtonLEDPins();
    if (led_config_ret != ESP_OK) {
        ESP_LOGW(TAG, "Some button LED pins could not be configured: %s", esp_err_to_name(led_config_ret));
        // Continue anyway, individual pin configuration will be attempted when setButtonLED is called
    } else {
        ESP_LOGI(TAG, "All button LED pins configured as outputs successfully");
    }
   
    esp_err_t last_error = ESP_OK;
    
    // Create and initialize all buttons
    for (auto &[buttonId, buttonInfo] : buttons)
    {
        // Create button configuration
        Button::Config config;
        config.name = buttonInfo.name;
        
        // Use different TCA6408A instances for different buttons
        // Regular buttons use TCA6408A_A (with pullups)
        // Encoder buttons use TCA6408A_B (without pullups)
        if (buttonId == ButtonId::HUE_A_ENCODER_BUTTON || buttonId == ButtonId::HUE_B_ENCODER_BUTTON) {
            config.i2c_expander = i2cExpanderB_;  // Encoder buttons on TCA6408A_B
        } else {
            config.i2c_expander = i2cExpanderA_;  // Regular buttons on TCA6408A_A
        }
        
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
            last_error = ret;  // Save error but continue with other buttons
            continue;
        }

        // Register callback for this button
        buttonInfo.button->registerCallback([this, buttonId](Button::Event event)
                                            { this->handleButtonEvent(buttonId, event); });
    }
    ESP_LOGI(TAG, "Buttons initialized successfully");
    
    return last_error;  // Return the last error encountered or ESP_OK if all went well
}

esp_err_t InputManager::initPotentiometers()
{
    ESP_LOGI(TAG, "Initializing Potentiometers");
    
    // Verify ADC is available
    if (!adsAdc_) {
        ESP_LOGE(TAG, "Cannot initialize potentiometers without ADS1015 ADC");
        return ESP_ERR_INVALID_STATE;
    }
    
    // First, set up the brightness potentiometer
    PotInfo brightnessPotInfo;
    brightnessPotInfo.name = "BrightnessPot";
    brightnessPotInfo.poll_interval_ms = POT_POLL_INTERVAL_MS;
    brightnessPotInfo.change_threshold = POT_CHANGE_THRESHOLD;
    brightnessPotInfo.enable_center_event = true;
    brightnessPotInfo.center_threshold = POT_CENTER_THRESHOLD;
    brightnessPotInfo.generalHandler = nullptr;
      
    // Configure brightness potentiometer to use ADS1015 channel 0
    brightnessPotInfo.type = PotType::I2C_ADS1015;
    brightnessPotInfo.i2c_channel = ADS1015::Channel::CHANNEL_0;
    brightnessPotInfo.i2c_gain = ADS1015::Gain::GAIN_ONE;  // ±4.096V range
    
    // Add to potentiometer map
    potentiometers[PotentiometerId::BRIGHTNESS_POT] = std::move(brightnessPotInfo);
    
    // Create and initialize I2C potentiometer
    auto &brightnessPot = potentiometers[PotentiometerId::BRIGHTNESS_POT];
    
    // Create I2C potentiometer configuration
    I2CPotentiometer::Config brightnessPotConfig;
    brightnessPotConfig.name = brightnessPot.name;
    brightnessPotConfig.adc = adsAdc_;
    brightnessPotConfig.channel = brightnessPot.i2c_channel;
    brightnessPotConfig.gain = brightnessPot.i2c_gain;
    brightnessPotConfig.poll_interval_ms = brightnessPot.poll_interval_ms;
    brightnessPotConfig.change_threshold = brightnessPot.change_threshold;
    brightnessPotConfig.enable_center_event = brightnessPot.enable_center_event;
    brightnessPotConfig.center_threshold = brightnessPot.center_threshold;
    brightnessPotConfig.use_cumulative_tracking = true; // Enable cumulative change tracking for better responsiveness
    
    // Create I2C potentiometer instance
    brightnessPot.i2c_pot = std::make_unique<I2CPotentiometer>(brightnessPotConfig);
    
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

    // Now set up the speed potentiometer
    PotInfo speedPotInfo;
    speedPotInfo.name = "SpeedPot";
    speedPotInfo.poll_interval_ms = POT_POLL_INTERVAL_MS;
    speedPotInfo.change_threshold = POT_CHANGE_THRESHOLD;
    speedPotInfo.enable_center_event = true;
    speedPotInfo.center_threshold = POT_CENTER_THRESHOLD;
    speedPotInfo.generalHandler = nullptr;
        
    // Configure brightness potentiometer to use ADS1015 channel 0
    speedPotInfo.type = PotType::I2C_ADS1015;
    speedPotInfo.i2c_channel = ADS1015::Channel::CHANNEL_1;
    speedPotInfo.i2c_gain = ADS1015::Gain::GAIN_ONE;  // ±4.096V range
    
    // Add to potentiometer map
    potentiometers[PotentiometerId::SPEED_POT] = std::move(speedPotInfo);
    
    // Create and initialize I2C potentiometer
    auto &speedPot = potentiometers[PotentiometerId::SPEED_POT];
    
    // Create I2C potentiometer configuration
    I2CPotentiometer::Config speedPotConfig;
    speedPotConfig.name = speedPot.name;
    speedPotConfig.adc = adsAdc_;
    speedPotConfig.channel = speedPot.i2c_channel;
    speedPotConfig.gain = speedPot.i2c_gain;
    speedPotConfig.poll_interval_ms = speedPot.poll_interval_ms;
    speedPotConfig.change_threshold = speedPot.change_threshold;
    speedPotConfig.enable_center_event = speedPot.enable_center_event;
    speedPotConfig.center_threshold = speedPot.center_threshold;
    speedPotConfig.use_cumulative_tracking = true; // Enable cumulative change tracking for better responsiveness
    
    // Create I2C potentiometer instance
    speedPot.i2c_pot = std::make_unique<I2CPotentiometer>(speedPotConfig);
    
    if (!speedPot.i2c_pot->init())
    {
        ESP_LOGE(TAG, "Failed to initialize I2C speed potentiometer");
        return ESP_FAIL;
    }
    
    // Register callback
    speedPot.i2c_pot->registerCallback([this](I2CPotentiometer::Event event, uint32_t value, float percentage)
        { this->handlePotEvent(PotentiometerId::SPEED_POT, 
            static_cast<Potentiometer::Event>(static_cast<int>(event)), value, percentage); });
    
    // Start monitoring
    speedPot.i2c_pot->start();
    
    ESP_LOGI(TAG, "Initialized I2C potentiometer '%s' on ADS1015 channel %d",
                speedPot.name.c_str(), static_cast<int>(speedPot.i2c_channel));
                
    return ESP_OK;
}

esp_err_t InputManager::initEncoders()
{
    ESP_LOGI(TAG, "Initializing Encoders");
    
    // Initialize hue A encoder
    EncoderInfo hueAEncoderInfo;
    hueAEncoderInfo.name = "HueAEncoder";
    hueAEncoderInfo.a_pin = HUE_A_ENCODER_A_PIN;
    hueAEncoderInfo.b_pin = HUE_A_ENCODER_B_PIN;
    hueAEncoderInfo.encoder = nullptr;
    hueAEncoderInfo.generalHandler = nullptr;

    // Add encoders to map
    encoders[EncoderId::HUE_A_ENCODER] = std::move(hueAEncoderInfo);

    // Create and initialize encoders
    auto& hueAEncoder = encoders[EncoderId::HUE_A_ENCODER];

    // Create a properly configured Encoder::Config
    Encoder::Config hueAEncoderConfig;
    hueAEncoderConfig.name = hueAEncoder.name;
    hueAEncoderConfig.debounce_ms = ENCODER_DEBOUNCE_MS; // Use a default debounce time
    hueAEncoderConfig.a_pin = hueAEncoder.a_pin;
    hueAEncoderConfig.b_pin = hueAEncoder.b_pin;
    hueAEncoderConfig.velocity_scaling = 10;    // Divide velocity by 10 to get step scaling

    // Create the encoder
    hueAEncoder.encoder = std::make_unique<Encoder>(hueAEncoderConfig);

    if (hueAEncoder.encoder->init()) {
        // Register callback for encoder
        hueAEncoder.encoder->registerCallback([this](Encoder::Event event, int32_t position) {
            this->handleEncoderEvent(EncoderId::HUE_A_ENCODER, event, position);
        });

        // Start monitoring encoder
        hueAEncoder.encoder->start();

        ESP_LOGI(TAG, "Initialized encoder '%s' on GPIO A:%ld B:%ld",
                 hueAEncoder.name.c_str(), hueAEncoder.a_pin, hueAEncoder.b_pin);
    } else {
        ESP_LOGE(TAG, "Failed to initialize hue A encoder");
    }

    // Configure encoder RGB LED (common anode)
    LEDManager::RGBLEDConfig encoderHueALedConfig;
    encoderHueALedConfig.name = "EncoderALed";
    encoderHueALedConfig.red_pin = HUE_A_ENCODER_RGB_RED_PIN;    // GPIO pin for red
    encoderHueALedConfig.green_pin = HUE_A_ENCODER_RGB_GREEN_PIN;  // GPIO pin for green
    encoderHueALedConfig.blue_pin = HUE_A_ENCODER_RGB_BLUE_PIN;   // GPIO pin for blue
    encoderHueALedConfig.red_channel = LEDC_CHANNEL_0;    // LEDC channel for red
    encoderHueALedConfig.green_channel = LEDC_CHANNEL_1;  // LEDC channel for green
    encoderHueALedConfig.blue_channel = LEDC_CHANNEL_2;   // LEDC channel for blue
    encoderHueALedConfig.common_anode = true;  // Common anode RGB LED
    
    // Register the encoder LED
    esp_err_t err = LEDManager::getInstance().registerLED(LEDManager::LEDId::ENCODER_A_RGB, encoderHueALedConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register encoder hue A LED: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Encoder hue A RGB LED registered successfully");
   
    // Note: Animation will be started by setEncodersEnabled() when appropriate pattern is active

    // Initialize hue B encoder
    EncoderInfo hueBEncoderInfo;
    hueBEncoderInfo.name = "hueBEncoder";
    hueBEncoderInfo.a_pin = HUE_B_ENCODER_A_PIN;
    hueBEncoderInfo.b_pin = HUE_B_ENCODER_B_PIN;
    hueBEncoderInfo.encoder = nullptr;
    hueBEncoderInfo.generalHandler = nullptr;

    // Add encoders to map
    encoders[EncoderId::HUE_B_ENCODER] = std::move(hueBEncoderInfo);

    // Create and initialize encoders
    auto& hueBEncoder = encoders[EncoderId::HUE_B_ENCODER];

    // Create a properly configured Encoder::Config
    Encoder::Config hueBEncoderConfig;
    hueBEncoderConfig.name = hueBEncoder.name;
    hueBEncoderConfig.debounce_ms = ENCODER_DEBOUNCE_MS;
    hueBEncoderConfig.a_pin = hueBEncoder.a_pin;
    hueBEncoderConfig.b_pin = hueBEncoder.b_pin;
    hueBEncoderConfig.velocity_scaling = 10;      // Divide velocity by 10 to get step scaling

    // Create the encoder
    hueBEncoder.encoder = std::make_unique<Encoder>(hueBEncoderConfig);

    if (hueBEncoder.encoder->init()) {
        // Register callback for encoder
        hueBEncoder.encoder->registerCallback([this](Encoder::Event event, int32_t position) {
            this->handleEncoderEvent(EncoderId::HUE_B_ENCODER, event, position);
        });

        // Start monitoring encoder
        hueBEncoder.encoder->start();

        ESP_LOGI(TAG, "Initialized encoder '%s' on GPIO A:%ld B:%ld",
                 hueBEncoder.name.c_str(), hueBEncoder.a_pin, hueBEncoder.b_pin);
    } else {
        ESP_LOGE(TAG, "Failed to initialize hue B encoder");
    }
    
    // Configure encoder RGB LED (common anode)
    LEDManager::RGBLEDConfig encoderHueBLedConfig;
    encoderHueBLedConfig.name = "EncoderBLed";
    encoderHueBLedConfig.red_pin = HUE_B_ENCODER_RGB_RED_PIN;    // GPIO pin for red
    encoderHueBLedConfig.green_pin = HUE_B_ENCODER_RGB_GREEN_PIN;  // GPIO pin for green
    encoderHueBLedConfig.blue_pin = HUE_B_ENCODER_RGB_BLUE_PIN;   // GPIO pin for blue
    encoderHueBLedConfig.red_channel = LEDC_CHANNEL_3;    // LEDC channel for red
    encoderHueBLedConfig.green_channel = LEDC_CHANNEL_4;  // LEDC channel for green
    encoderHueBLedConfig.blue_channel = LEDC_CHANNEL_5;   // LEDC channel for blue
    encoderHueBLedConfig.common_anode = true;  // Common anode RGB LED
    
    // Register the encoder LED
    err = LEDManager::getInstance().registerLED(LEDManager::LEDId::ENCODER_B_RGB, encoderHueBLedConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register hue B encoder LED: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Encoder hue B RGB LED registered successfully");
   
    // Note: Animation will be started by setEncodersEnabled() when appropriate pattern is active
    
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

    // Clean up devices before deleting the I2C bus
    
    // Stop TCA6408A_A monitoring
    if (i2cExpanderA_) {
        ESP_LOGI(TAG, "Stopping TCA6408A_A GPIO expander monitoring");
        i2cExpanderA_->stopMonitoring();
    }
    
    // Stop TCA6408A_B monitoring
    if (i2cExpanderB_) {
        ESP_LOGI(TAG, "Stopping TCA6408A_B GPIO expander monitoring");
        i2cExpanderB_->stopMonitoring();
    }
    
    // Release shared device pointers in the correct order
    adsAdc_.reset();       // Free ADS1015 first
    i2cExpanderA_.reset(); // Free TCA6408A instances
    i2cExpanderB_.reset();

    // Delete the I2C bus last, after all devices using it are gone
    if (i2c_bus_ != nullptr) {
        ESP_LOGI(TAG, "Deleting I2C master bus");
        esp_err_t ret = i2c_del_master_bus(i2c_bus_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete I2C master bus: %s", esp_err_to_name(ret));
        }
        i2c_bus_ = nullptr;
    }

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
    case ButtonId::GREEN_BUTTON:
        return "GREEN_BUTTON";
    case ButtonId::WHITE_BUTTON:
        return "WHITE_BUTTON";
    case ButtonId::HUE_A_ENCODER_BUTTON:
        return "HUE_A_ENCODER_BUTTON";
    case ButtonId::HUE_B_ENCODER_BUTTON:
        return "HUE_B_ENCODER_BUTTON";
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
    case EncoderId::HUE_A_ENCODER:
        return "HUE_A_ENCODER";
    case EncoderId::HUE_B_ENCODER:
        return "HUE_B_ENCODER";
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

//=== Button LED Methods ===//

esp_err_t InputManager::setButtonLED(ButtonId buttonId, bool state)
{
    if (i2cExpanderB_ == nullptr) {
        ESP_LOGE(TAG, "Cannot control button LED: TCA6408A_B not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Find the LED pin mapping for this button
    auto ledPinIt = buttonLedPins.find(buttonId);
    if (ledPinIt == buttonLedPins.end()) {
        ESP_LOGE(TAG, "Button ID %s does not have a corresponding LED pin mapping", buttonIdToString(buttonId));
        return ESP_ERR_NOT_FOUND;
    }
    
    uint8_t pin = ledPinIt->second;
    
    // Find the button name for logging purposes
    auto buttonIt = buttons.find(buttonId);
    std::string buttonName = (buttonIt != buttons.end()) ? buttonIt->second.name : buttonIdToString(buttonId);
    
    // Double check that the pin is configured as output (shouldn't be needed if configureButtonLEDPins was called during init)
    esp_err_t ret = i2cExpanderB_->configurePin(pin, true); // true = output
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure pin %d as output for %s LED: %s", 
                 pin, buttonName.c_str(), esp_err_to_name(ret));
        return ret;
    }
    
    // Set the pin state
    ESP_LOGI(TAG, "Setting %s LED (pin %d) to %s", buttonName.c_str(), pin, state ? "ON" : "OFF");
    return i2cExpanderB_->writePin(pin, state ? 1 : 0);
}

esp_err_t InputManager::setButtonLEDPin(ButtonId buttonId, uint8_t pin)
{
    // Check if this button already has a mapped LED pin
    auto existingPin = buttonLedPins.find(buttonId);
    if (existingPin != buttonLedPins.end()) {
        ESP_LOGW(TAG, "Button ID %s already has LED pin %d, overwriting with pin %d", 
                 buttonIdToString(buttonId), existingPin->second, pin);
    }
    
    // Map the button ID to the LED pin
    buttonLedPins[buttonId] = pin;
    ESP_LOGI(TAG, "Mapped button ID %s to LED pin %d", buttonIdToString(buttonId), pin);
    
    // If i2cExpanderB_ is initialized, configure the pin as output immediately
    if (i2cExpanderB_ != nullptr) {
        esp_err_t ret = i2cExpanderB_->configurePin(pin, true); // true = output
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure pin %d as output for button LED: %s", 
                     pin, esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Configured pin %d as output for button LED", pin);
    }
    
    return ESP_OK;
}

esp_err_t InputManager::configureButtonLEDPins()
{
    if (i2cExpanderB_ == nullptr) {
        ESP_LOGE(TAG, "Cannot configure button LED pins: TCA6408A_B not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Configuring all mapped button LED pins as outputs");
    esp_err_t lastError = ESP_OK;
    
    // Iterate through all button LED pins and configure them as outputs
    for (const auto& [buttonId, pin] : buttonLedPins) {
        esp_err_t ret = i2cExpanderB_->configurePin(pin, true); // true = output
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure pin %d as output for button %s LED: %s", 
                     pin, buttonIdToString(buttonId), esp_err_to_name(ret));
            lastError = ret; // Keep the last error but continue trying to configure other pins
        } else {
            ESP_LOGI(TAG, "Configured pin %d as output for button %s LED", pin, buttonIdToString(buttonId));
        }
    }
    
    return lastError;
}

esp_err_t InputManager::clearAllButtonLEDs()
{
    if (i2cExpanderB_ == nullptr) {
        ESP_LOGE(TAG, "Cannot clear button LEDs: TCA6408A_B not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t lastError = ESP_OK;
    
    // Iterate through all button LED pins and turn them off
    for (const auto& [buttonId, pin] : buttonLedPins) {
        esp_err_t ret = i2cExpanderB_->writePin(pin, 0); // 0 = off
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to turn off button %s LED (pin %d): %s", 
                     buttonIdToString(buttonId), pin, esp_err_to_name(ret));
            lastError = ret; // Keep the last error but continue trying other pins
        } else {
            ESP_LOGD(TAG, "Turned off button %s LED (pin %d)", buttonIdToString(buttonId), pin);
        }
    }
    
    return lastError;
}

esp_err_t InputManager::runButtonLEDDiagnostic(uint32_t duration_ms)
{
    if (i2cExpanderB_ == nullptr) {
        ESP_LOGE(TAG, "Cannot run button LED diagnostic: TCA6408A_B not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting button LED diagnostic sequence (duration: %lu ms per LED)", duration_ms);
    
    // First, ensure all LEDs are off
    esp_err_t ret = clearAllButtonLEDs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear all LEDs before diagnostic: %s", esp_err_to_name(ret));
        // Continue anyway
    }
    
    // Define the sequence of buttons to light up
    std::vector<ButtonId> sequence = {
        ButtonId::RED_BUTTON,
        ButtonId::GREEN_BUTTON,
        ButtonId::BLUE_BUTTON,
        ButtonId::WHITE_BUTTON
    };
    
    // Light each LED in sequence
    for (auto buttonId : sequence) {
        // Check if this button has an LED mapping
        auto ledPinIt = buttonLedPins.find(buttonId);
        if (ledPinIt == buttonLedPins.end()) {
            ESP_LOGW(TAG, "Button %s does not have an LED mapping, skipping in diagnostic", 
                     buttonIdToString(buttonId));
            continue;
        }
        
        // Turn on the LED
        ESP_LOGI(TAG, "Turning on %s LED for %lu ms", buttonIdToString(buttonId), duration_ms);
        ret = setButtonLED(buttonId, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to turn on %s LED: %s", buttonIdToString(buttonId), esp_err_to_name(ret));
            continue;
        }
        
        // Wait for the specified duration
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        
        // Turn off the LED
        ret = setButtonLED(buttonId, false);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to turn off %s LED: %s", buttonIdToString(buttonId), esp_err_to_name(ret));
        }
    }
    
    ESP_LOGI(TAG, "Button LED diagnostic sequence completed");
    return ESP_OK;
}

esp_err_t InputManager::runEncoderLEDHueDiagnostic(uint32_t duration_ms, uint16_t steps)
{
    ESP_LOGI(TAG, "Starting encoder RGB LED hue cycling diagnostic (duration: %lu ms, steps: %u)", duration_ms, steps);
    
    // Access LEDManager singleton
    auto& ledManager = LEDManager::getInstance();
    
    // Define the encoder LEDs to cycle through
    std::vector<LEDManager::LEDId> encoderLEDs = {
        LEDManager::LEDId::ENCODER_A_RGB,
        LEDManager::LEDId::ENCODER_B_RGB
    };
    
    // Calculate delay between steps
    uint32_t step_delay_ms = duration_ms / steps;
    if (step_delay_ms < 1) step_delay_ms = 1; // Ensure at least 1ms per step
    
    // First set all encoder LEDs to full brightness
    for (auto ledId : encoderLEDs) {
        esp_err_t ret = ledManager.setBrightness(ledId, 100);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set encoder LED brightness: %s", esp_err_to_name(ret));
            // Continue anyway
        }
    }
    
    // Cycle through all hues
    for (uint16_t hue = 0; hue < 360; hue += (360 / steps)) {
        LEDManager::HSV color(hue, 100, 100); // Full saturation and brightness
        
        // Apply the same color to all encoder LEDs
        for (auto ledId : encoderLEDs) {
            esp_err_t ret = ledManager.setHSV(ledId, color);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to set encoder LED color (hue: %u): %s", 
                         hue, esp_err_to_name(ret));
                // Continue to next LED
            }
        }
        
        // Wait for the step delay
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
    }
    
    // Set LEDs to off when finished (black color)
    LEDManager::RGB off_color(0, 0, 0);
    for (auto ledId : encoderLEDs) {
        esp_err_t ret = ledManager.setRGB(ledId, off_color);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to turn off encoder LED: %s", esp_err_to_name(ret));
            // Continue anyway
        }
    }
    
    ESP_LOGI(TAG, "Encoder RGB LED hue cycling diagnostic completed");
    return ESP_OK;
}

//=== Pattern State Management Methods ===//

esp_err_t InputManager::setActivePattern(ButtonId buttonId, PatternType patternType)
{
    ESP_LOGI(TAG, "Setting active pattern: %s, button: %s", 
             getPatternName(patternType), buttonIdToString(buttonId));
    
    // Store the new state
    activeButtonId_ = buttonId;
    activePattern_ = patternType;
    
    // Clear all button LEDs first
    esp_err_t err = clearAllButtonLEDs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear button LEDs when setting active pattern: %s", 
                 esp_err_to_name(err));
        // Continue anyway
    }
    
    // Light up the active button's LED
    err = setButtonLED(buttonId, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set active button LED for %s: %s", 
                 buttonIdToString(buttonId), esp_err_to_name(err));
        return err;
    }
    
    // Enable/disable encoders based on the active pattern
    // Only enable encoders for CHROMA_WAVE pattern (check pattern, not just button)
    bool shouldEnableEncoders = (patternType == PatternType::CHROMA_WAVE);
    ESP_LOGI(TAG, "%s encoders for pattern %s (button: %s)", 
             shouldEnableEncoders ? "Enabling" : "Disabling",
             getPatternName(patternType),
             buttonIdToString(buttonId));
    return setEncodersEnabled(shouldEnableEncoders);
}

PatternType InputManager::getActivePattern() const
{
    return activePattern_;
}

ButtonId InputManager::getActiveButton() const
{
    return activeButtonId_;
}

bool InputManager::areEncodersEnabled() const
{
    return encodersEnabled_;
}

esp_err_t InputManager::setEncodersEnabled(bool enable)
{
    // Record previous state for logging
    bool wasChanged = (encodersEnabled_ != enable);
    
    // Always log the operation even if no change, helpful for debugging initialization
    ESP_LOGI(TAG, "%s encoders (state change: %s)", 
             enable ? "Enabling" : "Disabling", 
             wasChanged ? "yes" : "no");
    
    // Update internal state
    encodersEnabled_ = enable;
    
    esp_err_t lastError = ESP_OK;
    
    if (enable) {
        // Always set up encoder LEDs when enabled, regardless of previous state
        ESP_LOGI(TAG, "Setting up encoder LED animations for pattern %s", getPatternName(activePattern_));
        
        // Use stored hue values to restore encoder colors
        ESP_LOGI(TAG, "Restoring encoder hue values: A=%u°, B=%u°", lastEncoderAHue_, lastEncoderBHue_);
        
        // Configure encoder LEDs with their saved colors
        LEDManager::getInstance().setHSV(LEDManager::LEDId::ENCODER_A_RGB, 
                                         LEDManager::HSV(lastEncoderAHue_, 100, 100));
        LEDManager::getInstance().setHSV(LEDManager::LEDId::ENCODER_B_RGB, 
                                         LEDManager::HSV(lastEncoderBHue_, 100, 100));
        
        // Configure and start animations
        for (const auto& [encoderId, ledId] : {
                std::make_pair(EncoderId::HUE_A_ENCODER, LEDManager::LEDId::ENCODER_A_RGB),
                std::make_pair(EncoderId::HUE_B_ENCODER, LEDManager::LEDId::ENCODER_B_RGB)
            }) {
            // Always stop existing animations to ensure a clean start
            if (LEDManager::getInstance().isAnimationRunning(ledId)) {
                ESP_LOGI(TAG, "Stopping existing animation for encoder %s", encoderIdToString(encoderId));
                LEDManager::getInstance().stopAnimation(ledId);
            }
            
            // Configure and start fresh breathing animation
            LEDManager::AnimationConfig breathingConfig;
            breathingConfig.type = LEDManager::AnimationType::BREATHING;
            breathingConfig.duration_ms = 800;  // 800ms per breathing cycle
            breathingConfig.repeat_count = 0;   // Run continuously
            
            ESP_LOGI(TAG, "Starting breathing animation for encoder %s", encoderIdToString(encoderId));
            esp_err_t err = LEDManager::getInstance().startAnimation(ledId, breathingConfig);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start animation for encoder %s: %s", 
                        encoderIdToString(encoderId), esp_err_to_name(err));
                lastError = err;
            } else {
                ESP_LOGI(TAG, "Animation started successfully for encoder %s", encoderIdToString(encoderId));
            }
        }
    } else {
        // Save the current encoder colors before disabling them
        auto hueA = LEDManager::getInstance().getCurrentColorHSV(LEDManager::LEDId::ENCODER_A_RGB);
        auto hueB = LEDManager::getInstance().getCurrentColorHSV(LEDManager::LEDId::ENCODER_B_RGB);
        
        // Only save if brightness is non-zero (LED is active)
        if (hueA.v > 0) {
            lastEncoderAHue_ = hueA.h;
        }
        if (hueB.v > 0) {
            lastEncoderBHue_ = hueB.h;
        }
        
        ESP_LOGI(TAG, "Stored encoder hue values: A=%u°, B=%u°", lastEncoderAHue_, lastEncoderBHue_);
        
        // Disable encoder LEDs by turning them off
        for (const auto& [encoderId, ledId] : {
                std::make_pair(EncoderId::HUE_A_ENCODER, LEDManager::LEDId::ENCODER_A_RGB),
                std::make_pair(EncoderId::HUE_B_ENCODER, LEDManager::LEDId::ENCODER_B_RGB)
            }) {
            // Stop any running animations
            if (LEDManager::getInstance().isAnimationRunning(ledId)) {
                ESP_LOGI(TAG, "Stopping animation for encoder %s", encoderIdToString(encoderId));
                LEDManager::getInstance().stopAnimation(ledId);
            }
            
            // Turn off the LED by setting brightness to 0
            esp_err_t err = LEDManager::getInstance().setHSV(ledId, LEDManager::HSV(0, 0, 0));
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to turn off LED for encoder %s: %s", 
                        encoderIdToString(encoderId), esp_err_to_name(err));
                lastError = err;
            }
        }
    }
    
    return lastError;
}
