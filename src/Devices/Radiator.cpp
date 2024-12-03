//
// Created by Jay on 10/14/2024.
//

#include "Radiator.h"

__NOINIT_ATTR uint32_t radiator_state_preserver;

Radiator::Radiator() {
    Serial.println("Initializing Radiator");
    pinMode(RADIATOR_PIN, OUTPUT);
    if (radiator_state_preserver != PRESERVER_OFF && radiator_state_preserver != PRESERVER_ON) {
        Serial.println("Radiator state has been reset");
        radiator_state_preserver = PRESERVER_OFF; // This magic number indicates off
    }
    if (radiator_state_preserver == PRESERVER_OFF) {
        this->setOn(false);
    } else if (radiator_state_preserver == PRESERVER_ON) {
        this->setOn(true);
    }
    addEventCallback("set_on", [](RoomDevice* self, const ParsedEvent_t* data) {
        const auto radiator = static_cast<Radiator*>(self);
        radiator->setOn(data->args[0].value.boolVal);
    });
    addEventCallback("heartbeat", [](RoomDevice* self, const ParsedEvent_t* data) {
        const auto radiator = static_cast<Radiator*>(self);
        radiator->lastHeartbeat = xTaskGetTickCount();
    });
    addEventCallback("radiator_temp_update", [](RoomDevice* self, const ParsedEvent_t* data) {
        const auto radiator = static_cast<Radiator*>(self);
        radiator->updateRadiatorTemp(data->args[0].value.floatVal);
    });

}

void Radiator::startTask(TaskHandle_t* taskHandle) {
    xTaskCreate(Radiator::RTOSLoop,
        "Radiator", STACK_SIZE, this, PRIORITY, taskHandle);
}

[[noreturn]] void Radiator::RTOSLoop(void* pvParameters) {
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
        switch(self->state) {
            case CLOSING: // Radiator is closing the valve
                if (self->radiator_temp < self->temp_at_shutdown - 0.5)
                    self->state = COOLDOWN;
                if (self->radiator_temp <= RADIATOR_COOLDOWN_TEMP)
                    self->state = OFF;
                if (xTaskGetTickCount() - self->cooldown_start > RADIATOR_COOLDOWN_TIME)
                    self->state = SHUTDOWN_FAULT;
            break;
            case COOLDOWN:
                if (self->radiator_temp <= RADIATOR_COOLDOWN_TEMP)
                    self->state = OFF;
                // Check if the temperature is currently decreasing
                if (self->radiator_temp < self->last_radiator_temp)
                    self->cooldown_start = xTaskGetTickCount();
                if (xTaskGetTickCount() - self->cooldown_start > RADIATOR_COOLDOWN_TIME)
                    self->state = SHUTDOWN_FAULT;
            break;
            case OFF: // Radiator is off and cold (and should stay cold)
                if (self->radiator_temp > RADIATOR_COOLDOWN_TEMP + 5 && !self->on)
                    self->state = SHUTDOWN_FAULT;
                if (self->on) self->state = WARMUP; // Edge case check
                break;
            case OPENING:
                if (self->radiator_temp > self->temp_at_startup + 0.5)
                    self->state = WARMUP;
                if (self->radiator_temp >= RADIATOR_OPERATING_TEMP)
                    self->state = ON;
                if (xTaskGetTickCount() - self->warmup_start > RADIATOR_HEATUP_TIME)
                    self->state = STARTUP_FAULT;
                break;
            case WARMUP:
                if (self->radiator_temp >= RADIATOR_OPERATING_TEMP)
                    self->state = ON;
                // Check if the temperature is currently increasing
                if (self->radiator_temp > self->last_radiator_temp)
                    self->warmup_start = xTaskGetTickCount();
                if (self->radiator_temp < self->temp_at_startup - 2)
                    self->state = STARTUP_FAULT;
                if (xTaskGetTickCount() - self->warmup_start > RADIATOR_HEATUP_TIME)
                    self->state = STARTUP_FAULT;
                break;
            case ON: // Radiator is on and at operating temperature
                if (self->radiator_temp < RADIATOR_OPERATING_TEMP - 5 && self->on)
                    self->state = STARTUP_FAULT;
                if (!self->on) self->state = COOLDOWN; // Edge case check
                break;
            case STARTUP_FAULT:
                if (self->radiator_temp > RADIATOR_OPERATING_TEMP)
                    self->state = ON;
                if (!self->on) self->state = OFF;
                break;
            case SHUTDOWN_FAULT:
                if (self->radiator_temp < RADIATOR_COOLDOWN_TEMP)
                    self->state = OFF;
                if (self->on) self->state = ON;
                break;

        }
        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}

void Radiator::setOn(const boolean on) {
    // Determine the new radiator state
    switch (state) { // The state before action
        case OFF:
        case CLOSING:
        case COOLDOWN:
            if (on) {
                state = OPENING;
                this->temp_at_startup = radiator_temp;
                warmup_start = xTaskGetTickCount();
                radiator_state_preserver = PRESERVER_ON;
            }
        break;
        case ON:
        case OPENING:
        case WARMUP:
            if (!on) {
                state = CLOSING;
                this->temp_at_shutdown = radiator_temp;
                cooldown_start = xTaskGetTickCount();
                radiator_state_preserver = PRESERVER_OFF;
            }
        break;
        case STARTUP_FAULT:
        case SHUTDOWN_FAULT:
            if (on)  state = OPENING;
            if (on)  warmup_start = xTaskGetTickCount();
            // if (on)  state = ON;
            if (!on) state = CLOSING;
            if (!on) cooldown_start = xTaskGetTickCount();
    }
    this->on = on;
    Serial.printf("Radiator has been set %s\n", on ? "on" : "off");
    digitalWrite(RADIATOR_PIN, on ? LOW : HIGH);
    uplinkNow(); // Force a transmission of the device data.
}

void Radiator::updateRadiatorTemp(const float temp) {
    // This function is called by the environment sensor to update the radiator temperature.
    // This is used to determine if the radiator should be turned on or off.
    this->last_radiator_temp = radiator_temp;
    if (isnan(radiator_temp)) {
        radiator_temp = temp;
        return;
    }
    if (40 < temp && temp < 120) radiator_temp = temp;
}

const char* Radiator::getStateString() const {
    switch(state) {
        case OFF:                   return "IDLE";
        case WARMUP:                return "WARMUP";
        case ON:                    return "ACTIVE";
        case COOLDOWN:              return "COOLDOWN";
        case STARTUP_FAULT:         return "STARTUP FAULT";
        case SHUTDOWN_FAULT:        return "SHUTDOWN FAULT";
        case OPENING:               return "OPENING VALVE";
        case CLOSING:               return "CLOSING VALVE";
    }
    return "UNKNOWN";
}

JsonVariant Radiator::getDeviceData(){
    // Update the device data with the current state of the device.
    // deviceData["name"] = getObjectName();
    deviceData["type"] = getObjectType();
    deviceData["data"]["on"] = on;
    deviceData["data"]["radiator_temp"] = radiator_temp;
    deviceData["data"]["state"] = getStateString();
    deviceData["health"]["online"] = true;
    // If the heartbeat is expired, set fault and reason.
    deviceData["health"]["fault"] = heartbeat_expired;
    deviceData["health"]["reason"] = heartbeat_expired ? "Heartbeat expired" : "";
    return deviceData;
}

