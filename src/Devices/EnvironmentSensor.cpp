//
// Created by Jay on 10/16/2024.
//

#include "EnvironmentSensor.h"


EnvironmentSensor::EnvironmentSensor() {
    Wire.begin();
    aht20.begin();
}

void EnvironmentSensor::startTask(TaskHandle_t* taskHandle) {
    xTaskCreate(EnvironmentSensor::RTOSLoop,
        "EnvironmentSensor", STACK_SIZE, this, PRIORITY, taskHandle);
}

void EnvironmentSensor::RTOSLoop(void* pvParameters) {
    auto* self = static_cast<EnvironmentSensor *>(pvParameters);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        if (self->aht20.available()) {
            self->temperature = self->aht20.getTemperature();
            self->humidity = self->aht20.getHumidity();
            // Send the event to the RoomInterface
            const auto event = EnvironmentSensor::getScratchSpace();
            event->eventName = writeStringToScratchSpace("environment_data_updated", event);
            event->numArgs = 2;
            event->args[0].type = ParsedArg::FLOAT;
            event->args[0].value.floatVal = self->temperature;
            event->args[1].type = ParsedArg::FLOAT;
            event->args[1].value.floatVal = self->humidity;
            EnvironmentSensor::sendEvent(event);
        }
        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(15000));
    }
}

JsonVariant EnvironmentSensor::getDeviceData() {
    deviceData["type"] = getObjectType();
    deviceData["data"]["state"]["temperature"] = temperature;
    deviceData["data"]["state"]["humidity"] = humidity;
    deviceData["health"]["online"] = aht20.available();
    return deviceData;
}
