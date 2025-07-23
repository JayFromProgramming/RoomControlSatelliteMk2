//
// Created by Jay on 10/12/2024.
//

#include <ESP32Ping.h>
#include "NetworkInterface.h"

#include <esp_task_wdt.h>

void NetworkInterface::begin(const char* device_info, const size_t device_info_length) {
    DEBUG_PRINT("Initializing Network Interface");
    memcpy(this->device_info, device_info, device_info_length);
    this->device_info_length = device_info_length;
    // The network interface runs on Core 0
    this->downlink_queue = xQueueCreate(10, sizeof(downlink_message_t));
    if (this->downlink_queue == nullptr) {
        DEBUG_PRINT("Failed to create downlink queue");
        return;
    }
    this->uplink_queue = xQueueCreate(10, sizeof(uplink_message_t));
    if (this->uplink_queue == nullptr) {
        DEBUG_PRINT("Failed to create uplink queue");
        vQueueDelete(this->downlink_queue);
        return;
    }
    // Setup wifi client
    this->datalink_client = new WiFiClient();
    // Initialize the tcp/ip connection to the server
    this->datalink_client->setTimeout(30); // Set a timeout for the connection
    // this->datalink_client->setNoDelay(true); // Disable Nagle's algorithm for low latency
    this->establish_connection();
    // esp_task_wdt_add(system_tasks[0].handle);
    xTaskCreate(NetworkInterface::downlink_task,
        "NetworkInterface::downlink_task",
        20000,
        this,
        1,
        &this->downlink_task_handle
    );
    xTaskCreate(NetworkInterface::poll_uplink_buffer,
        "NetworkInterface::poll_uplink_buffer",
        10000,
        this,
        1,
        &this->uplink_task_handle
    );
    DEBUG_PRINT("Network interface initialized successfully");
}

void NetworkInterface::establish_connection() {
    DEBUG_PRINT("Establishing connection to %s:%d", CENTRAL_HOST, CENTRAL_PORT);
    while (true) {
    const auto connect_result = this->datalink_client->connect(CENTRAL_HOST, CENTRAL_PORT);
        if (!connect_result) {
            DEBUG_PRINT("Failed to connect to %s:%d, retrying in 5 seconds...", CENTRAL_HOST, CENTRAL_PORT);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue; // Retry connection
        }
        break; // Connection successful
    }
    DEBUG_PRINT("Connected to %s:%d, sending device information...", CENTRAL_HOST, CENTRAL_PORT);
    device_info[device_info_length + 1] = '\0'; // Ensure the last byte is a null terminated
    const auto wrote = this->datalink_client->write(device_info, device_info_length + 1); // +1 for the null terminator
    if (wrote != device_info_length + 1) {
        DEBUG_PRINT("Failed to write device info to server, wrote %d != %d bytes",
                   wrote, device_info_length);
        return;
    }
    DEBUG_PRINT("Device information sent successfully [%d bytes]", wrote);
}

[[noreturn]] void NetworkInterface::downlink_task(void *pvParameters) {
    DEBUG_PING("Starting Downlink Task");
    auto *network_interface = static_cast<NetworkInterface *>(pvParameters);
    while (true) {
        network_interface->last_connection_attempt = millis();
        switch (WiFi.status()) {
            case WL_CONNECTED:
                network_interface->flush_downlink_queue();
                break;
            case WL_DISCONNECTED:
                WiFi.reconnect();
            default:
                break;
        }
        esp_task_wdt_reset();
        vTaskDelay(100);
    }
}

[[noreturn]] void NetworkInterface::poll_uplink_buffer(void *pvParameters) {
    DEBUG_PRINT("Starting Uplink Task");
    const auto *network_interface = static_cast<NetworkInterface *>(pvParameters);
    uint8_t buffer[4096] = {0}; // Buffer to hold incoming data
    while (true) {
        if (!network_interface->datalink_client->connected()) {
            vTaskDelay(5000);
        }
        // Check if there is data to read from the WebSocket client
        const auto read = network_interface->datalink_client->readBytesUntil('\0', buffer, sizeof(buffer) - 1);
        if (read > 0) {
            buffer[read] = '\0'; // Null-terminate the buffer
            network_interface->handle_uplink_data(buffer, read + 1);
        }
    }
}

void NetworkInterface::queue_message(const char *data, const size_t length) const {
    downlink_message_t message;
    memcpy(message.data, data, length);
    message.length = length;
    message.timestamp = micros();
    if (downlink_queue == nullptr) {
        DEBUG_PRINT("Downlink queue is not initialized, cannot send message");
        return;
    }

    xQueueSend(downlink_queue, &message, 10000);
}

/**
 * This event handler is called when the WebSocket client receives data from the server.
 */
void NetworkInterface::handle_uplink_data(const uint8_t* data, const size_t length) const {
    // Put the received data into a message structure
    uplink_message_t message;
    message.length = length;
    message.timestamp = millis();
    memset(message.data, 0, sizeof(message.data)); // Clear the data buffer
    memcpy(message.data, data, length);

    // Send the message to the uplink queue
    const auto status = xQueueSend(this->uplink_queue, &message, 200);
    if (status != pdTRUE) {
        DEBUG_PRINT("Failed to move inbound message to uplink queue %s",
                       status == errQUEUE_FULL ? "Queue is full" : "Unknown error");
    }
}


/**
 *
 */
void NetworkInterface::flush_downlink_queue() {
    downlink_message_t message;
    while (true) {
        if (this->downlink_queue == nullptr) {
            DEBUG_PRINT("Downlink queue is not initialized, cannot flush");
            return;
        }
        if (xQueueReceive(downlink_queue, &message, 0) == pdTRUE) {
            analogWrite(ACTIVITY_LED, 32);
            if (WiFi.status() != WL_CONNECTED) {
                DEBUG_PRINT("WiFi is not connected, skipping message");
                analogWrite(ACTIVITY_LED, 0);
                break;
            }
            if (!datalink_client->connected()) {
                DEBUG_PRINT("Socket is not connected, attempting to reconnect");
                this->establish_connection();
                if (!datalink_client->connected()) {
                    DEBUG_PRINT("Failed to reconnect to server, skipping message");
                    analogWrite(ACTIVITY_LED, 0);
                    continue; // Skip this message if we can't reconnect
                }
            }
            const uint32_t queue_time = micros() - message.timestamp;
            // Init a timer to keep track of how long it takes to send a message
            const uint32_t start_time = micros();
            const auto wrote = datalink_client->write(message.data, message.length);
            datalink_client->write("\0", 1); // Write a null terminator to indicate end of message
            if (wrote != message.length ) {
                DEBUG_PRINT("Failed to write downlink message, wrote %d != %d bytes",
                               wrote, message.length + 1);
                analogWrite(ACTIVITY_LED, 0);
                continue; // Skip this message if we can't write it
            }
            DEBUG_PRINT("Downlink sent [%d bytes] in %dus [%.03f KB/s] [Queue Time: %.02fms]",
                message.length,
                micros() - start_time,
                message.length / ((micros() - start_time) / 1000000.0f) / 1024.0f,
                queue_time / 1000.0f);
            last_transmission = millis();
            analogWrite(ACTIVITY_LED, 0); // Turn off the activity LED to indicate no activity
            esp_task_wdt_reset();
            taskYIELD(); // Yield to other tasks
        } else break;
    }
}

