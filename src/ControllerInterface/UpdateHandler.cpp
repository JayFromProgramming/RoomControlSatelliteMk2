//
// Created by Jay on 7/27/2025.
//

#include "UpdateHandler.h"

#include <esp32-hal.h>
#include <HardwareSerial.h>

#include "debug.h"


[[noreturn]] void UpdateHandler::updateTask(void* pvParameters) {
    auto* self = static_cast<UpdateHandler*>(pvParameters);
    DEBUG_PRINT("Starting Update Handler Task");
    while (true) {
        self->handleUpdate();
    }
}

void UpdateHandler::passData(const uint8_t* data, const size_t length) const {
    if (incomingDataQueue == nullptr) {
        return; // Queue not initialized
    }
    updateData_t incoming_buffer;
    uint32_t position = 0;
    // Re-add null characters as they were removed by the network interface
    for (size_t i = 0; i < length && position < sizeof(incoming_buffer.data); i++) {
        if (data[i] == NULL_TERM_ESCAPE) {
            if (data[i + 1] == NULL_TERM_REPLACE) {
                incoming_buffer.data[position++] = '\0'; // Replace with null character
                i++; // Skip the next byte
            } else if (data[i + 1] == NULL_TERM_ESCAPE_REPLACE) {
                incoming_buffer.data[position++] = NULL_TERM_ESCAPE; // Replace with escape character
                i++; // Skip the next byte
            } else if (data[i + 1] == '\0') {
                // Check if this is supposed to be the end of the data
                DEBUG_PRINT("Received null character in data at position %zu, stopping processing", i);
            } else {
                DEBUG_PRINT("Invalid escape sequence in data at position %zu", i);
            }
        } else {
            incoming_buffer.data[position++] = data[i];
        }
    }

    incoming_buffer.length = position;

    // Send the data to the queue
    xQueueSend(incomingDataQueue, &incoming_buffer, portMAX_DELAY);
}

void UpdateHandler::startUpdate() {
    DEBUG_PRINT("Starting OTA update with size: %u bytes", otaSize);
    esp_ota_handle_t otaHandle = 0;
    otaPartition = esp_ota_get_next_update_partition(nullptr);
    const auto result = esp_ota_begin(otaPartition, otaSize, &otaHandle);
    if (result != ESP_OK) {
        DEBUG_PRINT("Failed to begin OTA update: %s", esp_err_to_name(result));
        return; // Exit the function on error
    }
    this->otaRemaining = otaSize;
    this->otaHandle = otaHandle;
}

void UpdateHandler::handleUpdate() {
    // Wait for data to be available in the queue
    updateData_t data;
    if (xQueueReceive(incomingDataQueue, &data, portMAX_DELAY) == pdTRUE) {
        if (otaHandle == 0) {
            if (data.length != 4) {
                DEBUG_PRINT("Invalid OTA start message, expecting total firmware length got %zu bytes", data.length);
                char dataStr[64];
                // Print the data as a hex string for debugging
                for (size_t i = 0; i < data.length && i < sizeof(dataStr) - 1; i++) {
                    snprintf(dataStr + i * 2, sizeof(dataStr) - i * 2, "%02x", data.data[i]);
                }
                dataStr[sizeof(dataStr) - 1] = '\0'; // Ensure null termination
                DEBUG_PRINT("Received data: %s", dataStr);
                esp_restart();
            }
            otaSize = *(reinterpret_cast<const uint32_t*>(data.data));
            startUpdate();
            return; // Exit the function after starting the update
        }
        // Write the data to the OTA handle
        const auto writeResult = esp_ota_write(otaHandle, data.data, data.length);
        if (writeResult != ESP_OK) {
            DEBUG_PRINT("Failed to write OTA data: %s", esp_err_to_name(writeResult));
            finishUpdate();
            return; // Exit the function on error
        }
        otaRemaining -= data.length;
        // Check if the OTA update is complete
        if (otaRemaining == 0) {
            DEBUG_PRINT("OTA update complete, finishing update");
            finishUpdate();
            otaHandle = 0; // Reset the handle after finishing
        } else if (otaRemaining % 4096 == 0) {
            // Log progress every 4096 bytes
            DEBUG_PRINT("OTA update in progress: %u bytes remaining", otaRemaining);
        }
    }
}

void UpdateHandler::finishUpdate() {
    if (otaHandle == 0) {
        DEBUG_PRINT("No OTA handle to finish");
        return; // No update in progress
    }
    const auto result = esp_ota_end(otaHandle);
    if (result != ESP_OK) {
        DEBUG_PRINT("Failed to end OTA update: %s", esp_err_to_name(result));
        otaHandle = 0; // Reset the handle on error
        return;
    }
    DEBUG_PRINT("OTA update finished successfully, switching boot partitions");
    const auto switchResult = esp_ota_set_boot_partition(otaPartition);
    if (switchResult != ESP_OK) {
        DEBUG_PRINT("Failed to set boot partition: %s", esp_err_to_name(switchResult));
    } else {
        DEBUG_PRINT("Boot partition set successfully, rebooting device");
        esp_restart(); // Reboot the device to apply the update
    }

}