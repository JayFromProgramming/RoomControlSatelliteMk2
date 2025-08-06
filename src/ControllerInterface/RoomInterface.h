//
// Created by Jay on 10/12/2024.
//

#ifndef ROOMINTERFACE_H
#define ROOMINTERFACE_H

#include "NetworkInterface.h"
#include "RoomDevice.h"
#include "RoomInterfaceDatastructures.h"
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class RoomDevice;

// Room Interface is a singleton class that all room devices will bind themselves to in order to be
// controlled by the central controller.

class RoomInterface {

    // Setup scratch space for storing the parsed arguments for multiple events so we don't have to malloc/free
    // every time we parse an event.
    const char* deviceName = nullptr; // Name of the device this interface is running on
    const char* deviceVersion = nullptr; // Version of the device this interface is running on
    const char* deviceBranch = nullptr; // Branch of the device this interface is running on
    NetworkInterface* networkInterface = new NetworkInterface();

    ParsedEvent_t argumentScratchSpace[4] = {};

    JsonDocument event_document = JsonDocument();
    JsonDocument downlink_document = JsonDocument();
    SemaphoreHandle_t downlink_buffer_mutex = xSemaphoreCreateMutex();
    char downlink_buffer[4096] = {0}; // Buffer for the downlink data

    mutable TickType_t lastWakeTime = 0; // Last time the interface loop was woken up
    const TickType_t loopInterval = 15000 / portTICK_PERIOD_MS;  // Wake to send the status update every 30 seconds

    TaskHandle_t roomInterfaceTaskHandle = nullptr;
    TaskHandle_t eventLoopTaskHandle = nullptr;
    TaskHandle_t interfaceHealthCheckTaskHandle = nullptr;

    struct DeviceList {
        RoomDevice* device;
        TaskHandle_t taskHandle;
        DeviceList* next;
    };
    DeviceList* devices = nullptr;

    SemaphoreHandle_t downlinkSemaphore = xSemaphoreCreateBinary();
    SemaphoreHandle_t exclusive_downlink_mutex = xSemaphoreCreateMutex();
    TickType_t last_event_parse = 0;

    uint32_t last_full_send = 0; // Last time the downlink was sent
    char* downlink_target_device = nullptr;

public:

    RoomInterface() = default;

    void setNetworkCredentials(const char* ssid, const char* password,
        const char* central_host = nullptr, const uint16_t central_port = 0) const {
        networkInterface->pass_network_credentials(ssid, password, central_host, central_port);
    }

    size_t getDeviceInfo(char* buffer) const;

    void begin(const char* device_name, const char* device_version, const char* device_branch);

    /**
     * Writes a string to the scratch space buffer and returns a pointer to the string in the buffer.
     * This prevents the need to malloc/free every string which would cause memory fragmentation.
     * @param string The string to write to the buffer.
     * @param scratchSpace The scratch space to write to.
     * @return A pointer to the string in the buffer.
     */
    static char* write_string_to_scratch_space(const char* string, ParsedEvent_t* scratchSpace) {
        uint16_t buffer_position = 0;
        auto* buffer = scratchSpace->stringBuffer + scratchSpace->stringIndex;
        strcpy(buffer, string);
        scratchSpace->stringIndex += strlen(string) + 1;
        return buffer;
    }

    static void cleanup_scratch_space(ParsedEvent_t* scratchSpace) {
        scratchSpace->numArgs = 0;
        scratchSpace->numKwargs = 0;
        scratchSpace->stringIndex = 0;
        scratchSpace->document.clear();
        scratchSpace->finished = true;
    }

    ParsedEvent_t* get_free_scratch_space() const {
        for (const auto & i : argumentScratchSpace) {
            if (i.finished == true) {
                return const_cast<ParsedEvent_t *>(&i);
            }
        }
        return nullptr;
    }

    void addDevice(RoomDevice* device) {
        auto* newDevice = new DeviceList();
        newDevice->device = device;
        newDevice->next = devices;
        devices = newDevice;
    }

    size_t getDeviceCount() const {
        size_t count = 0;
        for (auto current = devices; current != nullptr; current = current->next) {
            count++;
        }
        return count;
    }

    void startDeviceLoops() const;

    void sendDownlink(); // Send the uplink data to the network interface.

    void downlinkNow(char* target_device); // Set the uplink semaphore to send the uplink now instead of waiting for next timer.

    static void interfaceLoop(void *pvParameters);

    static void eventLoop(void *pvParameters);

    static void interfaceHealthCheck(void* pvParameters);

    void sendEvent(ParsedEvent_t* event);

    ParsedEvent_t* eventParse(const char* data);

    void eventExecute(ParsedEvent_t* event) const;

};



#endif //ROOMINTERFACE_H
