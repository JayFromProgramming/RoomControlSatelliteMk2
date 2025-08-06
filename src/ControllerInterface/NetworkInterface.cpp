//
// Created by Jay on 10/12/2024.
//

#include "NetworkInterface.h"

#include <esp_task_wdt.h>

const char* wifi_status_to_string(const wl_status_t status) {
    switch (status) {
        case WL_NO_SHIELD: return "No Shield";
        case WL_IDLE_STATUS: return "Idle";
        case WL_NO_SSID_AVAIL: return "No SSID Available";
        case WL_SCAN_COMPLETED: return "Scan Completed";
        case WL_CONNECTED: return "Connected";
        case WL_CONNECT_FAILED: return "Connect Failed";
        case WL_CONNECTION_LOST: return "Connection Lost";
        case WL_DISCONNECTED: return "Disconnected";
        default: return "Unknown";
    }
}

void NetworkInterface::connect_wifi() {
    DEBUG_PRINT("Starting WiFi...");
    ledcSetup(LEDC_CHANNEL, LEDC_FREQUENCY_NO_WIFI, LEDC_TIMER);
    ledcAttachPin(ACTIVITY_LED, LEDC_CHANNEL);
    ledcWrite(LEDC_CHANNEL, 4096); // Turn off the activity LED initially
    WiFi.mode(WIFI_MODE_STA);  // Setup wifi to connect to an access point
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Pass the SSID and Password to the WiFi.begin function
    WiFi.setAutoReconnect(true); // Enable auto reconnect
    WiFi.setHostname("RoomDevice"); // Set the hostname of the device (doesn't seem to work)
    DEBUG_PRINT("Wi-FI MAC Address: %s", WiFi.macAddress().c_str());
    DEBUG_PRINT("Attempting to connect to WiFi SSID: \"%s\" with Password: \"%s\"", WIFI_SSID, WIFI_PASSWORD);
    auto wifi_status = WiFi.waitForConnectResult();
    if (wifi_status != WL_CONNECTED) {
        DEBUG_PRINT("WiFi failed to connect [%s], attempting again...", wifi_status_to_string(static_cast<wl_status_t>(wifi_status)));
        vTaskDelay(5000);
        connect_wifi(); // Retry connecting to WiFi
        return;
    }
    DEBUG_PRINT("WiFi connected [%s] with IP: %s",
        wifi_status_to_string(static_cast<wl_status_t>(wifi_status)),
        WiFi.localIP().toString().c_str());
    ledcDetachPin(ACTIVITY_LED); // Detach the LED pin after connecting to WiFi
}

void NetworkInterface::begin(const char* device_info_ptr, const size_t info_length) {
    DEBUG_PRINT("Initializing Network Interface");
    connect_wifi();
    pinMode(ACTIVITY_LED, OUTPUT);
    memcpy(this->device_info, device_info_ptr, info_length);
    this->device_info_length = info_length;
    // The network interface runs on Core 0
    this->downlink_queue = xQueueCreate(5, sizeof(downlink_message_t));
    if (this->downlink_queue == nullptr) {
        DEBUG_PRINT("Failed to create downlink queue");
        return;
    }
    this->uplink_queue = xQueueCreate(5, sizeof(uplink_message_t));
    if (this->uplink_queue == nullptr) {
        DEBUG_PRINT("Failed to create uplink queue");
        vQueueDelete(this->downlink_queue);
        return;
    }
    // Setup wifi client
    this->datalink_client = new WiFiClient();
    // Initialize the tcp/ip connection to the server
    this->datalink_client->setTimeout(15); // Set a timeout for the connection
    // this->datalink_client->setNoDelay(true); // Disable Nagle's algorithm for low latency
    this->establish_connection();
    // esp_task_wdt_add(system_tasks[0].handle);
    xTaskCreate(downlink_task,"downlink_task", 8192,
        this,1, &this->downlink_task_handle);
    xTaskCreate(poll_uplink_buffer,"uplink_task", 16384,
        this,1 , &this->uplink_task_handle);
    this->update_handler->begin();
    esp_task_wdt_add(this->downlink_task_handle);
    esp_task_wdt_add(this->uplink_task_handle);
    DEBUG_PRINT("Network interface initialized successfully");
}

void NetworkInterface::establish_connection() {
    DEBUG_PRINT("Establishing connection to %s:%d", CENTRAL_HOST, CENTRAL_PORT);
    ledcSetup(LEDC_CHANNEL, LEDC_FREQUENCY_NO_LINK, LEDC_TIMER);
    ledcAttachPin(ACTIVITY_LED, LEDC_CHANNEL);
    ledcWrite(LEDC_CHANNEL, 4096);
    while (true) {
        esp_task_wdt_reset();
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
        ledcDetachPin(ACTIVITY_LED); // Detach the LED pin if we failed to write
        return;
    }
    ledcDetachPin(ACTIVITY_LED); // Detach the LED pin after writing
    DEBUG_PRINT("Device information sent successfully [%d bytes]", wrote);
}

[[noreturn]] void NetworkInterface::downlink_task(void *pvParameters) {
    auto *network_interface = static_cast<NetworkInterface *>(pvParameters);
    while (true) {
        network_interface->last_connection_attempt = millis();
        network_interface->flush_downlink_queue();
        esp_task_wdt_reset();
    }
}

[[noreturn]] void NetworkInterface::poll_uplink_buffer(void *pvParameters) {
    DEBUG_PRINT("Starting Uplink Task");
    const auto *network_interface = static_cast<NetworkInterface *>(pvParameters);
    uint8_t buffer[4096] = {0}; // Buffer to hold incoming data
    while (true) {
        if (!network_interface->datalink_client->connected()) {
            esp_task_wdt_reset();
            vTaskDelay(5000);
        }
        // Check if there is data to read from the WebSocket client
        const auto read =
            network_interface->datalink_client->readBytesUntil('\0', buffer, sizeof(buffer) - 1);
        if (read > 0) {
            buffer[read] = '\0'; // Null-terminate the buffer
            network_interface->handle_uplink_data(buffer, read + 1);
        }
        esp_task_wdt_reset();
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
    BaseType_t status = pdFALSE;
    switch (data[0]){
        case '\b':
            memcpy(message.data, data + 1, length);
            // Send the message to the uplink queue
            status = xQueueSend(this->uplink_queue, &message, 200);
            if (status != pdTRUE) {
                DEBUG_PRINT("Failed to move inbound message to uplink queue %s",
                               status == errQUEUE_FULL ? "Queue is full" : "Unknown error");
            }
        break;
        case '\t': // This is a heartbeat message
            update_handler->passData(data + 1, length - 2); // Pass the data to the update handler
        break;
        default:
            DEBUG_PRINT("Received unknown message type %d", data[0]);
            return; // Ignore unknown message types
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
        esp_task_wdt_reset();
        if (xQueueReceive(downlink_queue, &message, 100) == pdTRUE) {
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
            message.data[message.length] = '\0'; // Ensure the last byte is a null terminator
            const auto wrote = datalink_client->write(message.data, message.length + 1); // +1 for the null terminator
            if (wrote != message.length + 1) {
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
            UpdateHandler::mark_update_valid();
            last_transmission = millis();
            analogWrite(ACTIVITY_LED, 0); // Turn off the activity LED to indicate no activity
            esp_task_wdt_reset();
            taskYIELD(); // Yield to other tasks
        } else break;
    }
}

BaseType_t NetworkInterface::uplink_queue_receive(uplink_message_t* message, const TickType_t waitTime) const {
    if (this->uplink_queue == nullptr) {
        DEBUG_PRINT("Uplink queue is not initialized, cannot get message");
        return pdFALSE;
    }
    return xQueueReceive(this->uplink_queue, message, waitTime);
}