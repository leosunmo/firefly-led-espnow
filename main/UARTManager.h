#ifndef UART_MANAGER_H
#define UART_MANAGER_H

#include <cstdint>
#include <memory>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "Messages.h"
#include "UARTMessages.h"
#include "config.h"  // Include configuration header for UART pins and other settings

// UART Configuration
#define UART_NUM UART_NUM_0  // Changed from UART_NUM_0 to avoid conflict with debug logs
#define TX_PIN UART_TX_PIN  // Defined in config.h
#define RX_PIN UART_RX_PIN  // Defined in config.h
#define UART_BUFFER_SIZE 256
#define UART_QUEUE_SIZE 10

class UARTManager {
public:
    static UARTManager& getInstance();
    
    // Delete copy constructor and assignment operator
    UARTManager(const UARTManager&) = delete;
    UARTManager& operator=(const UARTManager&) = delete;
    
    esp_err_t init();
    
    // Send functions for different message types
    void sendBrightnessCommand(uint8_t brightness);
    void sendPatternCommand(PatternType pattern);
    void sendSpeedCommand(uint8_t speed);
    void sendHueCommand(uint16_t hue);  // New function for Hue values (0-360)
    void sendPunchCommand(uint8_t intensity); // New function for Punch effect (0-100)
    void sendDebugMessage(uint32_t value);
    void sendMessage(CommandType cmd, uint32_t value);
    
    // Process ESP-NOW message and forward to RP2040
    void processESPNOWMessage(const Message* message);
    
    // Start UART receive task
    void startReceiveTask();

private:
    UARTManager() = default;
    static const char* TAG;
    QueueHandle_t uartQueue = nullptr;
    
    // Task to handle UART reception
    static void uartReceiveTask(void* pvParameters);
    
    // Parse a received UART message
    void parseReceivedMessage(const uint8_t* data, size_t length);
};

#endif // UART_MANAGER_H
