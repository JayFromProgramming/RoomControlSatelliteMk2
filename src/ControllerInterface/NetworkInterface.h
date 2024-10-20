//
// Created by Jay on 10/12/2024.
//

#ifndef NETWORKINTERFACE_H
#define NETWORKINTERFACE_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include <ESPAsyncWebServer.h>

#define ACTIVITY_LED 2

class NetworkInterface {

    size_t downlink_buffer[500] = {0};

public:

    typedef enum {
        WIRELESS_DOWN,   // No wifi connection
        GATEWAY_DOWN,    // No connection to the gateway
        NETWORK_DOWN,    // No connection to the internet
        CENTRAL_DOWN,    // No connection to the central server
        CONTROL_DOWN,    // No connection to the control interface (/downlink, /event, /uplink)
        LINK_OK          // All connections are up
    } network_state_t;

    enum target_endpoint {
        UPLINK,
        EVENT
    };

    typedef enum {
        SLOT_FREE,
        MESSAGE_PENDING,
        MESSAGE_SENT,
        MESSAGE_FAILED
    } message_status_t;

    typedef struct {
        SemaphoreHandle_t mutex; // Release this mutex after processing the message
        target_endpoint endpoint;
        char data[1024];
        size_t length;
        uint32_t timestamp;
    } uplink_message_t;

    struct UplinkDataStruct {
        char* payload;
        SemaphoreHandle_t mutex; // The mutex is locked when a new uplink is being generated
        size_t length;
    };

    UplinkDataStruct* uplinkData;

    typedef struct {
        enum type {
            EVENT,
            DOWNLINK
        } type;
        char data[512];
        size_t length;
        uint32_t timestamp;
    } downlink_message_t;

    QueueHandle_t downlink_queue;

private:

    uint8_t failed_connection_attempts = 0;
    network_state_t last_state = WIRELESS_DOWN;

    struct body_data_t {
        uint8_t data[1024];
        size_t length;
    };

    QueueHandle_t uplink_queue;

    void send_messages();

    void on_downlink(AsyncWebServerRequest *request) const;

    void on_body_data(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) const;

    void on_event(AsyncWebServerRequest *request, body_data_t* body_data) const;

    void on_uplink(AsyncWebServerRequest *request) const;

    uint32_t last_connection_attempt = 0;
    uint32_t last_transmission = 0;
    const uint32_t connection_interval = 5000;
    WiFiClient wifi_client;
    AsyncWebServer downlink_server;
    AsyncWebHandler downlink_handler;
    HTTPClient uplink_client;

public:

    NetworkInterface() : downlink_server(47670) {
        pinMode(ACTIVITY_LED, OUTPUT);
    }

    static void network_task(void *pvParameters);

    void begin();

    void pass_uplink_data(UplinkDataStruct* data) {
        this->uplinkData = data;
    }

    network_state_t link_status();

    void queue_message(target_endpoint endpoint, const char *data, size_t length) const;

    static boolean ping_dns();

    static boolean ping_gateway();

    static boolean ping_central();

    void check_wifi_health();
};



#endif //NETWORKINTERFACE_H
