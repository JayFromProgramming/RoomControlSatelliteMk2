//
// Created by Jay on 10/14/2024.
//

#ifndef RADIATOR_H
#define RADIATOR_H
#include <RoomDevice.h>

#define RADIATOR_PIN 5
#define RadiatorTimeout 120000  // Disable the radiator after 2 minutes no server heartbeat

#define RADIATOR_HEATUP_TIME (180000 * portTICK_PERIOD_MS)   // 4 minutes
#define RADIATOR_COOLDOWN_TIME (180000 * portTICK_PERIOD_MS) // 5 minutes

// Any temperature inbetween these two values is considered a transition state
#define RADIATOR_OPERATING_TEMP 83.0  // (F) if the radiator is above this temp, it is considered on
#define RADIATOR_COOLDOWN_TEMP  72.0  // (F) if the radiator is below this temp, it is considered off


#define PRESERVER_ON  0x4321
#define PRESERVER_OFF 0x1234

class Radiator : RoomDevice {

    enum RadiatorState {
        OFF,            // Radiator is off and cold
        OPENING,        // Radiator is opening the valve
        WARMUP,         // Radiator is warming up to operating temperature
        ON,             // Radiator is on and at operating temperature
        CLOSING,        // Radiator is closing the valve
        COOLDOWN,       // Radiator is cooling down
        STARTUP_FAULT,  // Radiator is on but failed to reach operating temperature within the time window
        SHUTDOWN_FAULT  // Radiator has been commanded off but is still at operating temperature
    };

    RadiatorState state = COOLDOWN;
    uint32_t lastHeartbeat = 0;
    uint32_t warmup_start = 0;
    uint32_t cooldown_start = 0;
    float_t  radiator_temp = NAN;
    float_t  last_radiator_temp = NAN;
    float_t  temp_at_startup = NAN;
    float_t  temp_at_shutdown = NAN;
    boolean heartbeat_expired = false;

public:

    const char* object_name = "Radiator";
    const char* object_type = "Radiator";
    boolean on = false;

    char* getObjectName() override {
        return const_cast<char *>(object_name);
    }

    char* getObjectType() override {
        return const_cast<char *>(object_type);
    }

    Radiator();

    void setOn(boolean on);

    void updateRadiatorTemp(float temp);

    const char *getStateString() const;

    void startTask(TaskHandle_t* taskHandle) override;

    [[noreturn]] static void RTOSLoop(void* pvParameters);

    JsonVariant getDeviceData() override;

};



#endif //RADIATOR_H
