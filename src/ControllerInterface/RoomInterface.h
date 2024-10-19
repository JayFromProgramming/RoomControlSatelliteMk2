//
// Created by Jay on 10/12/2024.
//

#ifndef ROOMINTERFACE_H
#define ROOMINTERFACE_H

#include "NetworkInterface.h"
#include "RoomDevice.h"
#include "RoomInterfaceDatastructures.h"
#include <ArduinoJson.h>

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

    char* uplink_buffer = new char[512];
    NetworkInterface* networkInterface = new NetworkInterface();
    NetworkInterface::UplinkDataStruct* uplinkData = new NetworkInterface::UplinkDataStruct();
    DeviceList* devices = nullptr;
    SystemTasks* system_tasks = new SystemTasks[3];
    SemaphoreHandle_t uplinkSemaphore = xSemaphoreCreateBinary();

public:

    RoomInterface() {

    }

    void begin() {
        Serial.println("Initializing Room Interface");
        // The network interface runs on Core 0
        uplinkData->payload = uplink_buffer;
        uplinkData->length = 0;
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
            1,
            &system_tasks[0].handle
        );

        xTaskCreate(
            NetworkInterface::network_task,
            "networkTask",
            10000,
            networkInterface,
            1,
            &system_tasks[1].handle
        );
        xTaskCreate(
            eventLoop,
            "eventLoop",
            20000,
            const_cast<RoomInterface*>(this),
            1,
            &system_tasks[2].handle
        );

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

    void sendUplink() const; // Send the uplink data to the network interface.

    void uplinkNow() const; // Set the uplink semaphore to send the uplink now instead of waiting for next timer.

    static void interfaceLoop(void *pvParameters);

    static void eventLoop(void *pvParameters);

    void sendEvent(const ParsedEvent_t* event) ;

    ParsedEvent_t* eventParse(const char* data) const;

    void eventExecute(ParsedEvent_t* event) const;

    TaskPile getAllTaskHandles() const;
};



#endif //ROOMINTERFACE_H
