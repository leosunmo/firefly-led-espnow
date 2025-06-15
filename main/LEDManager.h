#pragma once

#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "config.h"
#include <map>
#include <string>
#include <memory>
#include <functional>
#include <cmath>
#include <vector>

/**
 * @brief RGB LED Manager for controlling RGB LEDs using PWM via ESP-IDF's LEDC peripheral
 * 
 * This class provides an interface for controlling multiple RGB LEDs through PWM,
 * with support for HSV color space and centralized animation control.
 */
class LEDManager {
public:
    // Maximum duty cycle value for 8-bit resolution (0-255)
    static constexpr uint32_t MAX_DUTY = 255;
    
    /**
     * @brief RGB color structure
     */
    struct RGB {
        uint8_t r;    // Red component (0-255)
        uint8_t g;    // Green component (0-255)
        uint8_t b;    // Blue component (0-255)
        
        RGB() : r(0), g(0), b(0) {}
        RGB(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
    };
    
    /**
     * @brief HSV color structure
     */
    struct HSV {
        uint16_t h;   // Hue (0-359 degrees)
        uint8_t s;    // Saturation (0-100%)
        uint8_t v;    // Value/brightness (0-100%)
        
        HSV() : h(0), s(100), v(100) {}
        HSV(uint16_t hue, uint8_t saturation, uint8_t value) : 
            h(hue % 360), s(saturation > 100 ? 100 : saturation), v(value > 100 ? 100 : value) {}
    };
    
    /**
     * @brief RGB LED configuration
     */
    struct RGBLEDConfig {
        std::string name;               // Name for identification
        int red_pin;                    // GPIO pin for red channel
        int green_pin;                  // GPIO pin for green channel
        int blue_pin;                   // GPIO pin for blue channel
        ledc_channel_t red_channel;     // LEDC channel for red
        ledc_channel_t green_channel;   // LEDC channel for green
        ledc_channel_t blue_channel;    // LEDC channel for blue
        bool common_anode;              // true if LED is common anode (inverts PWM signal)
    };
    
    /**
     * @brief LED identifier enum for type-safe LED identification
     */
    enum class LEDId {
        ENCODER_A_RGB,  // RGB LED for Encoder A
        ENCODER_B_RGB,  // RGB LED for Encoder B
        // Add new LEDs here
    };
    
    /**
     * @brief LED animation types (simplified to focus on breathing only)
     */
    enum class AnimationType {
        NONE,           // No animation (static color)
        BREATHING,      // Breathing effect (pulsing brightness)
    };
    
    /**
     * @brief Animation configuration
     */
    struct AnimationConfig {
        AnimationType type;       // Animation type
        uint32_t duration_ms;     // Duration of one animation cycle in ms
        uint32_t repeat_count;    // Number of times to repeat (0 = infinite)
        HSV start_color;          // Starting color for animations
    };
    
    // Singleton access
    static LEDManager& getInstance();

    // Delete copy constructor and assignment operator
    LEDManager(const LEDManager&) = delete;
    LEDManager& operator=(const LEDManager&) = delete;

    /**
     * @brief Initialize the LED manager
     * @return ESP_OK on success
     */
    esp_err_t init();
    
    /**
     * @brief Register an RGB LED
     * @param ledId The LED identifier
     * @param config The RGB LED configuration
     * @return ESP_OK on success
     */
    esp_err_t registerLED(LEDId ledId, const RGBLEDConfig& config);
    
    /**
     * @brief Set the RGB color of an LED
     * @param ledId The LED identifier
     * @param color The RGB color to set
     * @return ESP_OK on success
     */
    esp_err_t setRGB(LEDId ledId, const RGB& color);
    
    /**
     * @brief Set the HSV color of an LED
     * @param ledId The LED identifier
     * @param color The HSV color to set
     * @return ESP_OK on success
     */
    esp_err_t setHSV(LEDId ledId, const HSV& color);
    
    /**
     * @brief Set LED brightness (preserving current color)
     * @param ledId The LED identifier
     * @param brightness Brightness level (0-100%)
     * @return ESP_OK on success
     */
    esp_err_t setBrightness(LEDId ledId, uint8_t brightness);
    
    /**
     * @brief Start an animation on an LED
     * @param ledId The LED identifier
     * @param config Animation configuration
     * @return ESP_OK on success
     */
    esp_err_t startAnimation(LEDId ledId, const AnimationConfig& config);
    
    /**
     * @brief Stop any active animation on an LED
     * @param ledId The LED identifier
     * @return ESP_OK on success
     */
    esp_err_t stopAnimation(LEDId ledId);
    
    /**
     * @brief Check if an LED has an animation running
     * @param ledId The LED identifier
     * @return true if the LED has an animation running, false otherwise
     */
    bool isAnimationRunning(LEDId ledId) const;
    
    /**
     * @brief Get the current HSV color of an LED
     * @param ledId The LED identifier
     * @return The current HSV color
     */
    HSV getCurrentColorHSV(LEDId ledId) const;
    
    /**
     * @brief Convert HSV color to RGB
     * @param hsv The HSV color
     * @return The equivalent RGB color
     */
    static RGB hsvToRgb(const HSV& hsv);
    
    /**
     * @brief Convert RGB color to HSV
     * @param rgb The RGB color
     * @return The equivalent HSV color
     */
    static HSV rgbToHsv(const RGB& rgb);
    
    /**
     * @brief Helper for converting enum values to strings (for logging)
     */
    static const char* ledIdToString(LEDId id);
    static const char* animationTypeToString(AnimationType type);
    
private:
    // Private constructor for singleton
    LEDManager();

    // Private destructor
    ~LEDManager();
    
    // Initialize LEDC timer
    esp_err_t initLEDCTimer();
    
    // Initialize a single LED
    esp_err_t initLED(LEDId ledId, const RGBLEDConfig& config);
    
    // Set raw duty cycle for a channel
    esp_err_t setDuty(ledc_channel_t channel, uint32_t duty);
    
    // Animation state structure
    struct AnimationState {
        LEDId ledId;                 // Which LED this animation controls
        AnimationType type;          // Type of animation
        AnimationConfig config;      // Animation configuration
        uint32_t start_time_ms;      // When animation started (ms)
        uint32_t last_update_ms;     // Last time animation was updated (ms)
        uint32_t repeat_count;       // Current repeat count
        uint8_t current_brightness;  // Current brightness value
        bool fading_in;              // Direction (true=fading in, false=fading out)
        bool running;                // Is this animation active?
        HSV initial_color;           // Initial color of the LED when animation started
        bool color_updated;          // Flag indicating the color was updated externally
        uint32_t color_update_time;  // Time when color was last updated
        bool animation_paused;       // Flag to pause animation during color selection
        uint32_t pause_until_ms;     // Time until animation should resume
        HSV calculatedColor;         // The color calculated during the latest update
        bool needsUpdate;            // Flag indicating the color needs to be updated
    };
    
    // LED related storage
    struct LEDInfo {
        std::string name;
        RGBLEDConfig config;
        HSV current_hsv;
        RGB current_rgb;
        AnimationType current_animation;
        bool animation_running;
    };
    
    // Storage for LEDs
    std::map<LEDId, LEDInfo> leds;
    
    // Animation manager task and related items
    static void animationManagerTask(void* arg);
    TaskHandle_t animation_manager_task;
    bool animation_manager_running;
    
    // Animation mutexes - one per LED to avoid contention
    std::map<LEDId, SemaphoreHandle_t> animation_mutexes;
    
    // Task suspension control
    SemaphoreHandle_t task_control_mutex;
    bool task_suspended;
    
    // Active animations - separated by LED to reduce contention
    std::map<LEDId, std::vector<AnimationState>> led_animations;
    
    // Update all animations in one frame
    void updateAnimations();
    
    // Update a specific breathing animation
    // Returns true if a color update is needed, along with the calculated color
    bool updateBreathingAnimation(AnimationState& anim, uint32_t current_time_ms, HSV& resultColor);
    
    // Helper to get current time in ms
    inline uint32_t getCurrentTimeMs() {
        return (uint32_t)(esp_timer_get_time() / 1000ULL);
    }
    
    // LEDC has been initialized
    bool initialized;
    
    // Log tag for this class
    static constexpr const char* TAG = "LED_MANAGER";
};