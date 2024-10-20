//
// Created by Jay on 10/12/2024.
//

#include "RoomDevice.h"

class RoomInterface;
extern RoomInterface MainRoomInterface;

ParsedEvent_t *RoomDevice::getScratchSpace() {
    return MainRoomInterface.get_free_scratch_space();
}

char* RoomDevice::writeStringToScratchSpace(const char *string, ParsedEvent_t *scratchSpace) {
    return RoomInterface::write_string_to_scratch_space(string, scratchSpace);
}

void RoomDevice::sendEvent(ParsedEvent_t *data) {
    MainRoomInterface.sendEvent(data);
}

void RoomDevice::uplinkNow() {
    MainRoomInterface.uplinkNow(this->getObjectName());
}

// const char RoomDevice::object_type[] = "RoomDevice";

RoomDevice::RoomDevice() {
    MainRoomInterface.addDevice(this);
    // deviceData["name"] = nullptr;
    deviceData["data"] = JsonObject();
    deviceData["data"] = JsonObject();
    deviceData["info"] = JsonObject();
    deviceData["health"] = JsonObject();
    deviceData["type"] = nullptr;
}

JsonVariant RoomDevice::getDeviceData() {
    return deviceData;
}

void RoomDevice::startTask(TaskHandle_t *taskHandle) {
    return;
}


void RoomDevice::addEventCallback(const char* event_name, void (*callback)(RoomDevice* self,
                                                                           const ParsedEvent_t* data)) {
    auto* newCallback = new EventCallbackList();
    newCallback->callback = callback;
    newCallback->event_name = event_name;
    newCallback->next = eventCallbacks;
    eventCallbacks = newCallback;
}

void RoomDevice::processEvent(const char* event, const ParsedEvent_t* data) {
    const auto* current = eventCallbacks;
    while (current != nullptr) {
        if (strcmp(current->event_name, event) == 0) {
            // Serial.print("Processing Event: ");
            current->callback(this, data);
        }
        current = current->next;
    }
}
