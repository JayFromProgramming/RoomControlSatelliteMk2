//
// Created by Jay on 10/16/2024.
//

#ifndef ENVIRONMENTSENSOR_H
#define ENVIRONMENTSENSOR_H

#include <ControllerInterface/RoomDevice.h>
#include <AM2302-Sensor.h>
#include <Wire.h>


class EnvironmentSensor : public RoomDevice {

public:

    const char* object_type = "EnvironmentSensor";
    const char* object_name = "RadiatorSensor";

    float_t temperature = 0;
    float_t humidity = 0;
    boolean has_data = false;

    AM2302::AM2302_Sensor am2302{19};

    EnvironmentSensor();

    void startTask(TaskHandle_t* taskHandle) override;

    static float_t celsiusToFahrenheit(float_t celsius);

    [[noreturn]] static void RTOSLoop(void* pvParameters);

    JsonVariant getDeviceData() override;

    char const *getObjectName() const override {
        return object_name;
    }

    char const *getObjectType() const override {
        return object_type;
    }

    char* getObjectName() override {
        return const_cast<char *>(object_name);
    }

};



#endif //ENVIRONMENTSENSOR_H
