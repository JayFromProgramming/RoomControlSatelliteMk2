//
// Created by Jay on 10/12/2024.
//

#ifndef NETWORKINTERFACE_H
#define NETWORKINTERFACE_H

#include <WiFi.h>
#include <ArduinoJson.h>
// #include "secrets.h"
#include "debug.h"
#include "UpdateHandler.h"

#define ACTIVITY_LED 2
#define LEDC_CHANNEL 0
#define LEDC_FREQUENCY_NO_WIFI 6
#define LEDC_FREQUENCY_NO_LINK 1
#define LEDC_TIMER 13

class NetworkInterface {

public:

    typedef enum {
        WIRELESS_DOWN,   // No wifi connection
        GATEWAY_DOWN,    // No connection to the gateway
        NETWORK_DOWN,    // No connection to the internet
        CENTRAL_DOWN,    // No connection to the central server
        CONTROL_DOWN,    // No connection to the control interface (/downlink, /event, /uplink)
        LINK_OK          // All connections are up
    } network_state_t;

    typedef struct {
        char data[4096];
        size_t length;
        uint32_t timestamp;
    } downlink_message_t;

    typedef struct {
        char data[4096];
        size_t length;
        uint32_t timestamp;
    } uplink_message_t;

private:

    QueueHandle_t uplink_queue = nullptr;
    QueueHandle_t downlink_queue = nullptr;

    char device_info[1024] = {0};
    size_t device_info_length = 0;
    uint8_t failed_connection_attempts = 0;
    network_state_t last_state = WIRELESS_DOWN;

    TaskHandle_t uplink_task_handle = nullptr;
    TaskHandle_t downlink_task_handle = nullptr;

    [[noreturn]] static void poll_uplink_buffer(void *pvParameters);

    void flush_downlink_queue();

    void handle_uplink_data(const uint8_t* data, size_t length) const;

    WiFiClient* datalink_client = nullptr;
    uint32_t last_connection_attempt = 0;
    uint32_t last_transmission = 0;

    UpdateHandler *update_handler = new UpdateHandler();

    const char* WIFI_SSID;
    const char* WIFI_PASSWORD;
    const char* CENTRAL_HOST;
    uint16_t CENTRAL_PORT;

public:

    NetworkInterface() = default;

    void pass_network_credentials(const char* ssid, const char* password,
        const char* central_host, const uint16_t central_port) {
        WIFI_SSID = ssid;
        WIFI_PASSWORD = password;
        CENTRAL_HOST = central_host;
        CENTRAL_PORT = central_port;
    }

    [[noreturn]] static void downlink_task(void *pvParameters);

    void connect_wifi();

    void begin(const char* device_info_ptr, size_t info_length);

    void establish_connection();

    void queue_message(const char *data, size_t length) const;

    BaseType_t uplink_queue_receive(uplink_message_t* message, TickType_t ticks_to_wait) const;

};



#endif //NETWORKINTERFACE_H
