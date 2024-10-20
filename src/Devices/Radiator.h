//
// Created by Jay on 10/14/2024.
//

#ifndef RADIATOR_H
#define RADIATOR_H
#include <ControllerInterface/RoomDevice.h>

#define RADIATOR_PIN 5
#define RadiatorTimeout 120000  // Disable the radiator after 2 minutes no server heartbeat

class Radiator : RoomDevice {

    uint32_t lastHeartbeat = 0;
    boolean heartbeat_expired = false;

public:

    const char* object_name = "Radiator";
    const char* object_type = "Radiator";
    boolean on = false;

    const char* getObjectName() const override {
        return object_name;
    }

    const char* getObjectType() const override {
        return object_type;
    }

    char* getObjectName() override {
        return const_cast<char *>(object_name);
    }

    Radiator();

    void setOn(boolean on);

    void startTask(TaskHandle_t* taskHandle) override;

    [[noreturn]] static void RTOSLoop(void* pvParameters);

    JsonVariant getDeviceData() override;

};



#endif //RADIATOR_H
