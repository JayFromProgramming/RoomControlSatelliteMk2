// //
// // Created by Jay on 10/18/2024.
// //
//
// #include "BlueStalker.h"
//
//
// BlueStalker::BlueStalker() {
//     BLEDevice::init("");
//     pBLEScan = BLEDevice::getScan();
//     pBLEScan->setAdvertisedDeviceCallbacks(new BLEAdvertisedDevice());
//     pBLEScan->setActiveScan(true);
//     pBLEScan->setInterval(100);
//     pBLEScan->setWindow(99);
// }
//
// void BlueStalker::startTask(TaskHandle_t* taskHandle) {
//     xTaskCreate(BlueStalker::RTOSLoop,
//         "BlueStalker", STACK_SIZE * 5, this, PRIORITY, taskHandle);
// }
//
// void BlueStalker::RTOSLoop(void* pvParameters) {
//     auto* self = static_cast<BlueStalker *>(pvParameters);
//     TickType_t xLastWakeTime = xTaskGetTickCount();
//     for (;;) {
//         BLEScanResults foundDevices = self->pBLEScan->start(5, false);
//         for (int i = 0; i < foundDevices.getCount(); i++) {
//             // BLEAdvertisedDevice device = foundDevices.getDevice(i);
//             // for (int j = 0; self->targetDeviceList[j] != nullptr; j++) {
//             //     if (device.getAddress().toString() == self->targetDeviceList[j]) {
//             //         // Send the event to the RoomInterface
//             //         const auto event = BlueStalker::getScratchSpace();
//             //         event->eventName = writeStringToScratchSpace("device_in_range", event);
//             //         event->numArgs = 1;
//             //         event->args[0].type = ParsedArg::STRING;
//             //         event->args[0].value.stringVal = writeStringToScratchSpace(device.getAddress().toString().c_str(), event);
//             //         BlueStalker::sendEvent(event);
//             //     }
//             // }
//         }
//         xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(15000));
//     }
// }
//
// JsonVariant BlueStalker::getDeviceData() {
//     // deviceData["name"] = getObjectName();
//     deviceData["data"]["state"]["devices_in_range"] = 0;
//     deviceData["type"] = getObjectType();
//     return deviceData;
// }