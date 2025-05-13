#ifndef ESPNOW_MANAGER_H
#define ESPNOW_MANAGER_H

#include <vector>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_now.h"
#include "ESPNOWMessages.h"

/* ESPNOW can work in both station and softap mode. It is configured in menuconfig. */
#if CONFIG_ESPNOW_WIFI_MODE_STATION
#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_STA
#else
#define ESPNOW_WIFI_MODE WIFI_MODE_AP
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_AP
#endif

#define ESPNOW_QUEUE_SIZE 6
#define ESPNOW_MAXDELAY 512

class ESPNOWManager {
public:
    ESPNOWManager();
    ~ESPNOWManager();

    esp_err_t init();

private:
    esp_err_t initNVS();
    esp_err_t initWiFi();
    esp_err_t initESPNOW();
    void deinitESPNOW();
};

#endif // ESPNOW_MANAGER_H