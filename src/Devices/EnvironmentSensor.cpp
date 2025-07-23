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

float_t EnvironmentSensor::celsiusToFahrenheit(float_t celsius_value) {
    return (celsius_value * (9.f / 5.f)) + 32;
}

[[noreturn]] void EnvironmentSensor::RTOSLoop(void* pvParameters) {
    auto* self = static_cast<EnvironmentSensor *>(pvParameters);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        if (self->aht20.available() && self->aht20.isConnected()) {
            const bool first_read = self->temperature == 0 && self->humidity == 0;
            self->temperature = celsiusToFahrenheit(self->aht20.getTemperature());
            self->humidity = self->aht20.getHumidity();
            self->has_data = true;
            if (first_read) self->uplinkNow();
            // Send the event to the RoomInterface
            const auto event = EnvironmentSensor::getScratchSpace();
            if (event == nullptr) {
                Serial.println("Failed to get scratch space for event");
                continue;
            }
            event->objectName = writeStringToScratchSpace(self->getObjectName(), event);
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

    deviceData["state"]["temperature"] = temperature;
    deviceData["state"]["humidity"]    = humidity;

    if (!this->aht20.isConnected()) {
        deviceData["health"]["online"] = false;
        deviceData["health"]["fault"] = true;
        deviceData["health"]["reason"] = "Sensor Not Connected";
    } else if (temperature != 0 && humidity != 0) {
        deviceData["health"]["online"] = true;
        deviceData["health"]["fault"] = false;
        deviceData["health"]["reason"] = "";
    } else {
        deviceData["health"]["online"] = false;
        deviceData["health"]["fault"] = true;
        deviceData["health"]["reason"] = "No Data";
    }
    return deviceData;
}
