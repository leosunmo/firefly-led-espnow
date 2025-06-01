#ifndef CONFIG_H
#define CONFIG_H

// Potentiometer configuration
// #define ENABLE_ADC_CALIBRATION
#define POT_BRIGHTNESS_GPIO_NUM 4   // ADC1 Channel 4
#define POT_SPEED_GPIO_NUM 5        // ADC1 Channel 5
#define POT_POLL_INTERVAL_MS 50     // Read potentiometer every 50ms
#define POT_CHANGE_THRESHOLD 100    // Minimum change to trigger an event (0-4095)
#define POT_CENTER_THRESHOLD 200    // Threshold around center position (0-4095)

// Encoder configuration
#define ENCODER_COLOR_A_PIN 8       // Color encoder signal A
#define ENCODER_COLOR_B_PIN 9       // Color encoder signal B
#define ENCODER_COLOR_BTN_PIN 10    // Color encoder button (push)
#define ENCODER_PATTERN_A_PIN 11    // Pattern encoder signal A
#define ENCODER_PATTERN_B_PIN 12    // Pattern encoder signal B
#define ENCODER_PATTERN_BTN_PIN 13  // Pattern encoder button (push)
#define ENCODER_POLL_INTERVAL_MS 10 // Read encoder every 10ms
#define ENCODER_DEBOUNCE_TIME_MS 20 // Button debounce time
#define ENCODER_LONG_PRESS_MS 1000  // Long press time for encoder buttons

typedef enum {
    DEVICE_ROLE_SENDER,
    DEVICE_ROLE_RECEIVER
} DeviceRole;

// Set the device role
#ifndef DEVICE_ROLE
#define DEVICE_ROLE DEVICE_ROLE_RECEIVER
#endif

#define USE_POINT_TO_POINT true

#define SENDER_LOG_LEVEL ESP_LOG_DEBUG
#define RECEIVER_LOG_LEVEL ESP_LOG_DEBUG
#define UART_LOG_LEVEL ESP_LOG_DEBUG

#define BUTTONBLUE_GPIO_NUM 16
#define BUTTONRED_GPIO_NUM 17

// UART Configuration for RP2040 communication
#define UART_TX_PIN 21 // TX pin to RP2040, default U0TXD pin
#define UART_RX_PIN 20 // RX pin from RP2040, default U0RXD pin
#define UART_BAUD_RATE 19200

// Potentiometer configuration
// #define ENABLE_ADC_CALIBRATION
#define POT_BRIGHTNESS_GPIO_NUM 4   // ADC1 Channel 4
#define POT_SPEED_GPIO_NUM 5        // ADC1 Channel 5
#define POT_POLL_INTERVAL_MS 50     // Read potentiometer every 50ms
#define POT_CHANGE_THRESHOLD 100    // Minimum change to trigger an event (0-4095)
#define POT_CENTER_THRESHOLD 200    // Threshold around center position (0-4095)

#endif // CONFIG_H