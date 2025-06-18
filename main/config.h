#ifndef CONFIG_H
#define CONFIG_H

// Encoder configuration
#define ENCODER_DEBOUNCE_MS 40 // Debounce time in milliseconds for rotary encoder

// Encoder Hue A pins
#define HUE_A_ENCODER_A_PIN 18 // 3
#define HUE_A_ENCODER_B_PIN 19 // 2  

// Encoder Hue A RGB LED configuration
#define HUE_A_ENCODER_RGB_RED_PIN 23    // GPIO pin for red channel
#define HUE_A_ENCODER_RGB_GREEN_PIN 22  // GPIO pin for green channel
#define HUE_A_ENCODER_RGB_BLUE_PIN 21   // GPIO pin for blue channel

// Encoder Hue B Pins
#define HUE_B_ENCODER_A_PIN 3 // 18
#define HUE_B_ENCODER_B_PIN 2 // 19

// Encoder Hue B RGB LED configuration
#define HUE_B_ENCODER_RGB_RED_PIN 10    // GPIO pin for red channel
#define HUE_B_ENCODER_RGB_GREEN_PIN 8  // GPIO pin for green channel
#define HUE_B_ENCODER_RGB_BLUE_PIN 1   // GPIO pin for blue channel


typedef enum {
    DEVICE_ROLE_SENDER,
    DEVICE_ROLE_RECEIVER
} DeviceRole;

// Set the device role
#ifndef DEVICE_ROLE
#define DEVICE_ROLE DEVICE_ROLE_SENDER
#endif

#define USE_POINT_TO_POINT true

#define SENDER_LOG_LEVEL ESP_LOG_INFO
#define RECEIVER_LOG_LEVEL ESP_LOG_DEBUG
#define UART_LOG_LEVEL ESP_LOG_INFO
#define INPUTMANAGER_LOG_LEVEL ESP_LOG_DEBUG
#define LEDMANAGER_LOG_LEVEL ESP_LOG_DEBUG

// I2C Configuration
#define I2C_SDA_PIN 6 // Default SDA pin for I2C
#define I2C_SCL_PIN 7 // Default SCL pin for I2C

#define TCA6408A_LOG_LEVEL ESP_LOG_DEBUG // Log level for TCA6408A GPIO expander

// I2C Configuration for button input TCA6408A GPIO Expander
#define TCA6408A_A_I2C_ADDRESS 0x20 // Default I2C address for TCA6408A
#define TCA6408A_A_INT_PIN 11      // GPIO pin connected to TCA6408A INT pin (-1 to disable and use polling)

// I2C Configuration for Encoder buttons and Button LED TCA6408A GPIO Expander
#define TCA6408A_B_I2C_ADDRESS 0x21 // Second address for TCA6408A (address jumper soldered on the back)
#define TCA6408A_B_INT_PIN 20      // GPIO pin connected to TCA6408A INT pin (-1 to disable and use polling)

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
#define POT_CHANGE_THRESHOLD 40     // Minimum change to trigger an event (smaller for ADS1015 with max 1648)
#define POT_CENTER_THRESHOLD 200    // Threshold around center position (0-4095)

// ADS1015 ADC I2C Configuration
#define ADS1015_I2C_ADDRESS 0x48    // Default I2C address for ADS1015 (0x72-0x75 based on ADR pin)
#define ADS1015_I2C_FREQ_HZ 400000  // 400kHz I2C frequency
#define ADS1015_TIMEOUT_MS 100      // I2C timeout in milliseconds

#endif // CONFIG_H