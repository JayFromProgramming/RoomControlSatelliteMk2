//
// Created by Jay on 10/12/2024.
//

#include <ESP32Ping.h>
#include "NetworkInterface.h"

#include <esp_task_wdt.h>

void NetworkInterface::begin() {
    // The network interface runs on Core 0
    this->downlink_queue = xQueueCreate(10, sizeof(downlink_message_t));
    this->uplink_queue = xQueueCreate(10, sizeof(uplink_message_t));
    constexpr esp_websocket_client_config_t ws_cfg = {
        .uri = CENTRAL_DATALINK_ENDPOINT,
        .port = CENTRAL_DATALINK_PORT,
        .buffer_size = 1024,
    };
    this->datalink_client = esp_websocket_client_init(&ws_cfg);
    if (this->datalink_client == nullptr) {
        Serial.println("FATAL: Failed to initialize WebSocket client");
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("FATAL: WiFi is not connected, cannot start network interface");
        return;
    }
    const esp_err_t err = esp_websocket_client_start(this->datalink_client);
    if (err != ESP_OK) {
        Serial.printf("FATAL: Failed to start WebSocket client: %s\n", esp_err_to_name(err));
        return;
    }
    // Send device information to the server hardcoded for now
    constexpr char device_info[] = R"({"name": "RoomDevice", "sub_device_count": 2, "sub_devices": ["radiator", "motion_detector"], "msg_type":"connection_info"})";
    const esp_err_t info_err = esp_websocket_client_send_text(
        this->datalink_client, device_info, sizeof(device_info) - 1, 1000);
    if (info_err != ESP_OK) {
        Serial.printf("FATAL: Failed to send device info: %s\n", esp_err_to_name(info_err));
        return;
    }
    esp_websocket_register_events(
        this->datalink_client,
        WEBSOCKET_EVENT_DATA,
        NetworkInterface::on_datalink_uplink,
        this->datalink_client
    );
    Serial.println("Network Interface Initialized");
}

[[noreturn]] void NetworkInterface::network_task(void *pvParameters) {
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


void NetworkInterface::queue_message(
    const target_endpoint endpoint, const char *data, const size_t length) const {
    downlink_message_t message;
    memcpy(message.data, data, length);
    message.length = length;
    message.endpoint = endpoint;
    message.timestamp = millis();
    xQueueSend(downlink_queue, &message, 10000);
}

/**
 * This event handler is called when the WebSocket client receives data from the server.
 * @param handler_args Pointer to the NetworkInterface instance that registered this handler.
 * @param base The event base, which can be assumed to be WEBSOCKET_EVENT_BASE.
 * @param event_id Can be assumed to be WEBSOCKET_EVENT_DATA
 * @param event_data Pointer to the event data, which is an esp_websocket_event_data_t structure.
 */
void NetworkInterface::on_datalink_uplink(void *handler_args, esp_event_base_t base,
                                          int32_t event_id, void *event_data){
    const auto *network_interface = static_cast<NetworkInterface *>(handler_args);
    const auto *data = static_cast<esp_websocket_event_data_t *>(event_data);
    if (data->payload_offset != 0) {
        Serial.printf("Received data with payload offset %d, ignoring\n", data->payload_offset);
        return; // Ignore messages with payload offset
    }
    if (data->data_len == 0) {
        Serial.println("Received empty message, ignoring");
        return; // Ignore empty messages
    }
    if (data->data_len >= 1024) {
        Serial.printf("Received message too large (%d bytes), ignoring\n", data->data_len);
        return; // Ignore messages that are too large
    }
    if (data->data_len > sizeof(uplink_message_t::data)) {
        Serial.printf("Received message too large (%d bytes), ignoring\n", data->data_len);
        return; // Ignore messages that are too large
    }
    // Put the received data into a message structure
    uplink_message_t message;
    message.length = data->data_len;
    message.timestamp = millis();
    memcpy(message.data, data->data_ptr, data->data_len);
    // Send the message to the uplink queue
    const auto status = xQueueSend(network_interface->uplink_queue, &message, 200);
    if (status != pdTRUE) {
        Serial.printf("Failed to move inbound message to uplink queue %s\n",
                       status == errQUEUE_FULL ? "Queue is full" : "Unknown error");
    } else {
        Serial.printf("Received uplink message [%d bytes]: %s\n", message.length, message.data);
    }
}


/**
 *
 */
void NetworkInterface::flush_downlink_queue() {
    downlink_message_t message;
    while (true) {
        if (xQueueReceive(downlink_queue, &message, 0) == pdTRUE) {
            analogWrite(ACTIVITY_LED, 32);
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("WiFi is not connected, skipping message");
                analogWrite(ACTIVITY_LED, 0);
                break;
            }
            if (!esp_websocket_client_is_connected(datalink_client)) {
                Serial.println("WebSocket client is not connected, aborting data downlink");
                analogWrite(ACTIVITY_LED, 0);
                break;
            }
            const uint32_t queue_time = millis() - message.timestamp;
            // Init a timer to keep track of how long it takes to send a message
            const uint32_t start_time = millis();
            const esp_err_t err = esp_websocket_client_send_text(
                datalink_client, message.data, message.length, 1000);
            if (err != ESP_OK) {
                Serial.printf("Failed to send message: %s\n", esp_err_to_name(err));
                analogWrite(ACTIVITY_LED, 0);
                continue; // Skip this message if it failed to send
            }
            Serial.printf("%s sent [%d bytes] in %dms [%.02f bytes/s] [Queue Time: %dms]\n",
                message.endpoint == EVENT ? "Event " : "Uplink",
                message.length,
                millis() - start_time,
                message.length / ((millis() - start_time) / 1000.0),
                queue_time);
            last_transmission = millis();
            analogWrite(ACTIVITY_LED, 0); // Turn off the activity LED to indicate no activity
            esp_task_wdt_reset();
            taskYIELD(); // Yield to other tasks
        } else break;
    }
}

/**
 * 
 * @return 
 */
NetworkInterface::network_state_t NetworkInterface::link_status() {
    if (WiFiClass::status() != WL_CONNECTED) {
        return WIRELESS_DOWN;
    }
    return LINK_OK;
}


