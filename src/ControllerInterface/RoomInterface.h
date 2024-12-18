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

public:

    struct TaskPile {
        TaskHandle_t** handles;
        const char **names = nullptr;
        size_t count;
    };


private:

    // Setup scratch space for storing the parsed arguments for multiple events so we don't have to malloc/free
    // every time we parse an event.

    ParsedEvent_t argumentScratchSpace[10] = {};

    JsonDocument event_document = JsonDocument();
    JsonDocument uplink_document = JsonDocument();

    mutable TickType_t lastWakeTime;
    const TickType_t loopInterval = 30000 / portTICK_PERIOD_MS;  // Wake to send the status update every 30 seconds

    struct DeviceList {
        RoomDevice* device;
        TaskHandle_t taskHandle;
        DeviceList* next;
    };

    struct SystemTasks {
        TaskHandle_t handle;
        const char* name;
    };

    char* uplink_buffer = new char[1024];
    uint32_t last_full_send;
    NetworkInterface* networkInterface = new NetworkInterface();
    NetworkInterface::UplinkDataStruct* uplinkData = new NetworkInterface::UplinkDataStruct();
    DeviceList* devices = nullptr;
    SystemTasks* system_tasks = new SystemTasks[4];
    SemaphoreHandle_t uplinkSemaphore = xSemaphoreCreateBinary();
    SemaphoreHandle_t exclusive_uplink_mutex = xSemaphoreCreateMutex();
    TickType_t last_event_parse;
    char* uplink_target_device = nullptr;

public:

    RoomInterface() {

    }

    void begin() {
        Serial.println("Initializing Room Interface");
        // The network interface runs on Core 0
        uplinkData->payload = uplink_buffer;
        uplinkData->length = 0;
        uplinkData->mutex = xSemaphoreCreateMutex(); // The mutex is locked when accessing the uplink data
        networkInterface->pass_uplink_data(uplinkData);
        networkInterface->begin();
        for (auto & i : argumentScratchSpace) {
            i.finished = true;
        }

        xTaskCreate(
            RoomInterface::interfaceLoop,
            "interfaceLoop",
            10000,
            const_cast<RoomInterface*>(this),
            2,
            &system_tasks[0].handle
        );
        // esp_task_wdt_add(system_tasks[0].handle);
        xTaskCreatePinnedToCore(
            NetworkInterface::network_task,
            "networkTask",
            10000,
            networkInterface,
            1,
            &system_tasks[1].handle,
            0
        );
        esp_task_wdt_add(system_tasks[1].handle);
        xTaskCreatePinnedToCore(
            eventLoop,
            "eventLoop",
            20000,
            const_cast<RoomInterface*>(this),
            2,
            &system_tasks[2].handle,
            1
        );
        esp_task_wdt_add(system_tasks[2].handle);
        xTaskCreate(
            interfaceHealthCheck,
            "interfaceHealthCheck",
            10000,
            const_cast<RoomInterface*>(this),
            0,
            &system_tasks[3].handle
        );
        esp_task_wdt_add(system_tasks[3].handle);
        startDeviceLoops();
        Serial.println("Room Interface Initialized");
    }

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

    void sendUplink(); // Send the uplink data to the network interface.

    void uplinkNow(char* target_device); // Set the uplink semaphore to send the uplink now instead of waiting for next timer.

    static void interfaceLoop(void *pvParameters);

    static void eventLoop(void *pvParameters);

    static void interfaceHealthCheck(void* pvParameters);

    void sendEvent(ParsedEvent_t* event) const;

    ParsedEvent_t* eventParse(const char* data);

    void eventExecute(ParsedEvent_t* event) const;

    TaskPile getAllTaskHandles() const;
};



#endif //ROOMINTERFACE_H
