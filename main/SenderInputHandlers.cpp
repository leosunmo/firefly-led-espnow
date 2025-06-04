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
    static const char *TAG = "Sender";
    
    // ====================
    // Register button handlers
    // ====================

    // Register handler for blue button press - sends blue pattern
    inputManager.registerButtonHandler(ButtonId::BLUE_BUTTON, Button::Event::PRESSED, [this]() {
        ESP_LOGI(TAG, "Blue button action: Sending blue pattern command");
        sendPatternChange(PatternType::BluePattern);
    });

    inputManager.registerButtonHandler(ButtonId::RED_BUTTON, Button::Event::PRESSED, [this]() {
        ESP_LOGI(TAG, "Red button action: sending punch command");
        sendEffectPunch(100); // Full intensity punch effect
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

    // Speed potentiometer (uncomment when hardware is available)
    /*
    inputManager.registerPotHandler(PotentiometerId::SPEED_POT, Potentiometer::Event::VALUE_CHANGED, 
        [this](uint32_t value, float percentage) {
            ESP_LOGI(TAG, "Speed changed to %.1f%%", percentage);
            sendSpeedChange(static_cast<uint8_t>(percentage));
        });
    */
    
    // ====================
    // Encoder handler examples (commented out until hardware is available)
    // ====================
    /*
    // Color encoder for hue adjustment
    static uint16_t encoderHue = 180; // Start at cyan
    
    // Clockwise rotation increases hue
    inputManager.registerEncoderHandler(EncoderId::COLOR_ENCODER, Encoder::Event::CLOCKWISE, 
        [this](int32_t position) {
            // Each encoder step is 5 degrees of hue
            // Access static variable directly without capture
            encoderHue = (encoderHue + 5) % 360;
            ESP_LOGI(TAG, "Color encoder: Hue changed to %u degrees", encoderHue);
            sendHueChange(encoderHue);
        });
    
    // Counter-clockwise rotation decreases hue
    inputManager.registerEncoderHandler(EncoderId::COLOR_ENCODER, Encoder::Event::COUNTER_CLOCKWISE, 
        [this](int32_t position) {
            // Each encoder step is 5 degrees of hue (355 is equivalent to -5 in mod 360)
            // Access static variable directly without capture
            encoderHue = (encoderHue + 355) % 360;
            ESP_LOGI(TAG, "Color encoder: Hue changed to %u degrees", encoderHue);
            sendHueChange(encoderHue);
        });
    
    // Pattern encoder for pattern selection
    static PatternType currentPattern = PatternType::BluePattern;
    static const PatternType patterns[] = {
        PatternType::BluePattern,
        PatternType::RedPattern,
        PatternType::GreenPattern,
        PatternType::RainbowPattern,
        PatternType::FirePattern,
        PatternType::StarfieldPattern
    };
    static const int patternCount = sizeof(patterns) / sizeof(patterns[0]);
    static int patternIndex = 0;
    
    // Rotation changes pattern
    inputManager.registerEncoderHandler(EncoderId::PATTERN_ENCODER, 
        [this](Encoder::Event event, int32_t position) {
            if (event == Encoder::Event::CLOCKWISE) {
                // Access static variables directly without capture
                patternIndex = (patternIndex + 1) % patternCount;
            } else if (event == Encoder::Event::COUNTER_CLOCKWISE) {
                patternIndex = (patternIndex + patternCount - 1) % patternCount;
            } else {
                return; // Only handle rotation events here
            }
            
            currentPattern = patterns[patternIndex];
            ESP_LOGI(TAG, "Pattern encoder: Pattern changed to index %d", patternIndex);
            sendPatternChange(currentPattern);
        });
    
    // Button press activates special effect
    inputManager.registerButtonHandler(ButtonId::PATTERN_ENCODER_BUTTON, Button::Event::PRESSED,
        [this]() {
            ESP_LOGI(TAG, "Pattern encoder button: Activating effect punch");
            sendEffectPunch(100); // Full intensity effect
        });
    */
}
