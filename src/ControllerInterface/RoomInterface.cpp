//
// Created by Jay on 10/12/2024.
//

#include "RoomInterface.h"

class RoomDevice;

// Instantiate the singleton instance of the RoomInterface
auto MainRoomInterface = RoomInterface();


void RoomInterface::startDeviceLoops() const {
    for (auto current = devices; current != nullptr; current = current->next) {
        Serial.print("Starting Task: ");
        Serial.println(current->device->getObjectName());
        current->device->startTask(&current->taskHandle);
    }
}

/**
 * Packages the device data into a json object and sends it to the CENTRAL server.
 * @note If the uplink_target_device is not null, only the device with the matching name will be sent.
 */
void RoomInterface::sendUplink() {
    auto payload = JsonDocument();
    const auto root = payload.to<JsonObject>();
    root["name"] = "RoomDevice";
    root["current_ip"] = WiFi.localIP().toString();
    root["objects"] = JsonObject();
    root["auth"] = "AAAAAA"; // Not needed anymore but I've left it in just in case.
    for (auto current = devices; current != nullptr; current = current->next) {
        if (uplink_target_device != nullptr && // If exclusive uplink is requested, only send the target device
            strcmp(current->device->getObjectName(), uplink_target_device) != 0) {
            continue;
        } // Otherwise, add all devices to the uplink.
        const auto deviceData = current->device->getDeviceData();
        root["objects"][current->device->getObjectName()] = deviceData;
    }
    Serial.printf("Sending Uplink: %s : %d\n", uplink_target_device == nullptr ? "All" : uplink_target_device,
        root["objects"].size());
    if (uplink_target_device == nullptr) last_full_send = millis(); // Update the last full send time
    uplink_target_device = nullptr; // Reset the exclusive uplink target device
    // Serialize the json data.
    char buffer[1024] = {0};
    xSemaphoreTake(uplinkData->mutex, portMAX_DELAY); // Lock the uplink data mutex to write the payload
    memset(uplinkData->payload, 0, 1024); // Clear the uplink buffer
    const auto serialized = serializeJson(payload, &buffer, sizeof(buffer));
    memcpy(uplinkData->payload, buffer, serialized); // Copy the serialized data to the uplink buffer
    uplinkData->length = serialized;
    xSemaphoreGive(uplinkData->mutex); // Unlock the uplink data mutex
    // Queue the message to be sent to CENTRAL
    networkInterface->queue_message(NetworkInterface::UPLINK, buffer, serialized);
}

/**
 * Called when a device changes it's data and wants to send an uplink to the CENTRAL server immediately.
 * @param target_device The name of the device to send the uplink to. If null, nothing happens and this was pointless.
 */
void RoomInterface::uplinkNow(char* target_device) {
    auto result = xSemaphoreTake(exclusive_uplink_mutex, 50);
    if (result != pdTRUE) {
        Serial.println("Failed to take exclusive uplink mutex");
        return;
    }
    if (uplink_target_device != nullptr) { // The uplink task is still processing a previous exclusive uplink
        xSemaphoreGive(exclusive_uplink_mutex); // Release the mutex and abort the uplink
        Serial.println("Failed to send exclusive uplink, previous uplink still processing");
        return;
    }
    uplink_target_device = target_device;
    xSemaphoreGive(uplinkSemaphore);
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
    Serial.println("Starting Room Interface Loop");
    auto* roomInterface = static_cast<RoomInterface*>(pvParameters);
    // roomInterface->lastWakeTime = xTaskGetTickCount();
    roomInterface->startDeviceLoops();
    while (true) {
        roomInterface->sendUplink();
        // This will either block until the semaphore is given or timeout after the loopInterval and send the uplink.
        if (millis() - roomInterface->last_full_send > 30000) roomInterface->sendUplink();
        xSemaphoreTake(roomInterface->uplinkSemaphore, roomInterface->loopInterval);
    }
}

/**
 * This method is the main task entry point for the Event Parsing and Execution task.
 * This task is responsible for parsing the incoming downlink messages and executing the events on subscribed devices.
 * @core 0/1
 * @param pvParameters The RoomInterface instance.
 */
[[noreturn]] void RoomInterface::eventLoop(void *pvParameters) {
    Serial.println("Starting Event Loop");
    auto* roomInterface = static_cast<RoomInterface*>(pvParameters);
    while (true) {
        // Check the downlink queue for new events.
        NetworkInterface::downlink_message_t message;
        if (xQueueReceive(roomInterface->networkInterface->downlink_queue, &message, 100) == pdTRUE) {
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
    auto document = JsonDocument();
    const auto root = document.to<JsonObject>();
    root["name"] = "RoomDevice";
    root["object"] = event->objectName;
    root["event"] = event->eventName;
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
    char buffer[512] = {0};
    const auto serialized = serializeJson(document, &buffer, sizeof(buffer));
    // Queue the message to be sent to CENTRAL
    networkInterface->queue_message(NetworkInterface::EVENT, buffer, serialized);
    memset(event, 0, sizeof(ParsedEvent_t));
    event->finished = true;
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
    auto* working_space = get_free_scratch_space();
    if (working_space == nullptr) {
        Serial.println("Failed to get scratch space for event");
        return nullptr;
    }
    working_space->finished = false;
    // Parse the json data and fill the working space.
    event_document.clear();
    const DeserializationError error = deserializeJson(event_document, data);
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.c_str());
        return nullptr;
    }
    const auto root = event_document.as<JsonObject>();
    char* object_ptr = write_string_to_scratch_space(root["object"], working_space);
    char* event_ptr = write_string_to_scratch_space(root["event"], working_space);
    working_space->objectName = object_ptr;
    working_space->eventName = event_ptr;
    // Parse the args array.
    const auto args = root["args"];
    for (int i = 0; i < args.size(); i++) {
        auto arg = args[i];
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
        }
        working_space->numArgs++;
    }
    return working_space;
}

void RoomInterface::eventExecute(ParsedEvent_t* event) const {
    Serial.printf("Executing Event: %s\n", event->eventName);
    for (auto current = devices; current != nullptr; current = current->next) {
        // Serial.printf("Checking Device: %s : %s\n", current->device->getObjectName(), event->objectName);
        if (strcmp(current->device->getObjectName(), event->objectName) == 0) {
            // Serial.printf("Sending Event to: %s\n", current->device->getObjectName());
            current->device->processEvent(event->eventName, event);
        }
    }
    // Clear the working space for the next event.
    memset(event, 0, sizeof(ParsedEvent_t));
    event->finished = true;
}


RoomInterface::TaskPile RoomInterface::getAllTaskHandles() const {
    auto pile = TaskPile();
    auto* taskHandles = new TaskHandle_t*[getDeviceCount() + 3];
    pile.names = new const char*[getDeviceCount() + 3];
    int i = 0;
    // Add the network task handle.
    taskHandles[i] = &system_tasks[0].handle;
    pile.names[i++] = "RoomInterface";
    taskHandles[i] = &system_tasks[1].handle;
    pile.names[i++] = "NetworkInterface";
    taskHandles[i] = &system_tasks[2].handle;
    pile.names[i++] = "EventLoop";
    for (auto current = devices; current != nullptr; current = current->next) {
        if (current->taskHandle == nullptr) {
            continue;
        }
        taskHandles[i] = &current->taskHandle;
        pile.names[i] = current->device->getObjectName();
        // pile.names[i] = "Unimplemented";
        i++;
    }
    pile.handles = taskHandles;
    pile.count = i;
    return pile;
}


