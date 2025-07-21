//
// Created by Jay on 10/12/2024.
//

#ifndef NETWORKINTERFACE_H
#define NETWORKINTERFACE_H

#include <WiFi.h>
#include <ArduinoJson.h>

#include <esp_websocket_client.h>
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

    enum target_endpoint {
        DOWNLINK,
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
    } downlink_message_t;

    struct UplinkDataStruct {
        char* payload;
        SemaphoreHandle_t mutex; // The mutex is locked when a new uplink is being generated
        size_t length;
    };

    UplinkDataStruct* uplinkData;

    typedef struct {
        char data[512];
        size_t length;
        uint32_t timestamp;
    } uplink_message_t;

    QueueHandle_t uplink_queue;

private:

    uint8_t failed_connection_attempts = 0;
    network_state_t last_state = WIRELESS_DOWN;

    struct body_data_t {
        uint8_t data[1024];
        size_t length;
    };

    QueueHandle_t downlink_queue;


    void flush_downlink_queue();

    esp_websocket_client_handle_t datalink_client = nullptr;
    uint32_t last_connection_attempt = 0;
    uint32_t last_transmission = 0;
    const uint32_t connection_interval = 5000;

public:

    NetworkInterface() {
        pinMode(ACTIVITY_LED, OUTPUT);
    }

    static void on_datalink_uplink(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

    static void network_task(void *pvParameters);

    void begin();

    void pass_uplink_data(UplinkDataStruct* data) {
        this->uplinkData = data;
    }

    network_state_t link_status();

    void queue_message(const char *data, size_t length) const;

};



#endif //NETWORKINTERFACE_H
