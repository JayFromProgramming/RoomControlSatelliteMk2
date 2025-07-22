//
// Created by Jay on 10/12/2024.
//

#ifndef ROOMDEVICE_H
#define ROOMDEVICE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "RoomInterfaceDatastructures.h"
#include "RoomInterface.h"


class RoomInterface;

/**
 * This class is a base class for all devices that can be controlled by the RoomInterface.
 */
class RoomDevice {

public:
    virtual ~RoomDevice() = default;

    const char* object_type;
    const char* object_name;

    const uint16_t PRIORITY = 1; // Default priority is the lowest
    const uint16_t STACK_SIZE = 4096;

    JsonDocument deviceData;

private:

    // Define the event callback list structure
    struct EventCallbackList {
        void (*callback)(RoomDevice* self, const ParsedEvent_t* data);
        const char* event_name;
        EventCallbackList* next;
    };

    EventCallbackList* eventCallbacks = nullptr;

protected:
    /**
     * This function is called by the subclass to add an event callback to the list of callbacks.
     * @param event_name The name of the event that the callback will be fired on.
     * @param callback The callback function to add.
     */
    void addEventCallback(const char* event_name,
        void (*callback)(RoomDevice* self,
        const ParsedEvent_t* data));

    static ParsedEvent_t* getScratchSpace();

    static char* writeStringToScratchSpace(const char* string, ParsedEvent_t* scratchSpace);

    static void sendEvent(ParsedEvent_t* event) ;

public:
    /**
     * Called by the RoomInterface to process an event. It is the responsibility of this method to verify
     * that the event actually exists and to call the appropriate callback.
     * @param event The event to process.
     * @param data JSON data associated with the event. (Array of key-value pairs)
     */
    void processEvent(const char* event, const ParsedEvent_t* data);

    virtual const char* getObjectName() const {
        return nullptr;
    }

    virtual const char* getObjectType() const {
        return nullptr;
    }

    virtual char* getObjectName() {
        return nullptr;
    }

    virtual char* getObjectType() {
        return nullptr;
    }

    RoomDevice();

    /**
     * This function is called by the RoomInterface to get the device data object, and update it with the latest
     * data
     * @return The device data object.
     */
    virtual JsonVariant getDeviceData();

    virtual void startTask(TaskHandle_t* taskHandle);

    [[noreturn]] static void RTOSLoop(void *pvParameters) {
        // This is a placeholder for the RTOS loop that will be implemented in the subclass.
        for (;;) {
            vTaskDelay(1000);
        }
    }

    void uplinkNow();

};



#endif //ROOMDEVICE_H
