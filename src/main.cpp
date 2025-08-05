#include <Arduino.h>
#include <Devices/EnvironmentSensor.h>
#include <Devices/MotionDetector.h>
#include <Devices/Radiator.h>

#include "ControllerInterface/RoomInterface.h"
// #include "Devices/Radiator.h"
// #include <esp_system.h>
#include <esp_task_wdt.h>

#include "build_info.h"
// #include <Devices/BlueStalker.h>
// #include <esp32/rom/ets_sys.h>

// #define DEBUG 0

extern RoomInterface MainRoomInterface;

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

const char* wifi_status_to_string(const wl_status_t status) {
    switch (status) {
        case WL_NO_SHIELD: return "No Shield";
        case WL_IDLE_STATUS: return "Idle";
        case WL_NO_SSID_AVAIL: return "No SSID Available";
        case WL_SCAN_COMPLETED: return "Scan Completed";
        case WL_CONNECTED: return "Connected";
        case WL_CONNECT_FAILED: return "Connect Failed";
        case WL_CONNECTION_LOST: return "Connection Lost";
        case WL_DISCONNECTED: return "Disconnected";
        default: return "Unknown";
    }
}

const char* ota_state_to_string(const esp_ota_img_states_t state) {
    switch (state) {
        case ESP_OTA_IMG_NEW: return "ESP_OTA_IMG_NEW";
        case ESP_OTA_IMG_UNDEFINED: return "ESP_OTA_IMG_UNDEFINED";
        case ESP_OTA_IMG_VALID: return "ESP_OTA_IMG_VALID";
        case ESP_OTA_IMG_INVALID: return "ESP_OTA_IMG_INVALID";
        case ESP_OTA_IMG_PENDING_VERIFY: return "ESP_OTA_IMG_PENDING_VERIFY";
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

void connect_wifi() {
    DEBUG_PRINT("Starting WiFi...");
    WiFi.mode(WIFI_MODE_STA);  // Setup wifi to connect to an access point
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Pass the SSID and Password to the WiFi.begin function
    WiFi.setAutoReconnect(true); // Enable auto reconnect
    WiFi.setHostname("RoomDevice"); // Set the hostname of the device (doesn't seem to work)
    DEBUG_PRINT("Wi-FI MAC Address: %s", WiFi.macAddress().c_str());
    DEBUG_PRINT("Attempting to connect to WiFi SSID: \"%s\" with Password: \"%s\"", WIFI_SSID, WIFI_PASSWORD);
    auto wifi_status = WiFi.waitForConnectResult();
    if (wifi_status != WL_CONNECTED) {
        DEBUG_PRINT("WiFi failed to connect [%s], attempting again...", wifi_status_to_string(static_cast<wl_status_t>(wifi_status)));
        vTaskDelay(5000);
        connect_wifi(); // Retry connecting to WiFi
        return;
    }
    DEBUG_PRINT("WiFi connected [%s] with IP: %s",
        wifi_status_to_string(static_cast<wl_status_t>(wifi_status)),
        WiFi.localIP().toString().c_str());

}

void check_partition_states() {
    // Print the state of both OTA partitions
    const auto partition = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(partition, &ota_state);
    DEBUG_PRINT("Running Partition: %s - State: %d (%s)",
        partition->label, ota_state, ota_state_to_string(ota_state));
    const auto next_partition = esp_ota_get_next_update_partition(partition);
    esp_ota_get_state_partition(next_partition, &ota_state);
    DEBUG_PRINT("Next Partition: %s - State: %d (%s)",
        next_partition->label, ota_state, ota_state_to_string(ota_state));
}

void setup() {
    Serial.begin(115200); // Initialize serial communication at 115200 baud rate
    const auto  partition = esp_ota_get_running_partition();
    DEBUG_PRINT("Starting RoomDevice [%s] on %s - Partition: %s",
        BUILD_VERSION, BUILD_GIT_BRANCH, partition->label);
    check_partition_states();
    esp_ota_mark_app_invalid_rollback_and_reboot();
    check_partition_states();
    ledcSetup(LEDC_CHANNEL, LEDC_FREQUENCY_NO_WIFI, LEDC_TIMER);
    ledcAttachPin(ACTIVITY_LED, LEDC_CHANNEL);
    ledcWrite(LEDC_CHANNEL, 4096); // Turn off the activity LED initially
    connect_wifi();
    ledcDetachPin(ACTIVITY_LED); // Detach the LED pin after connecting to WiFi
    // Set the time using the NTP protocol
    configTime(0, 0, "time.mtu.edu", "pool.ntp.org", "time.nist.gov");
    radiator = new Radiator();
    motionDetector = new MotionDetector();
    environmentSensor = new EnvironmentSensor();
    // delay(1000);
    DEBUG_PRINT("Starting up all Tasks...");
    MainRoomInterface.begin(BUILD_GIT_BRANCH);
    DEBUG_PRINT("Task startup complete.");
    esp_task_wdt_init(20, true);
    DEBUG_PRINT("Remaining Free Heap: %d bytes", esp_get_free_heap_size());
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}