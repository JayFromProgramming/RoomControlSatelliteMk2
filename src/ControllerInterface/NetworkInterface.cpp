//
// Created by Jay on 10/12/2024.
//

#include <ESP32Ping.h>
#include "NetworkInterface.h"

void NetworkInterface::begin() {
    // The network interface runs on Core 0
    this->uplink_queue = xQueueCreate(10, sizeof(uplink_message_t));
    this->downlink_queue = xQueueCreate(10, sizeof(downlink_message_t));

    // Set up the downlink server and it's callbacks (which are lambda's that call other functions)
    this->downlink_server.onRequestBody([this](AsyncWebServerRequest *request,
                                               uint8_t *data, size_t len, size_t index, size_t total) {
        // When the endpoint is hit, call the appropriate function
        if (request->url() == "/downlink") { // Mostly unused right now
            this->on_downlink(request, data, len, index, total);
        } else if (request->url() == "/event") { // Primary method for CENTRAL to control devices
            this->on_event(request, data, len, index, total);
        }
    });
    this->downlink_server.on("/uplink", HTTP_GET, [this](AsyncWebServerRequest *request) {
        // When the endpoint is hit, call the appropriate function
        this->on_uplink(request, nullptr, 0, 0, 0);
    });
    this->downlink_server.begin();
}

void NetworkInterface::on_downlink(AsyncWebServerRequest *request, uint8_t *data, size_t len,
    size_t index, size_t total) const {
    request->send(404, "text/plain", "Not implemented");
}

void NetworkInterface::on_uplink(AsyncWebServerRequest *request, uint8_t *data, size_t len,
    size_t index, size_t total) const {
    request->send(200, "application/json", this->uplinkData->payload);
}

void NetworkInterface::on_event(AsyncWebServerRequest *request, uint8_t *data, size_t len,
size_t index, size_t total) const {
    // If this message is a chunked message then return an error, we don't support that
    // if (index != 0 || index != total) {
    //     request->send(400, "text/plain", "Chunked messages not supported");
    //     return;
    // }
    Serial.println("Received event");
    // Copy the data into a downlink message and send it to the downlink queue
    downlink_message_t message;
    if (len > 512) {
        request->send(400, "text/plain", "Payload too large: Max 512 bytes");
        return;
    }
    memcpy(message.data, data, len);
    message.length = len;
    message.type = downlink_message_t::EVENT;
    const auto result = xQueueSend(downlink_queue, &message, portMAX_DELAY);
    if (result != pdTRUE) {
        request->send(500, "text/plain", "Queue full");
        return;
    }
    request->send(200, "text/plain", "In Queue");
}


[[noreturn]] void NetworkInterface::network_task(void *pvParameters) {
    auto *network_interface = static_cast<NetworkInterface *>(pvParameters);
    while (true) {
        network_interface->last_connection_attempt = millis();
        switch (network_interface->link_status()) {
            case WIRELESS_DOWN:
                // network_interface->check_wifi_health();
                break;
            case NETWORK_DOWN:
            case CENTRAL_DOWN:
                break;
            case LINK_OK:
                network_interface->send_messages();
                break;
            default:
                break;
        }
        vTaskDelay(100);
    }
}

void NetworkInterface::queue_message(
    const target_endpoint endpoint, const char *data, const size_t length) const {
    uplink_message_t message;
    memcpy(message.data, data, length);
    message.length = length;
    message.endpoint = endpoint;
    xQueueSend(uplink_queue, &message, portMAX_DELAY);
}

void NetworkInterface::send_messages() {
    uplink_message_t message;
    while (true) {
        if (xQueueReceive(uplink_queue, &message, 0) == pdTRUE) {
            uplink_client.begin(CENTRAL_HOSTNAME, CENTRAL_PORT,
                message.endpoint == EVENT ? "/event" : "/uplink");
            uplink_client.addHeader("Content-Type", "application/json");
            uplink_client.addHeader("Authorization", CENTRAL_AUTH);
            const int http_code = uplink_client.POST(
                reinterpret_cast<uint8_t *>(message.data), message.length);
            // Serial.print("Message sent with code: ");
            // Serial.println(http_code);
            if (http_code != 200) {
                failed_connection_attempts++;
            }
            taskYIELD(); // Yield to other tasks
        } else break;
    }
}

void NetworkInterface::sync_rtc() {
    // Send an NTP request to the a NTP server to set the onboard RTC

}

/**
 * Check if the DNS server is reachable. Used to validate if the network is up.
 * @return True if the DNS server is reachable, false otherwise.
 */
boolean NetworkInterface::ping_dns() {
    const IPAddress dns_ip(1, 1, 1, 1);
    return Ping.ping(dns_ip);
}

boolean NetworkInterface::ping_gateway() {
    const IPAddress gateway = WiFi.gatewayIP();
    return Ping.ping(gateway);
}

boolean NetworkInterface::ping_central() {
    IPAddress central_ip;
    WiFi.hostByName(CENTRAL_HOSTNAME, central_ip);
    return Ping.ping(central_ip);
}

/**
 * Called only when WL_CONNECTED is false to check why it's down.
 */
void NetworkInterface::check_wifi_health() {
    // Not implemented
}

/**
 * 
 * @return 
 */
NetworkInterface::network_state_t NetworkInterface::link_status() {
    if (WiFiClass::status() != WL_CONNECTED) {
        return WIRELESS_DOWN;
    }
    // if (failed_connection_attempts > 5) {
    //
    // }
    // switch (last_state) {
    //     case WIRELESS_DOWN:
    //         if (ping_dns()) last_state = NETWORK_DOWN;
    //         break;
    //     case NETWORK_DOWN:
    //         if (ping_gateway()) last_state = CENTRAL_DOWN;
    //         break;
    //     case CENTRAL_DOWN:
    //         if (ping_central()) last_state = LINK_OK;
    //         break;
    //     case LINK_OK:
    //         if (!ping_central()) last_state = CENTRAL_DOWN;
    //         break;
    // }
    return LINK_OK;
}


