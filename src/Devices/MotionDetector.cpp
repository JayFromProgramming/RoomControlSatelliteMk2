//
// Created by Jay on 10/15/2024.
//

#include "MotionDetector.h"

SemaphoreHandle_t motionEventMutex;

MotionDetector::MotionDetector() {
    Serial.println("Initializing Motion Detector");
    pinMode(MOTION_DETECTOR_PIN, INPUT_PULLDOWN);
    motionEventMutex = xSemaphoreCreateBinary();
    attachInterrupt(digitalPinToInterrupt(MOTION_DETECTOR_PIN), MotionDetector::pinISR, CHANGE);
    motionDetected = digitalRead(MOTION_DETECTOR_PIN);
}

void MotionDetector::pinISR() {
    xSemaphoreGiveFromISR(motionEventMutex, nullptr);
}

void MotionDetector::startTask(TaskHandle_t *taskHandle) {
    xTaskCreate(MotionDetector::RTOSLoop, "MotionDetector",
        4096, this, 1, taskHandle);
}

[[noreturn]] void MotionDetector::RTOSLoop(void* pvParameters) {
    auto* self = static_cast<MotionDetector *>(pvParameters);
    Serial.println("Motion Detector Loop Started");
    while (true) {
        if (xSemaphoreTake(motionEventMutex, portMAX_DELAY) == pdTRUE) {
            // Read the pin state to determine which edge triggered the interrupt
            self->motionDetected = digitalRead(MOTION_DETECTOR_PIN);
            Serial.printf("Motion Detected: %d\n", self->motionDetected);
            if (self->motionDetected) {
                // Set the last motion time to the current time from the RTC
                time(&self->lastMotionTime);
            }
            self->uplinkNow();
            // Send the event to the RoomInterface
            // const auto event = MotionDetector::getScratchSpace();
            // if (event == nullptr) {
            //     Serial.println("Failed to get scratch space for event");
            //     continue;
            // }
            // event->eventName = writeStringToScratchSpace("motion_detected", event);
            // event->numArgs = 1;
            // event->args[0].type = ParsedArg::BOOL;
            // event->args[0].value.boolVal = self->motionDetected;
            // MotionDetector::sendEvent(event);
        }
    }
}

JsonVariant MotionDetector::getDeviceData() {
    // deviceData["name"] = getObjectName();
    deviceData["type"] = getObjectType();
    deviceData["health"]["online"] = true;
    deviceData["health"]["fault"] = false;
    deviceData["health"]["reason"] = "";
    deviceData["data"]["motion_detected"] = motionDetected;
    deviceData["data"]["last_motion_time"] = lastMotionTime;
    return deviceData;
}



