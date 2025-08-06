#include <Arduino.h>
#include <Devices/EnvironmentSensor.h>
#include <Devices/MotionDetector.h>
#include <Devices/Radiator.h>

#include "ControllerInterface/RoomInterface.h"
#include <esp_task_wdt.h>
#include <esp_partition.h>
#include "build_info.h"
#include "secrets.h"
// #include <Devices/BlueStalker.h>
// #include <esp32/rom/ets_sys.h>

// #define DEBUG 0

extern RoomInterface MainRoomInterface;
volatile uint32_t idle_tick_count = 0;
volatile uint32_t idle_count;

Radiator* radiator;
MotionDetector* motionDetector;
EnvironmentSensor* environmentSensor;

const char* task_state_to_string(const eTaskState state) {
    switch (state) {
        case eRunning: return "Running";
        case eReady: return "Ready";
        case eBlocked: return "Blocked";
        case eSuspended: return "Suspended";
        case eDeleted: return "Deleted";
        case eInvalid: return "Invalid";
        default: return "Unknown";
    }
}

/**
 * Print the current time in the format mm/dd/yyyy hh:mm:ss
 * @param buffer Target string buffer
 */
void current_time(char* buffer) {
    time_t now;
    tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buffer, 80, "%m/%d/%Y %H:%M:%S", &timeinfo);
}

[[noreturn]] void idle_task(void* pvParameters) {
    for (;;) {
        idle_tick_count++;
        vTaskDelay(1);
    }
}

void setup() {
    Serial.begin(115200); // Initialize serial communication at 115200 baud rate
    UpdateHandler::check_for_rollback();
    const auto  partition = esp_ota_get_running_partition();
    DEBUG_PRINT("Starting RoomDevice [%s] on %s - Partition: %s",
        BUILD_VERSION, BUILD_GIT_BRANCH, partition->label);
    // Set the time using the NTP protocol
    configTime(0, 0, "time.mtu.edu", "pool.ntp.org", "time.nist.gov");
    MainRoomInterface.setNetworkCredentials(WIFI_SSID, WIFI_PASSWORD,
        CENTRAL_HOST, CENTRAL_PORT);
    radiator          = new Radiator();
    motionDetector    = new MotionDetector();
    environmentSensor = new EnvironmentSensor();
    DEBUG_PRINT("Starting up all Tasks...");
    MainRoomInterface.begin(BUILD_GIT_BRANCH, BUILD_VERSION, BUILD_GIT_BRANCH);
    DEBUG_PRINT("Task startup complete.");
    esp_task_wdt_init(20, true);
    DEBUG_PRINT("Remaining Free Heap: %d bytes", esp_get_free_heap_size());
    xTaskCreate(idle_task, "idle_task", 1024, nullptr, 0, nullptr);
}

[[noreturn]] void loop() {
    TickType_t last_wake_time = xTaskGetTickCount();
    for (;;) {
        // Determine how many times the idle task has run since this task last ran
        idle_count = idle_tick_count;
        idle_tick_count = 0; // Reset the idle tick count
        vTaskDelayUntil(&last_wake_time, 1000 / portTICK_PERIOD_MS); // Delay for 1 second
    }
}