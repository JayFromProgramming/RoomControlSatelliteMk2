//
// Created by Jay on 10/16/2024.
//

#ifndef ENVIRONMENTSENSOR_H
#define ENVIRONMENTSENSOR_H

#include <ControllerInterface/RoomDevice.h>
#include <AHT20.h>
#include <Wire.h>


class EnvironmentSensor : public RoomDevice {

public:

    const char* object_type = "EnvironmentSensor";
    const char* object_name = "LivingRoomSensor";

    float_t temperature = 0;
    float_t humidity = 0;

    AHT20 aht20;

    EnvironmentSensor();

    void startTask(TaskHandle_t* taskHandle) override;

    [[noreturn]] static void RTOSLoop(void* pvParameters);

    JsonVariant getDeviceData() override;

    char const *getObjectName() const override {
        return object_name;
    }

    char const *getObjectType() const override {
        return object_type;
    }

};



#endif //ENVIRONMENTSENSOR_H
