#include "Sender.h"
#include "InputManager.h"
#include "config.h"
#include "esp_log.h"
#include <algorithm> // For min/max

// This file contains the implementation of input handler setup for the Sender class
// Moving this code to a separate file improves organization while keeping it part of the Sender class

// Setup handlers for all input devices
void Sender::setupInputHandlers() {
    InputManager& inputManager = InputManager::getInstance();
    static const char *TAG = "InputHandler";

    esp_log_level_set(TAG, INPUTMANAGER_LOG_LEVEL); // Set log level for this module
    
    // ====================
    // Register button handlers
    // ====================

    // Register handler for blue button press
    inputManager.registerButtonHandler(ButtonId::BLUE_BUTTON, Button::Event::PRESSED, [this]() {
        ESP_LOGI(TAG, "Blue button action: Sending SHAKEEL_FLASH pattern command");
        PatternType pattern = PatternType::SHAKEEL_FLASH;
        sendPatternChange(pattern);
        // Update the UI state to reflect new pattern
        InputManager::getInstance().setActivePattern(ButtonId::BLUE_BUTTON, pattern);
    });

    // Register handler for red button press
    inputManager.registerButtonHandler(ButtonId::RED_BUTTON, Button::Event::PRESSED, [this]() {
        ESP_LOGI(TAG, "Red button action: Sending CHROMA_WAVE pattern command");
        PatternType pattern = PatternType::CHROMA_WAVE;
        sendPatternChange(pattern);
        // Update the UI state to reflect new pattern
        InputManager::getInstance().setActivePattern(ButtonId::RED_BUTTON, pattern);
    });

    // Register handler for green button press
    inputManager.registerButtonHandler(ButtonId::GREEN_BUTTON, Button::Event::PRESSED, [this]() {
        ESP_LOGI(TAG, "Green button action: Sending SHAKEEL_FLASH_BALL pattern command");
        PatternType pattern = PatternType::SHAKEEL_FLASH_BALL;
        sendPatternChange(pattern);
        // Update the UI state to reflect new pattern
        InputManager::getInstance().setActivePattern(ButtonId::GREEN_BUTTON, pattern);
    });

    // Register handler for white button press
    inputManager.registerButtonHandler(ButtonId::WHITE_BUTTON, Button::Event::PRESSED, [this]() {
        ESP_LOGI(TAG, "White button action: Sending FLASH pattern command");
        // Store the previous pattern before changing to FLASH
        previousPattern = InputManager::getInstance().getActivePattern();
        previousActiveButton = InputManager::getInstance().getActiveButton();
        PatternType pattern = PatternType::FLASH;
        sendPatternChange(pattern);
        // Update the UI state to reflect new pattern
        InputManager::getInstance().setActivePattern(ButtonId::WHITE_BUTTON, pattern);
    });

    inputManager.registerButtonHandler(ButtonId::WHITE_BUTTON, Button::Event::RELEASED, [this]() {
        // If the previous pattern was set, restore it
        if (previousPattern != PatternType::NONE) {
            ESP_LOGI(TAG, "White button released: Restoring previous pattern %d", static_cast<int>(previousPattern));
            sendPatternChange(previousPattern);
            InputManager::getInstance().setActivePattern(previousActiveButton, previousPattern);
            previousPattern = PatternType::NONE; // Reset after restoring
        }
    });

    // ====================
    // Register potentiometer handlers
    // ====================
    
    // Brightness potentiometer
    inputManager.registerPotHandler(PotentiometerId::BRIGHTNESS_POT, Potentiometer::Event::VALUE_CHANGED, 
        [this](uint32_t value, float percentage) {
            ESP_LOGI(TAG, "Brightness changed to %.1f%%", percentage);
            sendBrightnessChange(static_cast<uint8_t>(percentage));
        });

    // Speed potentiometer

    inputManager.registerPotHandler(PotentiometerId::SPEED_POT, Potentiometer::Event::VALUE_CHANGED, 
        [this](uint32_t value, float percentage) {
            ESP_LOGI(TAG, "Speed changed to %.1f%%", percentage);
            sendSpeedChange(static_cast<uint8_t>(percentage));
        });
    
    // ====================
    // Encoder handlers
    // ====================


    // Hue Encoder A
    // This encoder adjusts the other hue for the FullBar Pattern

    // Start encode A with yellow hue
    static uint16_t encoderAHue = 60;
    
    // Clockwise rotation increases hue
    inputManager.registerEncoderHandler(EncoderId::HUE_A_ENCODER, Encoder::Event::CLOCKWISE, 
        [this](int32_t position) {
            // Check if encoders are enabled for the current pattern
            if (!InputManager::getInstance().areEncodersEnabled()) {
                ESP_LOGD(TAG, "Ignoring HueA encoder event - encoders disabled for current pattern");
                return;
            }
            
            // If the animation isn't running, start it as we have moved the encoder.
            if (!LEDManager::getInstance().isAnimationRunning(LEDManager::LEDId::ENCODER_A_RGB)) {
                ESP_LOGI(TAG, "Starting animation for Hue A encoder");
                LEDManager::AnimationConfig breathingConfig;
                breathingConfig.type = LEDManager::AnimationType::BREATHING;
                breathingConfig.duration_ms = 800;  // 800 ms per breathing cycle for a smooth effect
                breathingConfig.repeat_count = 0;    // Run continuously until stopped
                
                // Start the animation with the current hue
                LEDManager::getInstance().startAnimation(LEDManager::LEDId::ENCODER_A_RGB, breathingConfig);
            }

            // Each encoder step is 5 degrees of hue
            // Access static variable directly without capture
            encoderAHue = (encoderAHue + 5) % 360;
            ESP_LOGI(TAG, "HueA: Hue changed to %u degrees", encoderAHue);
                      
            // Update the local RGB LED - using 100% saturation for vibrant colors
            // This will update the LED's stored HSV state which the animation will use
            LEDManager::getInstance().setHSV(LEDManager::LEDId::ENCODER_A_RGB, 
                LEDManager::HSV(encoderAHue, 100, 100)); // Update encoder RGB LED with full saturation
        });
    
    // Counter-clockwise rotation decreases hue
    inputManager.registerEncoderHandler(EncoderId::HUE_A_ENCODER, Encoder::Event::COUNTER_CLOCKWISE, 
        [this](int32_t position) {
            // Check if encoders are enabled for the current pattern
            if (!InputManager::getInstance().areEncodersEnabled()) {
                ESP_LOGD(TAG, "Ignoring HueA encoder event - encoders disabled for current pattern");
                return;
            }

            // If the animation isn't running, start it as we have moved the encoder.
            if (!LEDManager::getInstance().isAnimationRunning(LEDManager::LEDId::ENCODER_A_RGB)) {
                ESP_LOGI(TAG, "Starting animation for Hue A encoder");
                LEDManager::AnimationConfig breathingConfig;
                breathingConfig.type = LEDManager::AnimationType::BREATHING;
                breathingConfig.duration_ms = 800;  // 800 ms per breathing cycle for a smooth effect
                breathingConfig.repeat_count = 0;    // Run continuously until stopped
                
                // Start the animation with the current hue
                LEDManager::getInstance().startAnimation(LEDManager::LEDId::ENCODER_A_RGB, breathingConfig);
            }

            // Each encoder step is 5 degrees of hue (355 is equivalent to -5 in mod 360)
            // Access static variable directly without capture
            encoderAHue = (encoderAHue + 355) % 360;
            ESP_LOGI(TAG, "HueA: Hue changed to %u degrees", encoderAHue);
            
            // Update the local RGB LED - using 100% saturation for vibrant colors
            // This will update the LED's stored HSV state which the animation will use
            LEDManager::getInstance().setHSV(LEDManager::LEDId::ENCODER_A_RGB, 
                LEDManager::HSV(encoderAHue, 100, 100)); // Update encoder RGB LED with full saturation
        });

    // Register button handler for Hue A encoder
    inputManager.registerButtonHandler(ButtonId::HUE_A_ENCODER_BUTTON, Button::Event::PRESSED,
        [this]() {
            // Check if encoders are enabled for the current pattern
            if (!InputManager::getInstance().areEncodersEnabled()) {
                ESP_LOGD(TAG, "Ignoring HueA encoder button - encoders disabled for current pattern");
                return;
            }
            
            ESP_LOGD(TAG, "Pattern encoder button: Activating effect punch");
            // Get reference to LED Manager
            LEDManager& ledManager = LEDManager::getInstance();
            
            // Define the LED we're working with
            auto ledId = LEDManager::LEDId::ENCODER_A_RGB;
            
            // Check if animation is running using the public method
            if (ledManager.isAnimationRunning(ledId)) {
                // Animation is running, stop it
                ESP_LOGI(TAG, "EncoderB: toggling animation OFF, sending new hue");
                ledManager.stopAnimation(ledId);
            }

            // Send the new hue to receivers - use index 1 for primary hue
            sendHueChange(1, encoderAHue);
        });



    // Hue Encoder B
    // This encoder adjusts the other hue for the FullBar Pattern

    // Start encode B with blueish hue
    static uint16_t encoderBHue = 240;
    
    // Clockwise rotation increases hue
    inputManager.registerEncoderHandler(EncoderId::HUE_B_ENCODER, Encoder::Event::CLOCKWISE, 
        [this](int32_t position) {
            // Check if encoders are enabled for the current pattern
            if (!InputManager::getInstance().areEncodersEnabled()) {
                ESP_LOGD(TAG, "Ignoring HueB encoder event - encoders disabled for current pattern");
                return;
            }
            
            // If the animation isn't running, start it as we have moved the encoder.
            if (!LEDManager::getInstance().isAnimationRunning(LEDManager::LEDId::ENCODER_B_RGB)) {
                ESP_LOGI(TAG, "Starting animation for Hue B encoder");
                LEDManager::AnimationConfig breathingConfig;
                breathingConfig.type = LEDManager::AnimationType::BREATHING;
                breathingConfig.duration_ms = 800;  // 800 ms per breathing cycle for a smooth effect
                breathingConfig.repeat_count = 0;    // Run continuously until stopped
                
                // Start the animation with the current hue
                LEDManager::getInstance().startAnimation(LEDManager::LEDId::ENCODER_B_RGB, breathingConfig);
            }

            // Each encoder step is 5 degrees of hue
            // Access static variable directly without capture
            encoderBHue = (encoderBHue + 5) % 360;
            ESP_LOGI(TAG, "HueB: Hue changed to %u degrees", encoderBHue);
            
            // Update the local RGB LED - using 100% saturation for vibrant colors
            // This will update the LED's stored HSV state which the animation will use
            LEDManager::getInstance().setHSV(LEDManager::LEDId::ENCODER_B_RGB, 
                LEDManager::HSV(encoderBHue, 100, 100)); // Update encoder RGB LED with full saturation
        });
    
    // Counter-clockwise rotation decreases hue
    inputManager.registerEncoderHandler(EncoderId::HUE_B_ENCODER, Encoder::Event::COUNTER_CLOCKWISE, 
        [this](int32_t position) {
            // Check if encoders are enabled for the current pattern
            if (!InputManager::getInstance().areEncodersEnabled()) {
                ESP_LOGD(TAG, "Ignoring HueB encoder event - encoders disabled for current pattern");
                return;
            }
            
            // If the animation isn't running, start it as we have moved the encoder.
            if (!LEDManager::getInstance().isAnimationRunning(LEDManager::LEDId::ENCODER_B_RGB)) {
                ESP_LOGI(TAG, "Starting animation for Hue B encoder");
                LEDManager::AnimationConfig breathingConfig;
                breathingConfig.type = LEDManager::AnimationType::BREATHING;
                breathingConfig.duration_ms = 800;  // 800 ms per breathing cycle for a smooth effect
                breathingConfig.repeat_count = 0;    // Run continuously until stopped
                
                // Start the animation with the current hue
                LEDManager::getInstance().startAnimation(LEDManager::LEDId::ENCODER_B_RGB, breathingConfig);
            }

            // Each encoder step is 5 degrees of hue (355 is equivalent to -5 in mod 360)
            // Access static variable directly without capture
            encoderBHue = (encoderBHue + 355) % 360;
            ESP_LOGI(TAG, "HueB: Hue changed to %u degrees", encoderBHue);
            
            // Update the local RGB LED - using 100% saturation for vibrant colors
            // This will update the LED's stored HSV state which the animation will use
            LEDManager::getInstance().setHSV(LEDManager::LEDId::ENCODER_B_RGB, 
                LEDManager::HSV(encoderBHue, 100, 100)); // Update encoder RGB LED with full saturation
        });

    // Register button handler for Hue B encoder
    inputManager.registerButtonHandler(ButtonId::HUE_B_ENCODER_BUTTON, Button::Event::PRESSED,
        [this]() {
            // Check if encoders are enabled for the current pattern
            if (!InputManager::getInstance().areEncodersEnabled()) {
                ESP_LOGD(TAG, "Ignoring HueB encoder button - encoders disabled for current pattern");
                return;
            }
            
            ESP_LOGD(TAG, "Pattern encoder button: Activating effect punch");
            // Get reference to LED Manager
            LEDManager& ledManager = LEDManager::getInstance();
            
            // Define the LED we're working with
            auto ledId = LEDManager::LEDId::ENCODER_B_RGB;
            
            // Check if animation is running using the public method
            if (ledManager.isAnimationRunning(ledId)) {
                // Animation is running, stop it
                ESP_LOGI(TAG, "EncoderB: toggling animation OFF, sending new hue");
                ledManager.stopAnimation(ledId);
            }

            // Send the new hue to receivers - use index 0 for secondary hue
            sendHueChange(0, encoderBHue);
        });
}
