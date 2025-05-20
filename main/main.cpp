/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
   This example shows how to use ESPNOW.
   Prepare two device, one for sending ESPNOW data and another for receiving
   ESPNOW data.
*/
#include <memory>
#include <vector>
#include <functional>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_log_level.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
}

#include "Manager.h"
#include "Sender.h"
#include "Receiver.h"
#include "config.h"

extern "C" void app_main() {
    Manager manager;
    esp_err_t err = ESP_OK;

    err = manager.init();
    if (err != ESP_OK) {
        ESP_LOGE("app_main", "Failed to initialize Manager: %s", esp_err_to_name(err));
        return;
    }

    switch (DEVICE_ROLE) {
        case DEVICE_ROLE_SENDER:
            // Initialize the example ESPNOW sender
            Sender::init();
            break;
        case DEVICE_ROLE_RECEIVER:
            // Initialize the example ESPNOW receiver
            Receiver::init();
            break;
        default:
            ESP_LOGE("app_main", "Invalid device role defined.");
            return;
    }
}
