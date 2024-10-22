//
// Created by Jay on 10/14/2024.
//

#include "Radiator.h"

Radiator::Radiator() {
    Serial.println("Initializing Radiator");
    pinMode(RADIATOR_PIN, OUTPUT);
    digitalWrite(RADIATOR_PIN, HIGH);
    addEventCallback("set_on", [](RoomDevice* self, const ParsedEvent_t* data) {
        const auto radiator = dynamic_cast<Radiator*>(self);
        radiator->setOn(data->args[0].value.boolVal);
    });
    addEventCallback("heartbeat", [](RoomDevice* self, const ParsedEvent_t* data) {
        const auto radiator = dynamic_cast<Radiator*>(self);
        radiator->lastHeartbeat = xTaskGetTickCount();
    });
}

void Radiator::startTask(TaskHandle_t* taskHandle) {
    xTaskCreate(Radiator::RTOSLoop,
        "Radiator", STACK_SIZE, this, PRIORITY, taskHandle);
}

void Radiator::RTOSLoop(void* pvParameters) {
    auto* self = static_cast<Radiator *>(pvParameters);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        if (self->on) {
            if (self->lastHeartbeat == 0) {
                self->lastHeartbeat = xTaskGetTickCount();
            }
            if (xTaskGetTickCount() - self->lastHeartbeat > RadiatorTimeout) {
                self->setOn(false);
                self->heartbeat_expired = true;
            }
        }
        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}

void Radiator::setOn(const boolean on) {
    this->on = on;
    Serial.printf("Radiator has been set %s\n", on ? "on" : "off");
    digitalWrite(RADIATOR_PIN, on ? LOW : HIGH);
    uplinkNow(); // Force a transmission of the device data.
}

JsonVariant Radiator::getDeviceData(){
    // Update the device data with the current state of the device.
    // deviceData["name"] = getObjectName();
    deviceData["type"] = getObjectType();
    deviceData["data"]["on"] = on;
    deviceData["health"]["online"] = true;
    // If the heartbeat is expired, set fault and reason.
    deviceData["health"]["fault"] = heartbeat_expired;
    deviceData["health"]["reason"] = heartbeat_expired ? "Heartbeat expired" : "";
    return deviceData;
}

