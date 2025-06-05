#ifndef CONFIG_H
#define CONFIG_H

// Encoder configuration
#define ENCODER_DEBOUNCE_MS 100 // Debounce time in milliseconds for encoder buttons

#define ENCODER_COLOR_A_PIN 20       // Color encoder signal A
#define ENCODER_COLOR_B_PIN 19       // Color encoder signal B
// #define ENCODER_COLOR_BTN_PIN 10    // Color encoder button (push)
// #define ENCODER_PATTERN_A_PIN 22    // Pattern encoder signal A
// #define ENCODER_PATTERN_B_PIN 21    // Pattern encoder signal B
// #define ENCODER_PATTERN_BTN_PIN 11  // Pattern encoder button (push)
#define ENCODER_POLL_INTERVAL_MS 10 // Read encoder every 10ms
#define ENCODER_DEBOUNCE_TIME_MS 20 // Button debounce time

typedef enum {
    DEVICE_ROLE_SENDER,
    DEVICE_ROLE_RECEIVER
} DeviceRole;

// Set the device role
#ifndef DEVICE_ROLE
#define DEVICE_ROLE DEVICE_ROLE_SENDER
#endif

#define USE_POINT_TO_POINT true

#define SENDER_LOG_LEVEL ESP_LOG_DEBUG
#define RECEIVER_LOG_LEVEL ESP_LOG_DEBUG
#define UART_LOG_LEVEL ESP_LOG_DEBUG

// I2C Configuration
#define I2C_SDA_PIN 6 // Default SDA pin for I2C
#define I2C_SCL_PIN 7 // Default SCL pin for I2C

// I2C Configuration for TCA6408A GPIO Expander
#define TCA6408A_I2C_ADDRESS 0x20 // Default I2C address for TCA6408A
#define TCA6408A_INT_PIN 11      // GPIO pin connected to TCA6408A INT pin (-1 to disable and use polling)

// Button Configuration
#define CONFIG_BUTTON_DEBOUNCE_TIME_MS 20      // Debounce time in ms

// UART Configuration for RP2040 communication
#define UART_TX_PIN 21 // TX pin to RP2040, default U0TXD pin
#define UART_RX_PIN 20 // RX pin from RP2040, default U0RXD pin
#define UART_BAUD_RATE 19200

// Potentiometer configuration
// #define ENABLE_ADC_CALIBRATION
#define POT_BRIGHTNESS_GPIO_NUM 3   // ADC1 Channel 3
#define POT_SPEED_GPIO_NUM 5        // ADC1 Channel 5
#define POT_POLL_INTERVAL_MS 50     // Read potentiometer every 50ms
#define POT_CHANGE_THRESHOLD 100    // Minimum change to trigger an event (0-4095)
#define POT_CENTER_THRESHOLD 200    // Threshold around center position (0-4095)

#endif // CONFIG_H