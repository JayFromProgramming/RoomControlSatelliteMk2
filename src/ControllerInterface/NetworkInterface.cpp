//
// Created by Jay on 10/12/2024.
//

#include <ESP32Ping.h>
#include "NetworkInterface.h"

#include <esp_task_wdt.h>

void NetworkInterface::begin() {
    // The network interface runs on Core 0
    this->uplink_queue = xQueueCreate(10, sizeof(uplink_message_t));
    this->downlink_queue = xQueueCreate(10, sizeof(downlink_message_t));

    // Set up the downlink server and it's callbacks (which are lambda's that call other functions)
    this->downlink_server.onRequestBody([this](AsyncWebServerRequest *request,
                                               uint8_t *data, size_t len, size_t index, size_t total) {
        this->on_body_data(request, data, len, index, total);
    });
    this->downlink_server.on("/uplink", HTTP_GET, [this](AsyncWebServerRequest *request) {
        // When the endpoint is hit, call the appropriate function
        this->on_uplink(request);
    });
    this->downlink_server.begin();
}

void NetworkInterface::on_downlink(AsyncWebServerRequest *request) const {
    request->send(404, "text/plain", "Not implemented");
}

void NetworkInterface::on_body_data(AsyncWebServerRequest *request, uint8_t *data, size_t len,
    size_t index, size_t total) const {
    if (request->_tempObject == nullptr) {
        request->_tempObject = new body_data_t();
        analogWrite(ACTIVITY_LED, 64);
    }
    // This method might be called multiple times to build up the body data
    auto *body_data = static_cast<body_data_t *>(request->_tempObject);
    memcpy(body_data->data + body_data->length, data, len);
    body_data->length += len;
    if (index + len == total) {
        // If this is the last chunk of data, call the appropriate function
        if (strcmp(request->url().c_str(), "/event") == 0) {
            this->on_event(request, body_data);
        } else {
            request->send(404, "text/plain", "Not implemented");
            analogWrite(ACTIVITY_LED, 0);
        }
    }
}

void NetworkInterface::on_uplink(AsyncWebServerRequest *request) const {
    analogWrite(ACTIVITY_LED, 128);
    xSemaphoreTake(this->uplinkData->mutex, portMAX_DELAY);
    request->send(200, "application/json", this->uplinkData->payload);
    analogWrite(ACTIVITY_LED, 0);
    xSemaphoreGive(this->uplinkData->mutex);
}

void NetworkInterface::on_event(AsyncWebServerRequest *request, body_data_t* body_data) const {
    // Read the body data from the request
    if (body_data == nullptr) {
        request->send(400, "text/plain", "No body data");
        analogWrite(ACTIVITY_LED, 0);
        return;
    }
    // Copy the data into a downlink message and send it to the downlink queue
    downlink_message_t message;
    if (body_data->length > 512) {
        request->send(400, "text/plain", "Payload too large: Max 512 bytes");
        analogWrite(ACTIVITY_LED, 0);
        return;
    }
    memcpy(message.data, body_data->data, body_data->length);
    message.length = body_data->length;
    message.type = downlink_message_t::EVENT;
    const auto result = xQueueSend(downlink_queue, &message, 1000);
    if (result != pdTRUE) {
        request->send(500, "text/plain", "Queue full");
        analogWrite(ACTIVITY_LED, 0);
        return;
    }
    request->send(200, "text/plain", "In Queue");
    // delete body_data;
    analogWrite(ACTIVITY_LED, 0);
}


[[noreturn]] void NetworkInterface::network_task(void *pvParameters) {
    auto *network_interface = static_cast<NetworkInterface *>(pvParameters);
    while (true) {
        network_interface->last_connection_attempt = millis();
        if (WiFi.status() == WL_CONNECTED) {
            network_interface->send_messages();
        } else if (WiFi.status() == WL_CONNECT_FAILED) {
            // Reboot
            ESP.restart();
        } else {
            WiFi.reconnect();
        }
        esp_task_wdt_reset();
        vTaskDelay(100);
    }
}

void NetworkInterface::queue_message(
    const target_endpoint endpoint, const char *data, const size_t length) const {
    uplink_message_t message;
    memcpy(message.data, data, length);
    message.length = length;
    message.endpoint = endpoint;
    message.timestamp = millis();
    xQueueSend(uplink_queue, &message, 10000);
}

/**
 *
 */
void NetworkInterface::send_messages() {
    uplink_message_t message;
    while (true) {
        if (xQueueReceive(uplink_queue, &message, 0) == pdTRUE) {
            analogWrite(ACTIVITY_LED, 32);
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("WiFi is not connected, skipping message");
                analogWrite(ACTIVITY_LED, 0);
                break;
            }
            const uint32_t queue_time = millis() - message.timestamp;
            // Init a timer to keep track of how long it takes to send a message
            const uint32_t start_time = millis();
            uplink_client.begin(CENTRAL_HOSTNAME, CENTRAL_PORT,
                message.endpoint == EVENT ? "/event" : "/uplink");
            uplink_client.addHeader("Content-Type", "application/json");
            uplink_client.setUserAgent("ESP32");
            uplink_client.setTimeout(2500);
            const uint32_t setup_time = millis() - start_time;
            // uplink_client.addHeader("Authorization", CENTRAL_AUTH);
            // For debug print the message
            const int http_code = uplink_client.POST(
                reinterpret_cast<uint8_t *>(message.data), message.length);
            // Serial.print("Message sent with code: ");
            // Serial.println(http_code);
            if (http_code != 200) {
                Serial.printf("Failed to send message with code: %d\n", http_code);
                failed_connection_attempts++;
            }
            uplink_client.end();
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


