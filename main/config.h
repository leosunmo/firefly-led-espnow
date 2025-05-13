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

#define SENDER_LOG_LEVEL ESP_LOG_DEBUG
#define RECEIVER_LOG_LEVEL ESP_LOG_DEBUG

#endif // CONFIG_H