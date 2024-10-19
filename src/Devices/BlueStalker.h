//
// Created by Jay on 10/18/2024.
//

#ifndef BLUESTALKER_H
#define BLUESTALKER_H
#include <ControllerInterface/RoomDevice.h>
#include <BLEDevice.h>
// #include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>


class BlueStalker : RoomDevice {

    class BLEAdvertisedDevice : public BLEAdvertisedDeviceCallbacks {

        void onResult(::BLEAdvertisedDevice advertisedDevice) override {
            Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
        }

    };

    const char* object_name = "BlueStalker";
    const char* object_type = "BlueStalker";

    // Target Device List to check if they are in range
    const char* targetDeviceList[2] = {"41:1e:8c:3c:b1:c5", nullptr};

    BLEScan* pBLEScan;

public:

    BlueStalker();

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



#endif //BLUESTALKER_H
