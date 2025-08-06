//
// Created by Jay on 7/27/2025.
//

#include "UpdateHandler.h"

#include <esp32-hal.h>
#include <HardwareSerial.h>

#include "debug.h"

__NOINIT_ATTR uint32_t rollbackDetectionFlagPreserver;

[[noreturn]] void UpdateHandler::updateTask(void* pvParameters) {
    auto* self = static_cast<UpdateHandler*>(pvParameters);
    DEBUG_PRINT("Starting Update Handler Task");
    while (true) {
        self->handleUpdate();
    }
}

const char* ota_state_to_string(const esp_ota_img_states_t state) {
    switch (state) {
        case ESP_OTA_IMG_NEW: return "ESP_OTA_IMG_NEW";
        case ESP_OTA_IMG_UNDEFINED: return "ESP_OTA_IMG_UNDEFINED";
        case ESP_OTA_IMG_VALID: return "ESP_OTA_IMG_VALID";
        case ESP_OTA_IMG_INVALID: return "ESP_OTA_IMG_INVALID";
        case ESP_OTA_IMG_PENDING_VERIFY: return "ESP_OTA_IMG_PENDING_VERIFY";
        default: return "Unknown";
    }
}

void UpdateHandler::passData(const uint8_t* data, const size_t length) const {
    if (incomingDataQueue == nullptr) {
        return; // Queue not initialized
    }
    updateData_t incoming_buffer{};
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
#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#pragma message("Warning: Bootloader rollback is not enabled, OTA will not be enabled")
#endif
    DEBUG_PRINT("Starting OTA update with size: %u bytes", otaSize);
    esp_ota_handle_t otaHandle = 0;
    otaPartition = esp_ota_get_next_update_partition(nullptr);
    const auto result = esp_ota_begin(otaPartition, otaSize, &otaHandle);
    if (otaPartition == nullptr) {
        DEBUG_PRINT("No OTA partition found, cannot start update");
        return; // Exit the function if no partition is available
    }
    if (result != ESP_OK) {
        DEBUG_PRINT("Failed to begin OTA update: %s", esp_err_to_name(result));
        return; // Exit the function on error
    }
    DEBUG_PRINT("OTA update started successfully on partition: %s", otaPartition->label);
    this->otaRemaining = otaSize;
    this->otaHandle = otaHandle;
    this->otaDelay = 10000; // Set the delay to 10 seconds for the first write
}

void UpdateHandler::handleUpdate() {
    // Wait for data to be available in the queue
    updateData_t data{};
    const auto status = xQueueReceive(incomingDataQueue, &data, otaDelay);
    if (status == pdTRUE) {
        if (otaHandle == 0) {
            if (data.length != 4) {
                // DEBUG_PRINT("0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X",
                //             data.data[0], data.data[1], data.data[2], data.data[3], data.data[4], data.data[5]);
                // DEBUG_PRINT("Invalid OTA start message, expecting total firmware length got %zu bytes", data.length);
                // vTaskDelay(500);
                return; // Exit the function if the data is not the expected length
            }
            otaSize = *(reinterpret_cast<const uint32_t*>(data.data));
            startUpdate();
            return; // Exit the function after starting the update
        }
        // Write the data to the OTA handle
        const auto writeResult = esp_ota_write(otaHandle, data.data, data.length);
        if (writeResult != ESP_OK) {
            DEBUG_PRINT("Failed to write OTA data: %s", esp_err_to_name(writeResult));
            abortUpdate(); // Abort the update on error
            return; // Exit the function on error
        }
        otaRemaining -= data.length;
        // Check if the OTA update is complete
        if (otaRemaining == 0) {
            DEBUG_PRINT("OTA update complete, finishing update");
            finishUpdate();
            otaHandle = 0; // Reset the handle after finishing
        }
    } else {
        // No data received in 1.5 seconds, the update likely timed out or failed so abort the update
        DEBUG_PRINT("No data received in %d ms, aborting OTA update [%d bytes remaining]",
            otaDelay / portTICK_PERIOD_MS, otaRemaining);
        abortUpdate();
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
    // Mark the next partition as ESP_OTA_IMG_NEW
    if (switchResult != ESP_OK) {
        DEBUG_PRINT("Failed to set boot partition: %s", esp_err_to_name(switchResult));
        return; // Exit the function on error
    }
    // Mark the otaPartition as ESP_OTA_IMG_PENDING_VERIF
    esp_ota_img_states_t otaState;
    esp_ota_get_state_partition(otaPartition, &otaState);
    if (otaState != ESP_OTA_IMG_NEW) {
        DEBUG_PRINT("Warning: OTA partition was not marked as ESP_OTA_IMG_NEW by esp_ota_set_boot_partition, current state: %d", otaState);
    }
    rollbackDetectionFlagPreserver = ROLLBACK_ARMED; // Set the rollback detection flag to armed
    esp_restart();
}

void UpdateHandler::abortUpdate() {
    if (otaHandle == 0) {
        DEBUG_PRINT("No OTA handle to abort");
        return; // No update in progress
    }
    const auto result = esp_ota_abort(otaHandle);
    if (result != ESP_OK) {
        DEBUG_PRINT("Failed to abort OTA update: %s", esp_err_to_name(result));
    } else {
        DEBUG_PRINT("OTA update aborted successfully");
    }
    otaHandle = 0; // Reset the handle after aborting
    otaSize = OTA_SIZE_UNKNOWN; // Reset the OTA size
    otaRemaining = 0; // Reset the remaining size
    otaPartition = nullptr; // Reset the OTA partition
    otaDelay = portMAX_DELAY; // Reset the delay to default
    // Clear the incoming data queue
    updateData_t data{};
    while (uxQueueMessagesWaiting(incomingDataQueue) > 0) {
        xQueueReceive(incomingDataQueue, &data, 0); // Drain the queue
    }
}

void UpdateHandler::check_partition_states() {
    // Print the state of all partitions
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    DEBUG_PRINT("Getting partition states for all partitions:");
    while (it != nullptr) {
        const esp_partition_t *partition = esp_partition_get(it);
        esp_app_desc_t app_desc;
        // const auto desc_result = esp_ota_get_partition_description(partition, &app_desc);
        auto ota_state = ESP_OTA_IMG_UNDEFINED;
        esp_ota_get_state_partition(partition, &ota_state);
        DEBUG_PRINT("Partition Label: %s, Address: 0x%X, State: %s",
                    partition->label,
                    partition->address,
                    // app_desc.version,
                    // app_desc.project_name,
                    ota_state_to_string(ota_state));
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
}

const esp_partition_t* UpdateHandler::get_factory_partition() {
    const esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    if (it == nullptr) {
        DEBUG_PRINT("No factory partition found");
        return {};
    }
    const esp_partition_t *partition = esp_partition_get(it);
    esp_partition_iterator_release(it);
    return partition;
}

void UpdateHandler::check_for_rollback() {
    DEBUG_PRINT("Checking for rollback detection flag preserver: 0x%08X", rollbackDetectionFlagPreserver);
    check_partition_states();
    if (rollbackDetectionFlagPreserver == ROLLBACK_ARMED) {
        // Set the boot partition to the factory partition
        const auto factoryPartition = get_factory_partition();
        if (factoryPartition == nullptr) {
            DEBUG_PRINT("Rollback not possible, no factory partition found");
        }
        const auto result = esp_ota_set_boot_partition(factoryPartition);
        if (result != ESP_OK) {
            DEBUG_PRINT("Failed to set boot partition to factory: %s", esp_err_to_name(result));
            return; // Exit the function on error
        }
    }
}

void UpdateHandler::mark_update_valid() {
    if (rollbackDetectionFlagPreserver != ROLLBACK_ARMED) {
        // DEBUG_PRINT("Rollback detection flag not armed, no action needed");
        return; // No action needed if the flag is not armed
    }
    DEBUG_PRINT("Marking update as valid, this will prevent rollback detection");
    rollbackDetectionFlagPreserver = ROLLBACK_NOT_ARMED;
    const auto result = esp_ota_set_boot_partition(esp_ota_get_running_partition());
    if (result != ESP_OK) {
        DEBUG_PRINT("Failed to set boot partition to current running partition: %s", esp_err_to_name(result));
        return; // Exit the function on error
    }
    esp_ota_mark_app_valid_cancel_rollback(); // Mark the current app as valid (if the esp bootloader is configured for rollback detection)
}

void UpdateHandler::mark_update_invalid_and_reboot() {
    DEBUG_PRINT("Marking update as invalid, this will trigger a rollback on next boot");
    const auto result = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (result != ESP_OK) {
        DEBUG_PRINT("Failed to mark app as invalid and reboot: %s", esp_err_to_name(result));
        return; // Exit the function on error
    }
    // If the function returns, the rollback was not possible
    DEBUG_PRINT("Rollback not possible, no other valid partition found");
}
