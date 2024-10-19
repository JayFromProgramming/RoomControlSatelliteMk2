//
// Created by Jay on 10/15/2024.
//

#ifndef MOTIONDETECTOR_H
#define MOTIONDETECTOR_H

#include <ControllerInterface/RoomDevice.h>

#define MOTION_DETECTOR_PIN 17

class MotionDetector : public RoomDevice {

public:

    const char* object_name = "MotionDetector";
    const char* object_type = "MotionDetector";
    boolean motionDetected = false;
    time_t lastMotionTime = 0;

    const char* getObjectName() const override {
        return object_name;
    }

    const char* getObjectType() const override {
        return object_type;
    }

    MotionDetector();

    static void IRAM_ATTR pinISR();

    void startTask(TaskHandle_t* taskHandle) override;

    static void RTOSLoop(void* pvParameters);

    JsonVariant getDeviceData() override;

};



#endif //MOTIONDETECTOR_H
