#include "LEDManager.h"
#include "esp_err.h"
#include <algorithm>

// Singleton instance implementation
LEDManager &LEDManager::getInstance()
{
    static LEDManager instance; // Created only once on first access
    return instance;
}

LEDManager::LEDManager() : 
    animation_manager_task(nullptr),
    animation_manager_running(false),
    task_control_mutex(nullptr),
    task_suspended(false),
    initialized(false)
{
    ESP_LOGI(TAG, "LEDManager singleton instance created");
}

LEDManager::~LEDManager()
{
    // Stop animation manager
    if (animation_manager_running) {
        animation_manager_running = false;
        
        // Wake up task if suspended to allow clean exit
        if (task_suspended && animation_manager_task != nullptr) {
            vTaskResume(animation_manager_task);
        }
        
        // Wait for task to exit
        if (animation_manager_task != nullptr) {
            vTaskDelay(pdMS_TO_TICKS(100));
            vTaskDelete(animation_manager_task);
            animation_manager_task = nullptr;
        }
        
        // Free the mutexes
        for (auto& mutex_pair : animation_mutexes) {
            if (mutex_pair.second != nullptr) {
                vSemaphoreDelete(mutex_pair.second);
                mutex_pair.second = nullptr;
            }
        }
        animation_mutexes.clear();
        
        if (task_control_mutex != nullptr) {
            vSemaphoreDelete(task_control_mutex);
            task_control_mutex = nullptr;
        }
    }
    
    // Clear all LED animations
    led_animations.clear();
    
    // LEDC doesn't need explicit deinitialization
    ESP_LOGW(TAG, "LEDManager singleton instance destroyed");
}

esp_err_t LEDManager::init()
{
    ESP_LOGI(TAG, "Initializing LEDManager");
    
    esp_log_level_set(TAG, LEDMANAGER_LOG_LEVEL); // Set log level for this module

    // Initialize LEDC timer
    esp_err_t err = initLEDCTimer();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LEDC timer: %s", esp_err_to_name(err));
        return err;
    }
    
    // Create mutex for task control
    task_control_mutex = xSemaphoreCreateMutex();
    if (task_control_mutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create task control mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Start animation manager task
    animation_manager_running = true;
    BaseType_t res = xTaskCreate(
        animationManagerTask,
        "LED_Anim_Mgr",
        4096,                // Stack size
        this,                // Task parameter (this pointer)
        tskIDLE_PRIORITY+1,  // Low priority
        &animation_manager_task
    );
    
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create animation manager task");
        vSemaphoreDelete(task_control_mutex);
        task_control_mutex = nullptr;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Animation manager task created successfully");

    initialized = true;
    ESP_LOGI(TAG, "LEDManager initialized successfully");
    return ESP_OK;
}

esp_err_t LEDManager::initLEDCTimer()
{
    // Set LEDC timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,   // Timer mode
        .duty_resolution = LEDC_TIMER_8_BIT,  // Resolution of PWM duty (8 bits: 0-255)
        .timer_num = LEDC_TIMER_0,            // Timer index
        .freq_hz = 5000,                      // Frequency of PWM signal (5kHz)
        .clk_cfg = LEDC_AUTO_CLK,             // Auto select clock source
        .deconfigure = false                  // Don't deconfigure timer
    };

    // Set the configuration
    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "LEDC timer initialized with %luHz PWM frequency", ledc_timer.freq_hz);
    return ESP_OK;
}

esp_err_t LEDManager::registerLED(LEDId ledId, const RGBLEDConfig& config)
{
    if (!initialized) {
        ESP_LOGE(TAG, "LEDManager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if LED already registered
    if (leds.find(ledId) != leds.end()) {
        ESP_LOGW(TAG, "LED %s already registered, overwriting", ledIdToString(ledId));
    }
    
    // Initialize LED channels
    esp_err_t err = initLED(ledId, config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED %s: %s", ledIdToString(ledId), esp_err_to_name(err));
        return err;
    }
    
    // Create LED info
    LEDInfo led_info;
    led_info.name = config.name;
    led_info.config = config;
    led_info.current_hsv = HSV(0, 0, 0); // Off by default
    led_info.current_rgb = RGB(0, 0, 0); // Off by default
    led_info.current_animation = AnimationType::NONE;
    led_info.animation_running = false;
    
    // Add to LEDs map
    leds[ledId] = led_info;
    
    // Create per-LED animation mutex
    animation_mutexes[ledId] = xSemaphoreCreateMutex();
    if (animation_mutexes[ledId] == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutex for LED %s", ledIdToString(ledId));
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize the LED's animation list
    led_animations[ledId] = std::vector<AnimationState>();
    
    ESP_LOGI(TAG, "LED %s registered as '%s' (R:%d, G:%d, B:%d)",
             ledIdToString(ledId), config.name.c_str(), 
             config.red_pin, config.green_pin, config.blue_pin);
    
    return ESP_OK;
}

esp_err_t LEDManager::initLED(LEDId ledId, const RGBLEDConfig& config)
{
    // Configure red channel
    ledc_channel_config_t red_channel_config = {
        .gpio_num = config.red_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = config.red_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,                       // Start with LED off
        .hpoint = 0,
        .flags = {.output_invert = config.common_anode ? 1u : 0u}, // Invert for common anode
    };
    
    // Configure green channel
    ledc_channel_config_t green_channel_config = {
        .gpio_num = config.green_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = config.green_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,                       // Start with LED off
        .hpoint = 0,
        .flags = {.output_invert = config.common_anode ? 1u : 0u} // Invert for common anode
    };
    
    // Configure blue channel
    ledc_channel_config_t blue_channel_config = {
        .gpio_num = config.blue_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = config.blue_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,                       // Start with LED off
        .hpoint = 0,
        .flags = {.output_invert = config.common_anode ? 1u : 0u} // Invert for common anode
    };
    
    // Configure all channels
    esp_err_t err = ledc_channel_config(&red_channel_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure red channel: %s", esp_err_to_name(err));
        return err;
    }
    
    err = ledc_channel_config(&green_channel_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure green channel: %s", esp_err_to_name(err));
        return err;
    }
    
    err = ledc_channel_config(&blue_channel_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure blue channel: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

esp_err_t LEDManager::setRGB(LEDId ledId, const RGB& color)
{
    if (!initialized) {
        ESP_LOGE(TAG, "LEDManager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if LED exists
    auto it = leds.find(ledId);
    if (it == leds.end()) {
        ESP_LOGE(TAG, "LED %s not registered", ledIdToString(ledId));
        return ESP_ERR_NOT_FOUND;
    }
    
    // Update LED info
    it->second.current_rgb = color;
    it->second.current_hsv = rgbToHsv(color);
    
    // Set PWM duty cycles
    const RGBLEDConfig& config = it->second.config;
    esp_err_t err;
    
    err = setDuty(config.red_channel, color.r);
    if (err != ESP_OK) return err;
    
    err = setDuty(config.green_channel, color.g);
    if (err != ESP_OK) return err;
    
    err = setDuty(config.blue_channel, color.b);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t LEDManager::setHSV(LEDId ledId, const HSV& color)
{
    // Convert HSV to RGB and set the LED
    RGB rgb = hsvToRgb(color);
    
    // Update tracking of HSV value directly
    auto it = leds.find(ledId);
    if (it != leds.end()) {
        // Store the new HSV color for this LED
        it->second.current_hsv = color;
        
        // If this LED has an active animation, update the animation's initial color
        if (it->second.animation_running) {
            // Take the LED-specific mutex
            auto mutex_it = animation_mutexes.find(ledId);
            if (mutex_it != animation_mutexes.end() && mutex_it->second != nullptr) {
                if (xSemaphoreTake(mutex_it->second, pdMS_TO_TICKS(10)) == pdTRUE) {
                    // Find the animation for this LED and update its color
                    auto& led_animation_list = led_animations[ledId];
                    for (auto& anim : led_animation_list) {
                        if (anim.running) {
                            // Update the color stored in animation state
                            anim.initial_color.h = color.h;
                            anim.initial_color.s = color.s;
                            
                            uint32_t current_time_ms = getCurrentTimeMs();
                            
                            // If the animation is already paused, just update the color without extending the pause
                            if (anim.animation_paused) {
                                ESP_LOGD(TAG, "Updated color during pause to hue %u", color.h);
                            } else {
                                // Animation not paused - mark that color was updated and pause the animation
                                anim.color_updated = true;
                                anim.color_update_time = current_time_ms;
                                
                                // Pause the animation briefly to show the selected color at full brightness
                                anim.animation_paused = true;
                                anim.pause_until_ms = current_time_ms + 300; // Pause for 300ms
                                
                                ESP_LOGI(TAG, "Color changed to hue %u, pausing animation for preview", color.h);
                            }
                            break;
                        }
                    }
                    xSemaphoreGive(mutex_it->second);
                }
            }
        }
        return setRGB(ledId, rgb);
    }
    
    ESP_LOGE(TAG, "LED %s not registered", ledIdToString(ledId));
    return ESP_ERR_NOT_FOUND;
}

esp_err_t LEDManager::setBrightness(LEDId ledId, uint8_t brightness)
{
    if (!initialized) {
        ESP_LOGE(TAG, "LEDManager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if LED exists
    auto it = leds.find(ledId);
    if (it == leds.end()) {
        ESP_LOGE(TAG, "LED %s not registered", ledIdToString(ledId));
        return ESP_ERR_NOT_FOUND;
    }
    
    // Clamp brightness to 0-100
    brightness = std::min(brightness, static_cast<uint8_t>(100));
    
    // Create new HSV with updated brightness
    HSV new_hsv = it->second.current_hsv;
    new_hsv.v = brightness;
    
    return setHSV(ledId, new_hsv);
}

esp_err_t LEDManager::setDuty(ledc_channel_t channel, uint32_t duty)
{
    // LEDC duty is 0-255 for 8-bit resolution
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty cycle: %s", esp_err_to_name(err));
        return err;
    }
    
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty cycle: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

esp_err_t LEDManager::startAnimation(LEDId ledId, const AnimationConfig& config)
{
    if (!initialized) {
        ESP_LOGE(TAG, "LEDManager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if LED exists
    auto it = leds.find(ledId);
    if (it == leds.end()) {
        ESP_LOGE(TAG, "LED %s not registered", ledIdToString(ledId));
        return ESP_ERR_NOT_FOUND;
    }
    
    // Stop any existing animation
    stopAnimation(ledId);
    
    // Update LED info - mark this LED as being animated
    it->second.current_animation = config.type;
    it->second.animation_running = true;
    
    if (config.type == AnimationType::NONE) {
        ESP_LOGI(TAG, "No animation to start");
        return ESP_OK;
    }
    
    // Take the mutex for this specific LED
    auto mutex_it = animation_mutexes.find(ledId);
    if (mutex_it == animation_mutexes.end() || mutex_it->second == nullptr) {
        ESP_LOGE(TAG, "Animation mutex for LED %s not found", ledIdToString(ledId));
        return ESP_FAIL;
    }
    
    if (xSemaphoreTake(mutex_it->second, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take animation mutex for LED %s", ledIdToString(ledId));
        return ESP_FAIL;
    }
    
    // Create new animation state
    AnimationState anim;
    anim.ledId = ledId;
    anim.type = config.type;
    anim.config = config;
    
    // Use default duration of 800ms if none specified (faster animation)
    if (anim.config.duration_ms == 0) {
        anim.config.duration_ms = 800; // Default to 800ms for a quick breathing effect
    }
    
    // Store the LED's current HSV color
    anim.initial_color = it->second.current_hsv;
    ESP_LOGI(TAG, "Saving initial color for animation: H:%u S:%u V:%u",
             anim.initial_color.h, anim.initial_color.s, anim.initial_color.v);
    
    // Initialize animation timing
    anim.start_time_ms = getCurrentTimeMs();
    anim.last_update_ms = anim.start_time_ms;
    anim.repeat_count = 0;
    anim.current_brightness = 0;
    anim.fading_in = true;
    anim.running = true;
    
    // Initialize color change management
    anim.color_updated = false;
    anim.color_update_time = anim.start_time_ms;
    anim.animation_paused = false;
    anim.pause_until_ms = 0;
    anim.needsUpdate = false;
    
    // Add to this specific LED's animation list
    led_animations[ledId].push_back(anim);
    
    // Check if this is the first animation for any LED
    bool was_empty = true;
    for (const auto& animations_pair : led_animations) {
        if (!animations_pair.second.empty()) {
            was_empty = false;
            break;
        }
    }
    
    // Release the LED-specific mutex
    xSemaphoreGive(mutex_it->second);
    
    // Resume task if this is the first animation
    if (was_empty && xSemaphoreTake(task_control_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (task_suspended && animation_manager_task != nullptr) {
            ESP_LOGD(TAG, "Resuming animation manager task for new animation");
            task_suspended = false;
            vTaskResume(animation_manager_task);
        }
        xSemaphoreGive(task_control_mutex);
    }
    
    ESP_LOGI(TAG, "Started %s animation on LED %s (duration: %lu ms, repeats: %s, current hue: %u)",
             animationTypeToString(config.type), 
             ledIdToString(ledId),
             anim.config.duration_ms,
             config.repeat_count == 0 ? "infinite" : std::to_string(config.repeat_count).c_str(),
             it->second.current_hsv.h);
    
    return ESP_OK;
}

esp_err_t LEDManager::stopAnimation(LEDId ledId)
{
    // Check if LED exists
    auto it = leds.find(ledId);
    if (it == leds.end()) {
        ESP_LOGE(TAG, "LED %s not registered", ledIdToString(ledId));
        return ESP_ERR_NOT_FOUND;
    }
    
    // Check if animation is running
    if (!it->second.animation_running) {
        ESP_LOGD(TAG, "No animation running on LED %s", ledIdToString(ledId));
        return ESP_OK; // Nothing to stop
    }
    
    // Take the mutex for this specific LED
    auto mutex_it = animation_mutexes.find(ledId);
    if (mutex_it == animation_mutexes.end() || mutex_it->second == nullptr) {
        ESP_LOGE(TAG, "Animation mutex for LED %s not found", ledIdToString(ledId));
        return ESP_FAIL;
    }
    
    if (xSemaphoreTake(mutex_it->second, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take animation mutex for LED %s", ledIdToString(ledId));
        return ESP_FAIL;
    }
    
    // Find and mark animation as not running - even if paused
    auto& led_animation_list = led_animations[ledId];
    for (auto& anim : led_animation_list) {
        if (anim.ledId == ledId && anim.running) {
            anim.running = false;
            anim.animation_paused = false; // Clear pause state to ensure proper removal
            
            // Make sure the LED is left at full brightness with current color when stopped
            auto led_it = leds.find(ledId);
            if (led_it != leds.end()) {
                HSV finalColor = led_it->second.current_hsv;
                finalColor.v = 100; // Full brightness
                
                // Force set the final color directly to avoid animation logic
                RGB rgb = hsvToRgb(finalColor);
                setDuty(led_it->second.config.red_channel, rgb.r);
                setDuty(led_it->second.config.green_channel, rgb.g);
                setDuty(led_it->second.config.blue_channel, rgb.b);
                
                // Update the tracking values
                led_it->second.current_hsv = finalColor;
                led_it->second.current_rgb = rgb;
            }
            
            ESP_LOGI(TAG, "Marked animation for LED %s as stopped", ledIdToString(ledId));
        }
    }
    
    // Release the LED-specific mutex
    xSemaphoreGive(mutex_it->second);
    
    // Reset LED animation state
    it->second.animation_running = false;
    it->second.current_animation = AnimationType::NONE;
    
    ESP_LOGI(TAG, "Stopped animation on LED %s", ledIdToString(ledId));
    
    return ESP_OK;
}

void LEDManager::animationManagerTask(void* arg)
{
    LEDManager* manager = static_cast<LEDManager*>(arg);
    const uint32_t frame_time_ms = 20; // 50fps update rate - good balance between smoothness and performance
    
    ESP_LOGI(manager->TAG, "Animation manager task started");
    
    // Use absolute timing to maintain consistent frame rate
    TickType_t last_wake_time = xTaskGetTickCount();
    
    // Stats for timing diagnostics
    uint32_t max_processing_time_us = 0;
    uint32_t frame_count = 0;
    uint32_t last_stats_time = esp_timer_get_time() / 1000ULL;
    
    while (manager->animation_manager_running) {
        // Measure processing time
        uint64_t start_time = esp_timer_get_time();
        
        // Update all animations
        manager->updateAnimations();
        
        // Record timing statistics
        uint32_t processing_time_us = (esp_timer_get_time() - start_time);
        max_processing_time_us = std::max(max_processing_time_us, processing_time_us);
        frame_count++;
        
        // Log stats once per second
        uint32_t current_time = esp_timer_get_time() / 1000ULL;
        if (current_time - last_stats_time >= 5000) { // Log every 5 seconds
            ESP_LOGD(manager->TAG, "Animation statistics: %lu frames, max processing time: %lu μs", 
                     frame_count, max_processing_time_us);
            // Reset stats
            max_processing_time_us = 0;
            frame_count = 0;
            last_stats_time = current_time;
        }
        
        // Sleep until next frame using absolute time for consistent frame rate
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(frame_time_ms));
    }
    
    ESP_LOGI(manager->TAG, "Animation manager task exiting");
    vTaskDelete(NULL);
}

// Update all active animations in one frame
void LEDManager::updateAnimations()
{
    // Get current time for this frame
    uint32_t current_time_ms = getCurrentTimeMs();
    
    // Check if any LED has active animations
    bool has_animations = false;
    for (const auto& animations_pair : led_animations) {
        if (!animations_pair.second.empty()) {
            has_animations = true;
            break;
        }
    }
    
    // If no animations are running, suspend the task
    if (!has_animations) {
        // Suspend the task if there are no animations
        if (xSemaphoreTake(task_control_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (!task_suspended) {
                ESP_LOGD(TAG, "No active animations, suspending animation manager task");
                task_suspended = true;
                xSemaphoreGive(task_control_mutex);
                vTaskSuspend(NULL); // Suspend itself
                
                // When resumed, immediately return to avoid processing this frame
                ESP_LOGD(TAG, "Animation manager task resumed");
                return;
            }
            xSemaphoreGive(task_control_mutex);
        }
        return;
    }
    
    // Process animations for each LED independently
    for (auto& led_anim_pair : led_animations) {
        LEDId ledId = led_anim_pair.first;
        auto& animations = led_anim_pair.second;
        
        // Skip LEDs with no animations
        if (animations.empty()) {
            continue;
        }
        
        // Take the mutex for this specific LED
        auto mutex_it = animation_mutexes.find(ledId);
        if (mutex_it == animation_mutexes.end() || mutex_it->second == nullptr) {
            ESP_LOGW(TAG, "Animation mutex for LED %s not found, skipping", ledIdToString(ledId));
            continue;
        }
        
        // Try to take the mutex with a short timeout
        if (xSemaphoreTake(mutex_it->second, pdMS_TO_TICKS(5)) != pdTRUE) {
            // If we can't get the mutex quickly, skip this LED for this frame
            ESP_LOGD(TAG, "Mutex for LED %s busy, skipping animation frame", ledIdToString(ledId));
            continue;
        }
        
        // Iterate through all animations for this LED
        for (auto it = animations.begin(); it != animations.end(); ) {
            AnimationState& anim = *it;
            
            if (!anim.running) {
                // Animation was stopped, remove it
                ESP_LOGI(TAG, "Removing stopped animation for LED %s (was %s)",
                        ledIdToString(anim.ledId),
                        anim.animation_paused ? "paused" : "running");
                
                it = animations.erase(it);
                continue;
            }
            
            // Update based on animation type with color calculation (but not application yet)
            HSV calculatedColor;
            bool needColorUpdate = false;
            
            if (anim.type == AnimationType::BREATHING) {
                needColorUpdate = updateBreathingAnimation(anim, current_time_ms, calculatedColor);
            }
            
            // Store the calculation result to apply after releasing the mutex
            if (needColorUpdate) {
                it->calculatedColor = calculatedColor;
                it->needsUpdate = true;
            }
            
            // Check if animation has completed all repeats (but only if not infinite)
            if (anim.config.repeat_count > 0 && anim.repeat_count >= anim.config.repeat_count) {
                // Animation complete, remove it
                ESP_LOGI(TAG, "Animation complete for LED %s after %lu cycles", 
                         ledIdToString(anim.ledId), anim.repeat_count);
                
                // Update the LEDInfo
                auto led_it = leds.find(anim.ledId);
                if (led_it != leds.end()) {
                    led_it->second.current_animation = AnimationType::NONE;
                    led_it->second.animation_running = false;
                    
                    // Use the animation's stored initial color to ensure color persistence
                    HSV finalColor = anim.initial_color;
                    finalColor.v = 100; // Full brightness
                    
                    ESP_LOGD(TAG, "Animation ending, restoring to initial color H:%u S:%u at full brightness",
                             finalColor.h, finalColor.s);
                    
                    // Set the LED to the final color at full brightness
                    setHSV(anim.ledId, finalColor);
                    
                    ESP_LOGD(TAG, "Restored LED %s to full brightness with hue %u",
                             ledIdToString(anim.ledId), finalColor.h);
                }
                
                it = animations.erase(it);
            } else {
                ++it;
            }
        }
        
        // Keep track of which animations need color updates
        std::vector<AnimationState*> animations_needing_update;
        for (auto& anim : animations) {
            if (anim.needsUpdate) {
                animations_needing_update.push_back(&anim);
            }
        }
        
        // Release the LED-specific mutex
        xSemaphoreGive(mutex_it->second);
        
        // Apply any color updates AFTER releasing the mutex
        // This prevents deadlocks when setHSV tries to take the same mutex
        for (auto anim_ptr : animations_needing_update) {
            setRGB(anim_ptr->ledId, hsvToRgb(anim_ptr->calculatedColor));
            anim_ptr->needsUpdate = false;
        }
    }
}

// Update a breathing animation state
// Returns true if a color update is needed, along with the calculated color
bool LEDManager::updateBreathingAnimation(AnimationState& anim, uint32_t current_time_ms, HSV& resultColor)
{
    // Get elapsed time since last update
    uint32_t elapsed_ms = current_time_ms - anim.last_update_ms;
    if (elapsed_ms < 10) {
        // Not time to update yet (need at least 10ms between updates for smooth animation)
        return false;
    }
    
    // Update timestamp
    anim.last_update_ms = current_time_ms;
    
    // Check if animation is paused for color preview
    if (anim.animation_paused) {
        // If we're past the pause duration, resume the animation
        if (current_time_ms >= anim.pause_until_ms) {
            // Resume the animation and reset relevant flags
            anim.animation_paused = false;
            anim.color_updated = false; // Clear this flag to avoid re-entering pause
            
            // Reset the animation cycle to start from the beginning when resuming
            // This creates a smooth transition from the full brightness preview
            anim.start_time_ms = current_time_ms;
            
            ESP_LOGI(TAG, "Resuming animation after color preview pause with hue %u", anim.initial_color.h);
        } else {
            // Still in preview mode - keep the LED at full brightness with the new color
            resultColor = anim.initial_color;
            resultColor.v = 100; // Full brightness for preview
            
            // Log periodic updates while paused (only every 100ms to avoid spam)
            if (current_time_ms % 100 == 0) {
                ESP_LOGD(TAG, "Paused at full brightness, time remaining: %lu ms", 
                         anim.pause_until_ms - current_time_ms);
            }
            return true; // Need to update with preview color
        }
    }
    
    // Check if a color change was recently made but we're not showing preview yet
    if (anim.color_updated && !anim.animation_paused) {
        // Color was updated but we haven't shown the preview yet
        // Pause the animation for preview
        anim.animation_paused = true;
        anim.pause_until_ms = current_time_ms + 300; // Show preview for 300ms
        
        // Show preview at full brightness
        resultColor = anim.initial_color;
        resultColor.v = 100; // Full brightness for preview
        
        ESP_LOGI(TAG, "Starting color preview pause for 300ms with hue %u", resultColor.h);
        return true; // Need to update with preview color
    }
    
    // Normal animation processing (not paused and not just updated)
    
    // Use a faster cycle duration if one wasn't specified or use the configured one
    // Default to 800ms for a nice, quick breathing effect
    uint32_t cycle_ms = (anim.config.duration_ms == 0) ? 800 : anim.config.duration_ms;
    uint32_t elapsed_in_cycle = (current_time_ms - anim.start_time_ms) % cycle_ms;
    
    // Calculate brightness based on position in cycle (0-100%)
    uint8_t brightness;
    bool completed_cycle = false;
    
    // Enhanced sinusoidal breathing pattern - smoother and more pronounced
    float progress = static_cast<float>(elapsed_in_cycle) / cycle_ms;
    
    // Use sine wave for a smoother effect: sin²(πt)
    // This creates a smoother curve that starts at 0, goes to 1, then back to 0
    float sin_value = sinf(progress * M_PI);
    
    // Check if we just resumed from pause - if so, start with a gentle fade-in
    uint32_t time_since_resume = current_time_ms - anim.start_time_ms;
    if (time_since_resume < 100) {
        // Just resumed animation, ease in from full brightness to prevent abrupt changes
        float fade_ratio = 1.0f - (time_since_resume / 100.0f);
        // Start from a higher brightness and gradually transition to the normal animation curve
        brightness = static_cast<uint8_t>(100.0f * (fade_ratio + (1.0f - fade_ratio) * sin_value * sin_value));
    } else {
        // Normal animation curve
        brightness = static_cast<uint8_t>(100.0f * sin_value * sin_value);
    }
    
    // If we're close to the start of a cycle and we've been running for a while,
    // we've completed a cycle
    if (elapsed_in_cycle < 20 && current_time_ms - anim.start_time_ms > cycle_ms) {
        completed_cycle = true;
    }
    
    // Use the initial HSV color, only updating the brightness component
    resultColor = anim.initial_color;
    resultColor.v = brightness;
    
    // Update cycle counter if needed
    if (completed_cycle) {
        anim.repeat_count++;
        
        // Explicitly verify and refresh color at cycle boundaries to ensure persistence
        auto led_it = leds.find(anim.ledId);
        if (led_it != leds.end()) {
            // Check if the LED's stored color matches the animation's stored color
            if (led_it->second.current_hsv.h != anim.initial_color.h || 
                led_it->second.current_hsv.s != anim.initial_color.s) {
                
                // Mismatch found - update the LED's stored color to match the animation
                // Only update tracking, not actual color since we're in the animation
                led_it->second.current_hsv.h = anim.initial_color.h;
                led_it->second.current_hsv.s = anim.initial_color.s;
            }
        }
        
        // // Log more details when cycle completes to verify color persistence
        // ESP_LOGD(TAG, "LED %s breathing cycle %lu completed - using color H:%u S:%u V:%u", 
        //          ledIdToString(anim.ledId), anim.repeat_count, resultColor.h, resultColor.s, resultColor.v);
        
        // // In continuous mode, we don't need to log every single cycle
        // if (anim.repeat_count % 10 == 0 || anim.config.repeat_count > 0) {
        //     ESP_LOGD(TAG, "Breathing cycle %lu/%lu completed for LED %s", 
        //              anim.repeat_count, 
        //              anim.config.repeat_count > 0 ? anim.config.repeat_count : 0,
        //              ledIdToString(anim.ledId));
        // }
    }
    
    return true; // Need to update with calculated color
}

LEDManager::RGB LEDManager::hsvToRgb(const HSV& hsv)
{
    RGB rgb;
    
    // Handle special case where saturation or value is 0
    if (hsv.s == 0) {
        // Achromatic (gray)
        uint8_t gray = static_cast<uint8_t>(hsv.v * MAX_DUTY / 100);
        return RGB(gray, gray, gray);
    }
    
    if (hsv.v == 0) {
        // Black
        return RGB(0, 0, 0);
    }
    
    float h = hsv.h / 60.0f;  // sector 0 to 5
    uint8_t i = static_cast<uint8_t>(h);
    float f = h - i;          // fractional part of h
    
    // Brightness values
    float v = static_cast<float>(hsv.v) / 100.0f;
    float p = v * (1 - static_cast<float>(hsv.s) / 100.0f);
    float q = v * (1 - f * static_cast<float>(hsv.s) / 100.0f);
    float t = v * (1 - (1 - f) * static_cast<float>(hsv.s) / 100.0f);
    
    // Scale to PWM range (0-255)
    switch (i % 6) {
        case 0: 
            rgb.r = static_cast<uint8_t>(v * MAX_DUTY);
            rgb.g = static_cast<uint8_t>(t * MAX_DUTY);
            rgb.b = static_cast<uint8_t>(p * MAX_DUTY);
            break;
        case 1:
            rgb.r = static_cast<uint8_t>(q * MAX_DUTY);
            rgb.g = static_cast<uint8_t>(v * MAX_DUTY);
            rgb.b = static_cast<uint8_t>(p * MAX_DUTY);
            break;
        case 2:
            rgb.r = static_cast<uint8_t>(p * MAX_DUTY);
            rgb.g = static_cast<uint8_t>(v * MAX_DUTY);
            rgb.b = static_cast<uint8_t>(t * MAX_DUTY);
            break;
        case 3:
            rgb.r = static_cast<uint8_t>(p * MAX_DUTY);
            rgb.g = static_cast<uint8_t>(q * MAX_DUTY);
            rgb.b = static_cast<uint8_t>(v * MAX_DUTY);
            break;
        case 4:
            rgb.r = static_cast<uint8_t>(t * MAX_DUTY);
            rgb.g = static_cast<uint8_t>(p * MAX_DUTY);
            rgb.b = static_cast<uint8_t>(v * MAX_DUTY);
            break;
        default:
            rgb.r = static_cast<uint8_t>(v * MAX_DUTY);
            rgb.g = static_cast<uint8_t>(p * MAX_DUTY);
            rgb.b = static_cast<uint8_t>(q * MAX_DUTY);
            break;
    }
    
    return rgb;
}

LEDManager::HSV LEDManager::rgbToHsv(const RGB& rgb)
{
    HSV hsv;
    
    // Convert RGB to float range 0.0-1.0
    float r = static_cast<float>(rgb.r) / MAX_DUTY;
    float g = static_cast<float>(rgb.g) / MAX_DUTY;
    float b = static_cast<float>(rgb.b) / MAX_DUTY;
    
    float max_val = std::max(std::max(r, g), b);
    float min_val = std::min(std::min(r, g), b);
    float delta = max_val - min_val;
    
    // Calculate value (brightness)
    hsv.v = static_cast<uint8_t>(max_val * 100.0f);
    
    // If max value is 0, then color is black (all zeros)
    if (max_val == 0.0f) {
        hsv.h = 0;
        hsv.s = 0;
        return hsv;
    }
    
    // Calculate saturation
    hsv.s = static_cast<uint8_t>(delta / max_val * 100.0f);
    
    // If saturation is 0, then color is gray
    if (delta == 0.0f) {
        hsv.h = 0;
        return hsv;
    }
    
    // Calculate hue
    float hue;
    if (max_val == r) {
        hue = (g - b) / delta + (g < b ? 6.0f : 0.0f);
    } else if (max_val == g) {
        hue = (b - r) / delta + 2.0f;
    } else {
        hue = (r - g) / delta + 4.0f;
    }
    
    hsv.h = static_cast<uint16_t>(hue * 60.0f);
    return hsv;
}

const char* LEDManager::ledIdToString(LEDId id)
{
    switch (id) {
        case LEDId::ENCODER_A_RGB:
            return "ENCODER_A_RGB";
        case LEDId::ENCODER_B_RGB:
            return "ENCODER_B_RGB";
        default:
            return "UNKNOWN_LED";
    }
}

bool LEDManager::isAnimationRunning(LEDId ledId) const
{
    // Check if LED exists
    auto it = leds.find(ledId);
    if (it == leds.end()) {
        ESP_LOGW(TAG, "LED %s not registered, cannot check animation status", ledIdToString(ledId));
        return false;
    }
    
    // Return the animation status
    return it->second.animation_running;
}

LEDManager::HSV LEDManager::getCurrentColorHSV(LEDId ledId) const
{
    // Default color (white) if LED not found
    HSV defaultColor(0, 0, 100);
    
    // Check if LED exists
    auto it = leds.find(ledId);
    if (it == leds.end()) {
        ESP_LOGW(TAG, "LED %s not registered, returning default color", ledIdToString(ledId));
        return defaultColor;
    }
    
    // Return the current HSV color
    return it->second.current_hsv;
}

const char* LEDManager::animationTypeToString(AnimationType type)
{
    switch (type) {
        case AnimationType::NONE:
            return "NONE";
        case AnimationType::BREATHING:
            return "BREATHING";
        default:
            return "UNKNOWN_ANIMATION";
    }
}