//
// Created by Jay on 10/12/2024.
//

#include "RoomInterface.h"
#include "build_info.h"
class RoomDevice;

// Instantiate the singleton instance of the RoomInterface
auto MainRoomInterface = RoomInterface();

size_t RoomInterface::getDeviceInfo(char* buffer) const {
    auto payload = JsonDocument();
    const auto root = payload.to<JsonObject>();
    root["name"] = "TestingDevice"; // Placeholder for the device name
    root["version"] = BUILD_VERSION; // Use the build version from the build_info.h
    root["msg_type"] = "device_info"; // This is a device info message
    root["sub_device_count"] = getDeviceCount();
    root["sub_devices"] = JsonObject();
    size_t index = 0;
    for (auto current = devices; current != nullptr; current = current->next) {
        root["sub_devices"][current->device->getObjectName()] = current->device->getObjectType();
    }
    DEBUG_PRINT("Created device info payload with %d sub devices", root["sub_devices"].size());
    // Serialize the json data into the buffer.
    memset(buffer, 0, 1024); // Clear the buffer
    const auto serialized = serializeJson(payload, buffer, 1024);
    if (serialized == 0) {
        DEBUG_PRINT("Failed to serialize device info payload");
        return 0; // Return 0 on failure
    }
    DEBUG_PRINT("Serialized device info payload: %s", buffer);
    return serialized; // Return the size of the serialized data
}

void RoomInterface::startDeviceLoops() const {
    for (auto current = devices; current != nullptr; current = current->next) {
        DEBUG_PRINT("Starting Task: %s", current->device->getObjectName());
        current->device->startTask(&current->taskHandle);
    }
}

/**
 * Packages the device data into a json object and sends it to the CENTRAL server.
 * @note If the downlink_target_device is not null, only the device with the matching name will be sent.
 */
void RoomInterface::sendDownlink() {
    auto payload = JsonDocument();
    const auto root = payload.to<JsonObject>();
    root["current_ip"] = WiFi.localIP().toString();
    root["objects"] = JsonObject();
    root["msg_type"] = "state_update"; // This is a downlink message
    for (auto current = devices; current != nullptr; current = current->next) {
        if (downlink_target_device != nullptr && // If exclusive downlink is requested, only send the target device
            strcmp(current->device->getObjectName(), downlink_target_device) != 0) {
            continue;
        } // Otherwise, add all devices to the downlink.
        const auto deviceData = current->device->getDeviceData();
        root["objects"][current->device->getObjectName()] = deviceData;
    }
    DEBUG_PRINT("Sending downlink: %s : %d", downlink_target_device == nullptr ? "All" : downlink_target_device,
        root["objects"].size());
    if (downlink_target_device == nullptr) last_full_send = millis(); // Update the last full send time
    downlink_target_device = nullptr; // Reset the exclusive downlink target device
    // Serialize the json data.
    char buffer[4096] = {0};
    const auto serialized = serializeJson(payload, &buffer, sizeof(buffer));
    memcpy(uplink_buffer, buffer, serialized); // Copy the serialized data to the uplink buffer
    // Queue the message to be sent to CENTRAL
    networkInterface->queue_message(buffer, serialized);
}

/**
 * Called when a device changes it's data and wants to send an uplink to the CENTRAL server immediately.
 * @param target_device The name of the device to send the uplink to. If null, nothing happens and this was pointless.
 */
void RoomInterface::downlinkNow(char* target_device) {
    const auto result = xSemaphoreTake(exclusive_uplink_mutex, 50);
    if (result != pdTRUE) {
        DEBUG_PRINT("Failed to take exclusive uplink mutex");
        return;
    }
    if (downlink_target_device != nullptr) { // The uplink task is still processing a previous exclusive uplink
        xSemaphoreGive(exclusive_uplink_mutex); // Release the mutex and abort the uplink
        DEBUG_PRINT("Failed to send exclusive uplink, previous uplink still processing");
        return;
    }
    downlink_target_device = target_device;
    xSemaphoreGive(downlinkSemaphore);
    xSemaphoreGive(exclusive_uplink_mutex);
}


/**
 * This method is the main task entry point for the RoomInterface class.
 * This runs on Core 1 and is responsible for sending the uplink data to the CENTRAL server either every loopInterval
 * or when the uplinkSemaphore is given.
 * Additionally, this task is responsible for starting the device tasks.
 * @core 1
 * @param pvParameters A pointer to the RoomInterface instance.
 * @noreturn
 */
[[noreturn]] void RoomInterface::interfaceLoop(void *pvParameters) {
    DEBUG_PRINT("Starting Room Interface Loop");
    auto* roomInterface = static_cast<RoomInterface*>(pvParameters);
    // roomInterface->lastWakeTime = xTaskGetTickCount();
    roomInterface->startDeviceLoops();
    while (true) {
        roomInterface->sendDownlink();
        // This will either block until the semaphore is given or timeout after the loopInterval and send the uplink.
        if (millis() - roomInterface->last_full_send > 15000) roomInterface->sendDownlink();
        xSemaphoreTake(roomInterface->downlinkSemaphore, roomInterface->loopInterval);
    }
}

/**
 * This method is the main task entry point for the Event Parsing and Execution task.
 * This task is responsible for parsing the incoming uplink messages and executing the events on subscribed devices.
 * @core 0/1
 * @param pvParameters The RoomInterface instance.
 */
[[noreturn]] void RoomInterface::eventLoop(void *pvParameters) {
    DEBUG_PRINT("Starting Event Loop");
    auto* roomInterface = static_cast<RoomInterface*>(pvParameters);
    while (true) {
        // Check the uplink queue for new events.
        NetworkInterface::uplink_message_t message;
        if (xQueueReceive(roomInterface->networkInterface->uplink_queue, &message, 100) == pdTRUE) {
            DEBUG_PRINT("Received Event: %s", message.data);
            // Parse the event data and execute the event.
            auto* parsed = roomInterface->eventParse(message.data);
            if (parsed != nullptr) roomInterface->eventExecute(parsed);
        }
        esp_task_wdt_reset();
    }
}

/**
 * Send an event to the CENTRAL server.
 * @param event A pointer to a parsed event structure from the working space already filled
 */
void RoomInterface::sendEvent(ParsedEvent_t* event) const {
    // return;
    auto document = event->document;
    const auto root = document.to<JsonObject>();
    root["object"] = event->objectName;
    root["event"] = event->eventName;
    root["msg_type"] = "event"; // This is an event message
    root["args"] = JsonArray();
    for (int i = 0; i < event->numArgs; i++) {
        switch (event->args[i].type) {
            case ParsedArg::BOOL:
                root["args"].add(event->args[i].value.boolVal);
                break;
            case ParsedArg::INT:
                root["args"].add(event->args[i].value.intVal);
                break;
            case ParsedArg::FLOAT:
                root["args"].add(event->args[i].value.floatVal);
                break;
            case ParsedArg::STRING:
                root["args"].add(event->args[i].value.stringVal);
                break;
            default: break;
        }
    }
    // Implement the kwargs object later.
    root["kwargs"] = JsonObject();
    // Serialize the json data.
    char buffer[4096] = {0};
    const auto serialized = serializeJson(document, &buffer, sizeof(buffer));
    // Queue the message to be sent to CENTRAL
    networkInterface->queue_message(buffer, serialized);
    cleanup_scratch_space(event);
}

/**
 * Example json data:
 * {
 *  "object": "device_name",
 *  "event": "event_name",
 *  "args": [],
 *  "kwargs": {}
 * }
 * @param data The json data to parse.
 * @return
 */
ParsedEvent_t* RoomInterface::eventParse(const char* data) {
    // Serial.println("Parsing Event");
    this->last_event_parse = xTaskGetTickCount();
    auto* working_space = get_free_scratch_space();
    if (working_space == nullptr) {
        DEBUG_PRINT("Failed to get scratch space for event");
        return nullptr;
    }
    working_space->finished = false;
    // Parse the json data and fill the working space.
    event_document.clear();
    const DeserializationError error = deserializeJson(event_document, data);
    if (error) {
        DEBUG_PRINT("deserializeJson() failed: %s", error.c_str());
        return nullptr;
    }
    const auto root = event_document.as<JsonObject>();
    char* object_ptr = write_string_to_scratch_space(root["sub_device_id"], working_space);
    char* event_ptr = write_string_to_scratch_space(root["event_name"], working_space);
    working_space->objectName = object_ptr;
    working_space->eventName = event_ptr;
    // Parse the args array.
    const auto args = root["args"].as<JsonArray>();
    for (const auto & i : args) {
        auto arg = i.as<JsonVariant>();
        if (arg.is<bool>()) {
            working_space->args[working_space->numArgs].value.boolVal = arg.as<bool>();
            working_space->args[working_space->numArgs].type = ParsedArg::BOOL;
        } else if (arg.is<int>()) {
            working_space->args[working_space->numArgs].value.intVal = arg.as<int>();
            working_space->args[working_space->numArgs].type = ParsedArg::INT;
        } else if (arg.is<float>()) {
            working_space->args[working_space->numArgs].value.floatVal = arg.as<float>();
            working_space->args[working_space->numArgs].type = ParsedArg::FLOAT;
        } else if (arg.is<const char*>()) {
            working_space->args[working_space->numArgs].value.stringVal =
                write_string_to_scratch_space(arg.as<const char*>(), working_space);
            working_space->args[working_space->numArgs].type = ParsedArg::STRING;
        } else {
            DEBUG_PRINT("Unknown arg type in event, aborting");
            return nullptr;
        }
        working_space->numArgs++;
    }
    return working_space;
}

void RoomInterface::eventExecute(ParsedEvent_t* event) const {
    DEBUG_PRINT("Executing Event: %s", event->eventName);
    for (auto current = devices; current != nullptr; current = current->next) {
        // Serial.printf("Checking Device: %s : %s\n", current->device->getObjectName(), event->objectName);
        if (strcmp(current->device->getObjectName(), event->objectName) == 0) {
            // Serial.printf("Sending Event to: %s\n", current->device->getObjectName());
            current->device->processEvent(event->eventName, event);
        }
    }
    // Clear the working space for the next event.
    cleanup_scratch_space(event);
    event->finished = true;
}

[[noreturn]] void RoomInterface::interfaceHealthCheck(void* pvParameters) {
    const auto* roomInterface = static_cast<RoomInterface*>(pvParameters);
    while (true) {
        // Check if the last event parse was more than 2 minutes ago.
        if (xTaskGetTickCount() - roomInterface->last_event_parse > 120000) {
            DEBUG_PRINT("Event Parse Timeout");
            esp_restart();
        }
        esp_task_wdt_reset();
        vTaskDelay(1000);
    }
}


RoomInterface::TaskPile RoomInterface::getAllTaskHandles() const {
    // auto pile = TaskPile();
    // auto* taskHandles = new TaskHandle_t*[getDeviceCount() + 3];
    // pile.names = new const char*[getDeviceCount() + 3];
    // int i = 0;
    // // Add the network task handle.
    // taskHandles[i] = &system_tasks[0].handle;
    // pile.names[i++] = "RoomInterface";
    // taskHandles[i] = &system_tasks[1].handle;
    // // pile.names[i++] = "NetworkInterface";
    // // taskHandles[i] = &system_tasks[2].handle;
    // pile.names[i++] = "EventLoop";
    // for (auto current = devices; current != nullptr; current = current->next) {
    //     if (current->taskHandle == nullptr) {
    //         continue;
    //     }
    //     taskHandles[i] = &current->taskHandle;
    //     pile.names[i] = current->device->getObjectName();
    //     // pile.names[i] = "Unimplemented";
    //     i++;
    // }
    // pile.handles = taskHandles;
    // pile.count = i;
    // return pile;
    return TaskPile();
}


