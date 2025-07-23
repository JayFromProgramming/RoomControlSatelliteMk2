//
// Created by Jay on 10/12/2024.
//

#ifndef NETWORKINTERFACE_H
#define NETWORKINTERFACE_H

#include <WiFi.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include "debug.h"

#define ACTIVITY_LED 2

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

    typedef enum {
        SLOT_FREE,
        MESSAGE_PENDING,
        MESSAGE_SENT,
        MESSAGE_FAILED
    } message_status_t;

    typedef struct {
        SemaphoreHandle_t mutex; // Release this mutex after processing the message
        char data[4096];
        size_t length;
        uint32_t timestamp;
    } downlink_message_t;

    typedef struct {
        char data[4096];
        size_t length;
        uint32_t timestamp;
    } uplink_message_t;

    QueueHandle_t uplink_queue;

private:

    char device_info[1024] = {0};
    size_t device_info_length = 0;
    uint8_t failed_connection_attempts = 0;
    network_state_t last_state = WIRELESS_DOWN;

    TaskHandle_t uplink_task_handle = nullptr;
    TaskHandle_t downlink_task_handle = nullptr;

    QueueHandle_t downlink_queue;

    static void poll_uplink_buffer(void *pvParameters);

    void flush_downlink_queue();
    void handle_uplink_data(const uint8_t* data, const size_t length) const;

    WiFiClient* datalink_client;
    uint32_t last_connection_attempt = 0;
    uint32_t last_transmission = 0;
    const uint32_t connection_interval = 5000;

public:

    NetworkInterface() {
        pinMode(ACTIVITY_LED, OUTPUT);
    }

    static void downlink_task(void *pvParameters);

    void begin(const char* device_info, const size_t device_info_length);

    void establish_connection();

    void queue_message(const char *data, size_t length) const;

};



#endif //NETWORKINTERFACE_H
