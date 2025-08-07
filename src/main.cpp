#include <Arduino.h>
#include <Devices/EnvironmentSensor.h>
#include <Devices/MotionDetector.h>
#include <Devices/Radiator.h>

#include "ControllerInterface/RoomInterface.h"
#include <esp_task_wdt.h>
#include <esp_partition.h>
#include <esp_freertos_hooks.h>
#include "build_info.h"
#include "secrets.h"

// #define DEBUG 0

extern RoomInterface MainRoomInterface;
volatile uint32_t idle_tick_count_app = 0;
volatile uint32_t idle_tick_count_pro = 0;
volatile float_t mcu_load = 0.0f;

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

bool pro_idle_task() {
    idle_tick_count_pro++;
    return true;
}

bool app_idle_task() {
    idle_tick_count_app++;
    return true;
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
    auto result = esp_register_freertos_idle_hook_for_cpu(&app_idle_task, APP_CPU_NUM);
    if (result != ESP_OK) {
        DEBUG_PRINT("Failed to register app idle hook: %s", esp_err_to_name(result));
    }
    result = esp_register_freertos_idle_hook_for_cpu(&pro_idle_task, PRO_CPU_NUM);
    if (result != ESP_OK) {
        DEBUG_PRINT("Failed to register pro idle hook: %s", esp_err_to_name(result));
    }

}

[[noreturn]] void loop() {
    TickType_t interval_tick = xTaskGetTickCount();
    TickType_t last_wake_tick = xTaskGetTickCount();
    for (;;) {
        // Determine how many times the idle task has run since this task last ran
        const auto ticks_since_last_run = xTaskGetTickCount() - last_wake_tick;
        if (ticks_since_last_run == 0) {
            vTaskDelay(1); // Prevent div by zero
            continue;
        }
        float_t app_load = (idle_tick_count_app * 100.0f) / static_cast<float_t>(ticks_since_last_run);
        float_t pro_load = (idle_tick_count_pro * 100.0f) / static_cast<float_t>(ticks_since_last_run);
        app_load = 100.0f - app_load; // Convert idle count to load percentage
        pro_load = 100.0f - pro_load; // Convert idle count to load

        // Calculate the idle count for each CPU
        idle_tick_count_app = 0;
        idle_tick_count_pro = 0;
        // Calculate the CPU load as a percentage
        mcu_load = ((app_load + pro_load) / 2.0f);
        DEBUG_PRINT("MCU Load: %.02f%% | App CPU Load: %.02f%% | Pro CPU Load: %.02f%%",
            mcu_load, app_load, pro_load);
        last_wake_tick = xTaskGetTickCount(); // Update the last wake tick
        vTaskDelayUntil(&interval_tick, 1000 / portTICK_PERIOD_MS); // Delay for 1 second

    }
}