#ifndef CONFIG_H
#define CONFIG_H

// Define the device roles as an enum
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