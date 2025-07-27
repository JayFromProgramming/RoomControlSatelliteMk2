//
// Created by Jay on 7/27/2025.
//

#ifndef UPDATEHANDLER_H
#define UPDATEHANDLER_H

#include <cstring>
#include <esp32-hal.h>
#include <esp_ota_ops.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "debug.h"

#define NULL_TERM_ESCAPE  0x08  // Escape character for null termination in uplink messages
#define NULL_TERM_REPLACE 0x01  // Replacement character for null termination in uplink messages
#define NULL_TERM_ESCAPE_REPLACE 0x02  // Replacement character for escaped null termination in uplink messages


class UpdateHandler {

    esp_ota_handle_t otaHandle = 0;
    uint32_t otaSize = OTA_SIZE_UNKNOWN;
    uint32_t otaRemaining = 0;

    const esp_partition_t *otaPartition = nullptr;

    struct updateData_t {
        size_t length;
        uint8_t data[1024];
    };

    QueueHandle_t incomingDataQueue = xQueueCreate(5, sizeof(updateData_t));

    static void updateTask(void* pvParameters);

    void handleUpdate();

    void startUpdate();

    void finishUpdate();

public:

    UpdateHandler() = default;

    void begin() {
        // Create the task to handle OTA updates
        xTaskCreatePinnedToCore(updateTask, "UpdateHandler", 8192, this, 1, nullptr, 1);
    }

    void passData(const uint8_t* data, const size_t length) const;

};



#endif //UPDATEHANDLER_H
