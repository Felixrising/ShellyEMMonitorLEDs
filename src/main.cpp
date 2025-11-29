#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <LittleFS.h>
#include <ArduinoWebsockets.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <ESP32Ping.h>
#include <ArduinoOTA.h>
#include <ezTime.h>
#include <Preferences.h>
#include <algorithm>
#include <limits>
#include <map>
#include <vector>
#include <cstdint>
#include "includes.h"
#include <cstring>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using namespace websockets;

// NVS Preferences for uploadfs-survivable settings
Preferences preferences;
const char* NVS_NAMESPACE = "shemonitor";

// Timing and debug utilities
String timestamp() {
    unsigned long ms = millis();
    unsigned long days = ms / 86400000UL;
    ms %= 86400000UL;
    unsigned long hours = ms / 3600000UL;
    ms %= 3600000UL;
    unsigned long minutes = ms / 60000UL;
    ms %= 60000UL;
    unsigned long seconds = ms / 1000UL;
    unsigned long milliseconds = ms % 1000UL;
    char buf[20];
    sprintf(buf, "%03lu%02lu%02lu%02lu.%03lu", days, hours, minutes, seconds, milliseconds);
    return String(buf);
}

#define TIMED_PRINT(x) { Serial.print(timestamp()); Serial.print(" "); Serial.print(x); }
#define TIMED_PRINTLN(x) { Serial.print(timestamp()); Serial.print(" "); Serial.println(x); }

// Configuration constants
#define MAX_BRIGHTNESS 255
int globalBrightness = 32;

// Physical button for factory reset (Boot button on ESP32-C3)
#define BUTTON_PIN 9
#define FACTORY_RESET_HOLD_TIME 5000  // 5 seconds
unsigned long buttonPressStart = 0;
bool buttonWasPressed = false;
bool factoryResetTriggered = false;

#ifndef USE_WS2812B_FOR_STATUS
#define LED_STATUS_PIN 8
#endif

#ifdef USE_WS2812B_FOR_STATUS
#define LED_STATUS_PIN 7
#define LED_STATUS_COUNT 1
Adafruit_NeoPixel statusLED(LED_STATUS_COUNT, LED_STATUS_PIN, NEO_GRB + NEO_KHZ800);
#endif

// Network and connection management
unsigned long lastWebSocketAttempt = 0;
const long webSocketRetryInterval = 10000; // 10 seconds
const unsigned long WS_TIMEOUT = 30000; // 30 seconds
const unsigned long MDNS_QUERY_INTERVAL = 60000; // 1 minute
const unsigned long NETWORK_HEALTH_CHECK_INTERVAL = 30000; // 30 seconds
const unsigned long REQUEST_TIMEOUT = 10000; // 10 seconds

unsigned long lastWSActivity = 0;
unsigned long lastMDNSQuery = 0;
unsigned long lastNetworkHealthCheck = 0;

// System state
bool inSetup = true;
bool rpcInProgress = false;
bool newDataAvailable = false;
uint64_t deviceStartTimeMillis = 0;

// Configuration defaults
const char* defaultSSID = "";
const char* defaultPassword = "";
const char* defaultShellyIP = "";
const char* defaultTimezone = "Australia/Sydney";

String ShemeterName = "SheMonitor";
String fallbackSSID = ShemeterName + "AP";

char ssid[32] = "";
char password[64] = "";
char shellyIP[16] = "";
char timezone[64] = "Australia/Sydney";

// Time and uptime tracking
Timezone myTZ;
unsigned long startTime = 0;
bool timeInitialized = false;

// Timing
unsigned long previousMillis = 0;
const long dataUpdateInterval = 1000; // 1-second update frequency
const long loopDelay = 2;
unsigned long lastDataUpdateTime = 0;
int calculatedValue = 0;

// Energy meter data structure
struct EnergyMeter { 
    String name; 
    int act_power; 
    unsigned long lastUpdateTime; 
};
EnergyMeter meters[3] = { {"Grid", 0, 0}, {"Solar", 0, 0}, {"Consumer", 0, 0} };

// Shelly device support structure
struct ShellyDevice {
    String ip;
    String type; // "3EM" or "EM"
    String hostname;
    int channelOffset;
    bool isActive;
};
std::vector<ShellyDevice> shellyDevices;

// Data history
struct DataPoint {
    uint32_t timestamp;  // Unix epoch seconds
    int grid;
    int solar;
    int consumer;
};

const int SHORT_HISTORY_POINTS = 3600; // 1 hour @ 1-second resolution
DataPoint shortHistory[SHORT_HISTORY_POINTS];
int shortHistoryIndex = 0;
bool shortHistoryFilled = false;
uint32_t lastShortHistoryTimestamp = 0;

struct AggregatedPoint {
    uint32_t timestamp; // bucket start in epoch seconds
    int grid;
    int solar;
    int consumer;
};

const int HISTORY_24H_BUCKETS = 48;   // 30-minute buckets over 24 hours
const int HISTORY_30D_BUCKETS = 30;   // Daily buckets over 30 days
const uint32_t HISTORY_24H_BUCKET_SECONDS = 1800;
const uint32_t HISTORY_30D_BUCKET_SECONDS = 86400;

AggregatedPoint history24h[HISTORY_24H_BUCKETS];
int history24hIndex = 0;
bool history24hFilled = false;

AggregatedPoint history30d[HISTORY_30D_BUCKETS];
int history30dIndex = 0;
bool history30dFilled = false;

struct BucketState {
    uint32_t bucketDuration;
    uint32_t bucketStart;
    uint32_t sampleCount;
    int64_t gridSum;
    int64_t solarSum;
    int64_t consumerSum;
};

BucketState bucket24h = {HISTORY_24H_BUCKET_SECONDS, 0, 0, 0, 0, 0};
BucketState bucket30d = {HISTORY_30D_BUCKET_SECONDS, 0, 0, 0, 0, 0};

// JsonDocument sizing to avoid heap fragmentation
constexpr size_t JSON_CAPACITY_SMALL = 256;
constexpr size_t JSON_CAPACITY_MEDIUM = 1024;
constexpr size_t JSON_CAPACITY_CONFIG = 4096;
constexpr size_t JSON_CAPACITY_LARGE = 6144;
constexpr size_t JSON_CAPACITY_WIFI_SCAN = 4096;

// Persistent crash diagnostics
RTC_DATA_ATTR uint32_t bootCounter = 0;
RTC_DATA_ATTR uint32_t crashCounter = 0;
RTC_DATA_ATTR uint32_t lastResetReasonRaw = ESP_RST_POWERON;

// Log/metrics configuration
const char* LOG_DIRECTORY = "/logs";
const size_t MAX_LOG_FILES = 72; // approx 72 hours assuming 1 file per hour
const size_t LOG_FILE_SIZE_LIMIT = 50 * 1024; // 50 KB per log file
const unsigned long METRICS_LOG_INTERVAL = 60000;
const unsigned long LOW_MEMORY_LOG_INTERVAL = 300000;
const size_t LOW_MEMORY_THRESHOLD = 45000;
const char* HISTORY_DIRECTORY = "/history";
const char* HISTORY_24H_FILE = "/history/24h.bin";
const char* HISTORY_30D_FILE = "/history/30d.bin";
const uint32_t HISTORY_FILE_MAGIC = 0x53485231;
const uint16_t HISTORY_FILE_VERSION = 1;
const int SHELLY_PING_FAILURE_THRESHOLD = 3;

int shellyPingFailureCount = 0;
bool shellyRediscoveryNeeded = false;
unsigned long lastSuccessfulShellyPing = 0;

// Metrics tracking
bool littleFSReady = false;
unsigned long lastMetricsLog = 0;
unsigned long lastLowMemoryLog = 0;
size_t lowestHeapObserved = std::numeric_limits<size_t>::max();
size_t lowestLargestBlockObserved = std::numeric_limits<size_t>::max();
unsigned long maxLoopDurationMicros = 0;
unsigned long lastLoopDurationMicros = 0;

// Network components
WebServer server(80);
DNSServer dnsServer;
WebsocketsClient wsClient;
bool isAPMode = false;  // Track if we're in AP mode for captive portal

// RPC request management with timeout support
struct PendingRequest { 
    std::function<void(JsonObject&)> callback; 
    unsigned long timestamp;
};
std::map<int, PendingRequest> pendingRequests;
int commandId = 0;

// LED configuration variables
int LED_COUNT_var = 60;
int LED_PIN_var = 4;
uint32_t LED_TYPE_flags = NEO_GRBW + NEO_KHZ800;
bool invertStrip = false;

// LED Type definitions for common ICs
struct LEDType {
    const char* name;
    const char* description;
    uint32_t flags;
};

const LEDType LED_TYPES[] = {
    {"WS2812B", "WS2812B - RGB (800KHz, GRB order)", NEO_GRB + NEO_KHZ800},
    {"WS2812B_RGBW", "WS2812B - RGBW (800KHz, GRBW order)", NEO_GRBW + NEO_KHZ800},
    {"WS2811", "WS2811 - RGB (400KHz, RGB order)", NEO_RGB + NEO_KHZ400},
    {"WS2811_800", "WS2811 - RGB (800KHz, RGB order)", NEO_RGB + NEO_KHZ800},
    {"WS2813", "WS2813 - RGB (800KHz, GRB order)", NEO_GRB + NEO_KHZ800},
    {"SK6812", "SK6812 - RGB (800KHz, GRB order)", NEO_GRB + NEO_KHZ800},
    {"SK6812_RGBW", "SK6812 - RGBW (800KHz, GRBW order)", NEO_GRBW + NEO_KHZ800},
    {"APA102", "APA102/DotStar - RGB (GRB order)", NEO_GRB + NEO_KHZ800},
    {"CUSTOM_RGB", "Custom RGB (800KHz, RGB order)", NEO_RGB + NEO_KHZ800},
    {"CUSTOM_BGR", "Custom BGR (800KHz, BGR order)", NEO_BGR + NEO_KHZ800},
    {"CUSTOM_BRG", "Custom BRG (800KHz, BRG order)", NEO_BRG + NEO_KHZ800},
    {"CUSTOM_GBR", "Custom GBR (800KHz, GBR order)", NEO_GBR + NEO_KHZ800},
    {"CUSTOM_RBG", "Custom RBG (800KHz, RBG order)", NEO_RBG + NEO_KHZ800}
};

const int LED_TYPES_COUNT = sizeof(LED_TYPES) / sizeof(LED_TYPES[0]);

// Current LED type index
int currentLEDTypeIndex = 0;

// Timezone definitions for dropdown
struct TimezoneInfo {
    const char* name;
    const char* olson;
};

const TimezoneInfo TIMEZONES[] = {
    {"UTC", "UTC"},
    {"GMT", "GMT"},
    {"US/Eastern", "America/New_York"},
    {"US/Central", "America/Chicago"},
    {"US/Mountain", "America/Denver"},
    {"US/Pacific", "America/Los_Angeles"},
    {"US/Alaska", "America/Anchorage"},
    {"US/Hawaii", "Pacific/Honolulu"},
    {"Europe/London", "Europe/London"},
    {"Europe/Paris", "Europe/Paris"},
    {"Europe/Berlin", "Europe/Berlin"},
    {"Europe/Rome", "Europe/Rome"},
    {"Europe/Madrid", "Europe/Madrid"},
    {"Europe/Amsterdam", "Europe/Amsterdam"},
    {"Europe/Stockholm", "Europe/Stockholm"},
    {"Europe/Moscow", "Europe/Moscow"},
    {"Asia/Tokyo", "Asia/Tokyo"},
    {"Asia/Shanghai", "Asia/Shanghai"},
    {"Asia/Hong_Kong", "Asia/Hong_Kong"},
    {"Asia/Singapore", "Asia/Singapore"},
    {"Asia/Bangkok", "Asia/Bangkok"},
    {"Asia/Dubai", "Asia/Dubai"},
    {"Asia/Kolkata", "Asia/Kolkata"},
    {"Australia/Sydney", "Australia/Sydney"},
    {"Australia/Melbourne", "Australia/Melbourne"},
    {"Australia/Brisbane", "Australia/Brisbane"},
    {"Australia/Perth", "Australia/Perth"},
    {"Australia/Adelaide", "Australia/Adelaide"},
    {"Australia/Darwin", "Australia/Darwin"},
    {"Pacific/Auckland", "Pacific/Auckland"},
    {"America/Toronto", "America/Toronto"},
    {"America/Vancouver", "America/Vancouver"},
    {"America/Mexico_City", "America/Mexico_City"},
    {"America/Sao_Paulo", "America/Sao_Paulo"},
    {"America/Buenos_Aires", "America/Argentina/Buenos_Aires"},
    {"Africa/Cairo", "Africa/Cairo"},
    {"Africa/Johannesburg", "Africa/Johannesburg"}
};

const int TIMEZONES_COUNT = sizeof(TIMEZONES) / sizeof(TIMEZONES[0]);

// LED strip instance - will be initialized after config load
Adafruit_NeoPixel* strip = nullptr;

// Function declarations
bool startUniqueMDNS(String& name);
void blinkPWMLED(uint8_t pin, unsigned long interval, int blinks);
bool tryConnectWiFi(const char* ssid, const char* password);
int scaledBrightness(int brightness);
int scaledBrightness();
void displayMetricsOnStrip();
void handleRoot();
void handleJson();
void loadDefaultConfig();
bool saveConfig();
void handleConfig();
void setupWebServer();
void handleLogsList();
void checkWiFiConnection();
void startAPMode();
void updateEnergyMeterData();
int sendRequest(const char* method, JsonVariant params, std::function<void(JsonObject&)> callback);
void onMessageCallback(WebsocketsMessage message);
void handleWebSocketEvent(WebsocketsEvent event, String data);
void sendShellyGetStatus();
void checkAndEstablishWebSocket();
bool isValidShellyHostname(const String& host);
bool isValidShellyIP(const char* ip);
void updateMeterActPower(int meterIndex, int newPower);
void storeDataPoint(uint32_t timestampEpoch);
void handleHistory();
void handleHistory24h();
void handleHistory30d();
void loadHistory();
void setInitialTimeRange();
void initializeLEDStrip();
void cleanupPendingRequests();
void cleanupExpiredRequests();
void handleWebSocketError(const String& error);
void monitorNetworkHealth();
void performMDNSQuery();
void checkWebSocketHealth();
bool validateConfig();
void discoverShellyDevices();
void connectToMultipleDevices();
void setupOTA();
void handleOTA();
bool ensureLittleFS();
bool ensureLogDirectory();
bool ensureHistoryDirectory();
bool loadAggregatedHistory(const char* path, AggregatedPoint* buffer, int maxRecords, int &writeIndex, bool &filled);
void persistAggregatedHistory(const char* path, AggregatedPoint* buffer, int maxRecords, int writeIndex, bool filled);
void loadStoredHistories();
String formatTimestampString(uint32_t epochSeconds);
uint32_t extractShellyTimestamp(JsonObject root);
void cleanupOldLogs();
void trimLogFile(const String& path);
String getLogFilePath();
void logSystemEvent(const char* level, const String& message);
void logSystemMetrics(bool force = false);
void monitorMemoryHealth();
String resetReasonToString(esp_reset_reason_t reason);
void recordResetDiagnostics();
void updateDeviceStartTimeFromClock();

// Helper function to find LED type index by flags
int findLEDTypeIndex(uint32_t flags) {
    for (int i = 0; i < LED_TYPES_COUNT; i++) {
        if (LED_TYPES[i].flags == flags) {
            return i;
        }
    }
    return 0; // Default to first type if not found
}

// Helper function to get LED type name by flags
String getLEDTypeName(uint32_t flags) {
    for (int i = 0; i < LED_TYPES_COUNT; i++) {
        if (LED_TYPES[i].flags == flags) {
            return String(LED_TYPES[i].name);
        }
    }
    return "Unknown";
}

// Helper function to find timezone index by Olson name
int findTimezoneIndex(const char* olson) {
    for (int i = 0; i < TIMEZONES_COUNT; i++) {
        if (strcmp(TIMEZONES[i].olson, olson) == 0) {
            return i;
        }
    }
    return 23; // Default to Australia/Sydney
}

// Helper function to get timezone display name
String getTimezoneDisplayName(const char* olson) {
    for (int i = 0; i < TIMEZONES_COUNT; i++) {
        if (strcmp(TIMEZONES[i].olson, olson) == 0) {
            return String(TIMEZONES[i].name);
        }
    }
    return "Unknown";
}

// Helper function to format uptime
String formatUptime(unsigned long uptimeSeconds) {
    unsigned long days = uptimeSeconds / 86400;
    uptimeSeconds %= 86400;
    unsigned long hours = uptimeSeconds / 3600;
    uptimeSeconds %= 3600;
    unsigned long minutes = uptimeSeconds / 60;
    unsigned long seconds = uptimeSeconds % 60;
    
    String result = "";
    if (days > 0) {
        result += String(days) + "d ";
    }
    if (hours > 0 || days > 0) {
        result += String(hours) + "h ";
    }
    if (minutes > 0 || hours > 0 || days > 0) {
        result += String(minutes) + "m ";
    }
    result += String(seconds) + "s";
    
    return result;
}

bool ensureLittleFS() {
    if (littleFSReady) {
        return true;
    }
    if (LittleFS.begin(false)) {
        littleFSReady = true;
        TIMED_PRINTLN("LittleFS mounted successfully.");
        return true;
    }
    TIMED_PRINTLN("LittleFS mount failed (non-destructive). Logging/static assets unavailable.");
    return false;
}

bool ensureLogDirectory() {
    if (!ensureLittleFS()) {
        return false;
    }
    if (!LittleFS.exists(LOG_DIRECTORY)) {
        if (!LittleFS.mkdir(LOG_DIRECTORY)) {
            TIMED_PRINTLN("Failed to create log directory.");
            return false;
        }
    }
    return true;
}

void trimLogFile(const String& path) {
    if (!LittleFS.exists(path.c_str())) {
        return;
    }
    File file = LittleFS.open(path, "r");
    if (!file) {
        return;
    }
    size_t fileSize = file.size();
    if (fileSize <= LOG_FILE_SIZE_LIMIT) {
        file.close();
        return;
    }
    size_t retainBytes = LOG_FILE_SIZE_LIMIT / 2;
    size_t seekPos = fileSize > retainBytes ? fileSize - retainBytes : 0;
    file.seek(seekPos, SeekSet);
    String tempPath = String(LOG_DIRECTORY) + "/.tmp";
    File temp = LittleFS.open(tempPath, "w");
    if (!temp) {
        file.close();
        return;
    }
    temp.println("[Log truncated - keeping recent entries]");
    while (file.available()) {
        temp.write(file.read());
    }
    file.close();
    temp.close();
    LittleFS.remove(path.c_str());
    LittleFS.rename(tempPath.c_str(), path.c_str());
}

void cleanupOldLogs() {
    if (!ensureLogDirectory()) {
        return;
    }
    File dir = LittleFS.open(LOG_DIRECTORY);
    if (!dir || !dir.isDirectory()) {
        return;
    }
    std::vector<String> files;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        files.push_back(String(entry.name()));
        entry.close();
    }
    dir.close();
    if (files.size() <= MAX_LOG_FILES) {
        return;
    }
    std::sort(files.begin(), files.end());
    while (files.size() > MAX_LOG_FILES) {
        String oldest = files.front();
        files.erase(files.begin());
        if (!oldest.startsWith("/")) {
            oldest = String(LOG_DIRECTORY) + "/" + oldest;
        }
        LittleFS.remove(oldest.c_str());
    }
}

String getLogFilePath() {
    char buffer[32];
    if (timeInitialized) {
        time_t now = myTZ.now();
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        strftime(buffer, sizeof(buffer), "/logs/%Y%m%d_%H.log", &timeinfo);
    } else {
        unsigned long hoursSinceBoot = millis() / 3600000UL;
        snprintf(buffer, sizeof(buffer), "/logs/boot%05lu_%02lu.log",
                 static_cast<unsigned long>(bootCounter),
                 static_cast<unsigned long>(hoursSinceBoot % 100));
    }
    return String(buffer);
}

void logSystemEvent(const char* level, const String& message) {
    if (!ensureLogDirectory()) {
        return;
    }
    cleanupOldLogs();
    String logPath = getLogFilePath();
    const char* mode = LittleFS.exists(logPath.c_str()) ? "a" : "w";
    File logFile = LittleFS.open(logPath, mode);
    if (!logFile) {
        return;
    }
    String timeLabel;
    if (timeInitialized) {
        timeLabel = myTZ.dateTime("Y-m-d H:i:s T");
    } else {
        timeLabel = "uptime:" + String(millis() / 1000) + "s";
    }
    logFile.print(timeLabel);
    logFile.print(" [");
    logFile.print(level);
    logFile.print("] ");
    logFile.println(message);
    logFile.close();
    trimLogFile(logPath);
}

String resetReasonToString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN: return "UNKNOWN";
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXTERNAL";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNSPECIFIED";
    }
}

void recordResetDiagnostics() {
    esp_reset_reason_t reason = esp_reset_reason();
    lastResetReasonRaw = reason;
    String message = "Boot #" + String(bootCounter) + " reset=" + resetReasonToString(reason) +
                     " freeHeap=" + String(ESP.getFreeHeap()) +
                     " flashFree=" + String(ESP.getFreeSketchSpace());
    logSystemEvent("BOOT", message);
    switch (reason) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            crashCounter++;
            logSystemEvent("CRASH", "Crash count=" + String(crashCounter) + " reason=" + resetReasonToString(reason));
            break;
        default:
            break;
    }
}

void updateDeviceStartTimeFromClock() {
    if (!timeInitialized) {
        return;
    }
    time_t now = myTZ.now();
    if (now > 0) {
        deviceStartTimeMillis = static_cast<uint64_t>(now) * 1000ULL - millis();
    }
}

void monitorMemoryHealth() {
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (freeHeap < lowestHeapObserved) {
        lowestHeapObserved = freeHeap;
    }
    if (largestBlock < lowestLargestBlockObserved) {
        lowestLargestBlockObserved = largestBlock;
    }
    if (freeHeap < LOW_MEMORY_THRESHOLD && millis() - lastLowMemoryLog > LOW_MEMORY_LOG_INTERVAL) {
        lastLowMemoryLog = millis();
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "Low heap: %uB free, largest block %uB", static_cast<unsigned int>(freeHeap),
                 static_cast<unsigned int>(largestBlock));
        logSystemEvent("WARN", String(buffer));
    }
}

void logSystemMetrics(bool force) {
    unsigned long now = millis();
    if (!force && now - lastMetricsLog < METRICS_LOG_INTERVAL) {
        return;
    }
    lastMetricsLog = now;
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t minHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    char buffer[192];
    snprintf(buffer, sizeof(buffer),
             "heap=%uB (min=%uB largest=%uB) loopMax=%luus WiFi=%d ws=%d pendingRPC=%u",
             static_cast<unsigned int>(freeHeap),
             static_cast<unsigned int>(minHeap),
             static_cast<unsigned int>(largestBlock),
             maxLoopDurationMicros,
             static_cast<int>(WiFi.status()),
             static_cast<int>(wsClient.available()),
             static_cast<unsigned int>(pendingRequests.size()));
    logSystemEvent("METRICS", String(buffer));
}

struct HistoryFileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t maxRecords;
    uint16_t writeIndex;
    uint8_t filled;
    uint8_t reserved;
};

bool ensureHistoryDirectory() {
    if (!ensureLittleFS()) {
        return false;
    }
    if (!LittleFS.exists(HISTORY_DIRECTORY)) {
        if (!LittleFS.mkdir(HISTORY_DIRECTORY)) {
            TIMED_PRINTLN("Failed to create history directory.");
            return false;
        }
    }
    return true;
}

bool loadAggregatedHistory(const char* path, AggregatedPoint* buffer, int maxRecords, int &writeIndex, bool &filled) {
    if (!ensureHistoryDirectory()) {
        return false;
    }
    memset(buffer, 0, maxRecords * sizeof(AggregatedPoint));
    writeIndex = 0;
    filled = false;
    if (!LittleFS.exists(path)) {
        return false;
    }
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }
    HistoryFileHeader header;
    if (file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
        file.close();
        return false;
    }
    if (header.magic != HISTORY_FILE_MAGIC || header.version != HISTORY_FILE_VERSION || header.maxRecords != maxRecords) {
        file.close();
        TIMED_PRINTLN(String("History file header mismatch for ") + path);
        return false;
    }
    size_t expectedSize = maxRecords * sizeof(AggregatedPoint);
    size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(buffer), expectedSize);
    file.close();
    if (bytesRead != expectedSize) {
        TIMED_PRINTLN(String("History file size mismatch for ") + path);
        return false;
    }
    writeIndex = header.writeIndex < maxRecords ? header.writeIndex : 0;
    filled = header.filled == 1;
    return true;
}

void persistAggregatedHistory(const char* path, AggregatedPoint* buffer, int maxRecords, int writeIndex, bool filled) {
    if (!ensureHistoryDirectory()) {
        return;
    }
    File file = LittleFS.open(path, "w");
    if (!file) {
        TIMED_PRINTLN(String("Failed to open history file for writing: ") + path);
        return;
    }
    HistoryFileHeader header;
    header.magic = HISTORY_FILE_MAGIC;
    header.version = HISTORY_FILE_VERSION;
    header.maxRecords = maxRecords;
    header.writeIndex = writeIndex;
    header.filled = filled ? 1 : 0;
    header.reserved = 0;
    
    file.write(reinterpret_cast<uint8_t*>(&header), sizeof(header));
    file.write(reinterpret_cast<uint8_t*>(buffer), maxRecords * sizeof(AggregatedPoint));
    file.close();
}

void loadStoredHistories() {
    if (!ensureHistoryDirectory()) {
        return;
    }
    bool loaded24h = loadAggregatedHistory(HISTORY_24H_FILE, history24h, HISTORY_24H_BUCKETS, history24hIndex, history24hFilled);
    bool loaded30d = loadAggregatedHistory(HISTORY_30D_FILE, history30d, HISTORY_30D_BUCKETS, history30dIndex, history30dFilled);
    TIMED_PRINTLN(String("History 24h loaded: ") + (loaded24h ? "yes" : "no"));
    TIMED_PRINTLN(String("History 30d loaded: ") + (loaded30d ? "yes" : "no"));
}
// Validation functions
bool validateConfig() {
    bool valid = true;
    
    if (LED_COUNT_var < 1 || LED_COUNT_var > 300) {
        TIMED_PRINTLN("Invalid LED count, resetting to default");
        LED_COUNT_var = 60;
        valid = false;
    }
    
    if (LED_PIN_var < 0 || LED_PIN_var > 39) {
        TIMED_PRINTLN("Invalid LED pin, resetting to default");
        LED_PIN_var = 4;
        valid = false;
    }
    
    // Validate LED type flags
    currentLEDTypeIndex = findLEDTypeIndex(LED_TYPE_flags);
    if (currentLEDTypeIndex == 0 && LED_TYPE_flags != LED_TYPES[0].flags) {
        TIMED_PRINTLN("Invalid LED type flags, resetting to default");
        LED_TYPE_flags = LED_TYPES[0].flags;
        currentLEDTypeIndex = 0;
        valid = false;
    }
    
    return valid;
}

// LED strip management
void initializeLEDStrip() {
    if (strip) {
        delete strip;
    }
    
    strip = new Adafruit_NeoPixel(LED_COUNT_var, LED_PIN_var, LED_TYPE_flags);
    strip->begin();
    strip->setBrightness(scaledBrightness());
    strip->show();
    
    TIMED_PRINTLN("LED strip initialized:");
    TIMED_PRINTLN("  Type: " + getLEDTypeName(LED_TYPE_flags) + " (0x" + String(LED_TYPE_flags, HEX) + ")");
    TIMED_PRINTLN("  Count: " + String(LED_COUNT_var) + " LEDs");
    TIMED_PRINTLN("  Pin: " + String(LED_PIN_var));
    TIMED_PRINTLN("  Inverted: " + String(invertStrip ? "Yes" : "No"));
}

// Network and connection management
void cleanupPendingRequests() {
    pendingRequests.clear();
    rpcInProgress = false;
    TIMED_PRINTLN("Pending requests cleaned up");
}

void cleanupExpiredRequests() {
    unsigned long now = millis();
    bool timedOut = false;
    for (auto it = pendingRequests.begin(); it != pendingRequests.end();) {
        unsigned long age = now - it->second.timestamp;
        if (age > REQUEST_TIMEOUT) {
            TIMED_PRINTLN("Request " + String(it->first) + " timed out after " + String(age) + "ms");
            logSystemEvent("RPC", "Request " + String(it->first) + " timed out after " + String(age) + "ms");
            it = pendingRequests.erase(it);
            timedOut = true;
        } else {
            ++it;
        }
    }
    if (timedOut && pendingRequests.empty()) {
        rpcInProgress = false;
        TIMED_PRINTLN("RPC state reset after timeout recovery");
        logSystemEvent("RPC", "RPC state reset after timeout recovery");
    }
}

void handleWebSocketError(const String& error) {
    TIMED_PRINTLN("WebSocket error: " + error);
    logSystemEvent("WS", "WebSocket error: " + error);
    cleanupPendingRequests();
    lastWebSocketAttempt = millis() + webSocketRetryInterval; // Backoff
}

void checkWebSocketHealth() {
    if (wsClient.available() && (millis() - lastWSActivity > WS_TIMEOUT)) {
        TIMED_PRINTLN("WebSocket timeout, reconnecting...");
        logSystemEvent("WS", "WebSocket timeout detected, closing connection");
        wsClient.close();
        cleanupPendingRequests();
    }
}

void monitorNetworkHealth() {
    // Don't perform network health checks if WiFi isn't connected
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    
    static unsigned long lastPing = 0;
    if (millis() - lastPing > NETWORK_HEALTH_CHECK_INTERVAL) {
        lastPing = millis();
        if (strlen(shellyIP) > 0) {
            IPAddress ip;
            ip.fromString(shellyIP);
            if (!Ping.ping(ip, 1)) { // Single ping with timeout
                shellyPingFailureCount++;
                TIMED_PRINTLN("Shelly device ping failed (" + String(shellyPingFailureCount) + "/" + String(SHELLY_PING_FAILURE_THRESHOLD) + ")");
                if (shellyPingFailureCount >= SHELLY_PING_FAILURE_THRESHOLD) {
                    TIMED_PRINTLN("Shelly device unreachable, triggering rediscovery");
                    logSystemEvent("WARN", "Shelly unreachable at " + String(shellyIP) + " after " + String(shellyPingFailureCount) + " failed checks");
                    wsClient.close();
                    shellyRediscoveryNeeded = true;
                    shellyPingFailureCount = 0;
                    lastWebSocketAttempt = 0; // allow immediate reconnect attempt
                }
            } else {
                if (shellyPingFailureCount > 0) {
                    logSystemEvent("INFO", "Shelly reachable again after " + String(shellyPingFailureCount) + " failures");
                }
                shellyPingFailureCount = 0;
                shellyRediscoveryNeeded = false;
                lastSuccessfulShellyPing = millis();
            }
        }
    }
}

// Shelly device discovery and management
bool isValidShellyHostname(const String& host) {
    String lowerHost = host;
    lowerHost.toLowerCase();
    return (lowerHost.startsWith("shelly") && host.indexOf('-') != -1);
}

bool isValidShellyIP(const char* ip) {
    String ipStr = String(ip);
    return ipStr.length() > 0 && ipStr != "xxx";
}

void discoverShellyDevices() {
    // Don't attempt discovery if WiFi isn't connected
    if (WiFi.status() != WL_CONNECTED) {
        TIMED_PRINTLN("Skipping Shelly discovery - WiFi not connected");
        return;
    }
    
    if (millis() - lastMDNSQuery < MDNS_QUERY_INTERVAL) {
        return;
    }
    lastMDNSQuery = millis();
    
    TIMED_PRINTLN("Discovering Shelly devices...");
    shellyDevices.clear();
    
    int nServices = MDNS.queryService("http", "tcp");
    TIMED_PRINTLN("Found " + String(nServices) + " HTTP services");
    
    for (int i = 0; i < nServices; ++i) {
        String host = MDNS.hostname(i);
        TIMED_PRINTLN("Checking service " + String(i) + ": " + host + " at " + MDNS.IP(i).toString());
        
        if (isValidShellyHostname(host)) {
            ShellyDevice device;
            device.ip = MDNS.IP(i).toString();
            device.hostname = host;
            device.channelOffset = 0;
            device.isActive = false;
            
            String lowerHost = host;
            lowerHost.toLowerCase();
            if (lowerHost.startsWith("shellypro3em")) {
                device.type = "3EM";
            } else if (lowerHost.startsWith("shellyem-")) {
                device.type = "EM";
                device.channelOffset = shellyDevices.size();
            } else {
                device.type = "Unknown";
            }
            
            shellyDevices.push_back(device);
            TIMED_PRINTLN("Found " + device.type + " device: " + host + " at " + device.ip);
        } else {
            TIMED_PRINTLN("Skipping non-Shelly device: " + host);
        }
    }
    
    TIMED_PRINTLN("Discovery complete. Found " + String(shellyDevices.size()) + " Shelly devices");
}

// Brightness and display functions
int scaledBrightness(int brightness) {
    if (brightness <= 0) return 0;
    if (brightness >= MAX_BRIGHTNESS) return MAX_BRIGHTNESS;
    float scale = (float)globalBrightness / MAX_BRIGHTNESS;
    int newBrightness = (int)(brightness * scale);
    return newBrightness <= 0 ? 1 : min(newBrightness, MAX_BRIGHTNESS);
}

int scaledBrightness() { 
    return scaledBrightness(globalBrightness); 
}

void displayMetricsOnStrip() {
    if (!strip) return;
    
    int consumerValue = 0;
    int solarValue = 0;

    for (int i = 0; i < 3; i++) {
        if (meters[i].name.equalsIgnoreCase("Consumer")) {
            consumerValue = meters[i].act_power;
        } else if (meters[i].name.equalsIgnoreCase("Solar")) {
            solarValue = meters[i].act_power;
        }
    }

    uint32_t colorBoth     = strip->Color(scaledBrightness(153), scaledBrightness(255), 0, 0);
    uint32_t colorConsumer = strip->Color(scaledBrightness(255), 0, 0, 0);
    uint32_t colorSolar    = strip->Color(0, scaledBrightness(204), scaledBrightness(255), 0);
    uint32_t colorOff      = strip->Color(0, 0, 0, 0);

    int minRaw = 100;
    int maxRaw = 5000;
    int range  = (maxRaw - minRaw);
    int last   = LED_COUNT_var - 1;

    for (int i = 0; i < LED_COUNT_var; i++) {
        int ledValue = minRaw + (range * i) / last;
        // Calculate actual physical LED index based on strip direction
        int physicalLED = invertStrip ? (LED_COUNT_var - 1 - i) : i;
        
        if (ledValue <= consumerValue && ledValue <= solarValue) {
            strip->setPixelColor(physicalLED, colorBoth);
        } else if (ledValue <= consumerValue) {
            strip->setPixelColor(physicalLED, colorConsumer);
        } else if (ledValue <= solarValue) {
            strip->setPixelColor(physicalLED, colorSolar);
        } else {
            strip->setPixelColor(physicalLED, colorOff);
        }
    }
    strip->show();
}

// Status LED functions
void blinkPWMLED(uint8_t pin, unsigned long interval, int blinks) {
    static unsigned long lastBlinkTime = 0;
    static int blinkCount = 0;
    static bool ledState = false;
    unsigned long currentMillis = millis();
    if (blinkCount < blinks * 2) {
        if (currentMillis - lastBlinkTime >= interval) {
            lastBlinkTime = currentMillis;
            ledState = !ledState;
            digitalWrite(pin, ledState ? HIGH : LOW);
            if (!ledState) blinkCount++;
        }
    } else {
        blinkCount = 0;
        digitalWrite(pin, LOW);
    }
}

void flickStatusLED() {
    digitalWrite(LED_STATUS_PIN, HIGH);
    delay(100);
    digitalWrite(LED_STATUS_PIN, LOW);
}

// WiFi connection management
bool tryConnectWiFi(const char* ssid, const char* password) {
    // Don't try to connect if SSID is empty or too long
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) >= 32) {
        TIMED_PRINTLN("Invalid SSID - not attempting connection");
        return false;
    }
    
    // Ensure we're in STA mode
    WiFi.mode(WIFI_STA);
    delay(100);
    
    WiFi.disconnect(true);
    delay(100);
    
    WiFi.begin(ssid, password);
    unsigned long startTime = millis();
    TIMED_PRINT("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
        delay(500);
        Serial.print(".");
        esp_task_wdt_reset();
    }
    if (WiFi.status() == WL_CONNECTED) {
        TIMED_PRINTLN("WiFi Connected!");
        TIMED_PRINTLN("IP Address: " + String(WiFi.localIP()));
        logSystemEvent("WIFI", "Connected to SSID=" + String(ssid) + " IP=" + WiFi.localIP().toString());
        return true;
    } else {
        TIMED_PRINTLN("Failed to connect to WiFi.");
        logSystemEvent("WIFI", "Failed to connect to SSID=" + String(ssid));
        return false;
    }
}

void checkWiFiConnection() {
    static unsigned long lastRetryAttempt = 0;
    const unsigned long RETRY_INTERVAL = 300000; // 5 minutes
    
    // If we're in AP mode (or AP+STA mode), periodically try to reconnect with stored credentials
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA || isAPMode) {
        // While in AP mode, periodically try stored credentials if they exist
        if (strlen(ssid) > 0 && strlen(ssid) < 32) {
            if (millis() - lastRetryAttempt > RETRY_INTERVAL) {
                lastRetryAttempt = millis();
                TIMED_PRINTLN("AP mode: Attempting to reconnect with stored credentials...");
                TIMED_PRINTLN("Trying SSID: " + String(ssid));
                
                // Stop AP mode services temporarily
                dnsServer.stop();
                server.stop();
                delay(100);
                
                // Try to connect in STA mode
                WiFi.mode(WIFI_OFF);
                delay(500);
                WiFi.mode(WIFI_STA);
                delay(100);
                
                if (tryConnectWiFi(ssid, password)) {
                    TIMED_PRINTLN("Successfully reconnected! Exiting AP mode.");
                    isAPMode = false;
                    
                    // Reinitialize services for STA mode
                    setupWebServer();
                    
                    // Initialize ezTime if not already done
                    if (!timeInitialized) {
                        TIMED_PRINTLN("Initializing time synchronization...");
                        setDebug(INFO);
                        waitForSync();
                        if (myTZ.setLocation(timezone)) {
                            TIMED_PRINTLN("Timezone set to: " + getTimezoneDisplayName(timezone));
                            TIMED_PRINTLN("Current time: " + myTZ.dateTime("Y-m-d H:i:s T"));
                            timeInitialized = true;
                            updateDeviceStartTimeFromClock();
                        }
                    }
                    
                    // Setup mDNS if needed
                    MDNS.end();
                    if (startUniqueMDNS(ShemeterName)) {
                        TIMED_PRINTLN("mDNS responder restarted successfully.");
                    }
                    
                    // Discover Shelly devices
                    discoverShellyDevices();
                    
                    TIMED_PRINTLN("Switched from AP mode to STA mode successfully!");
                    TIMED_PRINTLN("Device accessible at: http://" + WiFi.localIP().toString());
                } else {
                    TIMED_PRINTLN("Retry failed. Returning to AP mode.");
                    startAPMode();
                }
            }
        }
        return; // Don't proceed to STA checks if we're in AP mode
    }
    
    // Only try to reconnect if we have valid credentials and we're in STA mode
    if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) {
        // Check if we have valid stored credentials
        if (strlen(ssid) > 0 && strlen(ssid) < 32) {
        TIMED_PRINTLN("WiFi connection lost. Attempting to reconnect...");
        if (tryConnectWiFi(ssid, password)) {
            TIMED_PRINTLN("WiFi reconnected.");
        } else {
                TIMED_PRINTLN("Reconnection failed. Switching to AP mode...");
                startAPMode();
            }
        } else {
            TIMED_PRINTLN("No valid WiFi credentials. Starting AP mode...");
            startAPMode();
        }
    }
}



void startAPMode() {
    TIMED_PRINTLN("DEBUG: startAPMode() called! millis=" + String(millis()));
    static unsigned long lastAPAttempt = 0;
    TIMED_PRINTLN("DEBUG: lastAPAttempt=" + String(lastAPAttempt));
    // Prevent rapid AP mode switching (minimum 10 seconds between attempts)
    if (millis() - lastAPAttempt < 10000 && lastAPAttempt != 0) {
        TIMED_PRINTLN("AP mode start throttled - too soon since last attempt");
        return;
    }
    lastAPAttempt = millis();
    TIMED_PRINTLN("DEBUG: startAPMode() executing past throttle check...");
    
    TIMED_PRINTLN("Starting Access Point mode for WiFi configuration...");
    logSystemEvent("WIFI", "Entering AP mode for configuration");
    
    // Stop web server if it's running
    server.stop();
    delay(100);
    
    // Safely disconnect and reset WiFi
    WiFi.disconnect(true, true);
    delay(1000);
    
    // Clear WiFi mode
    WiFi.mode(WIFI_OFF);
    delay(1000);
    
    // Start AP+STA mode to allow WiFi scanning while AP is active
    WiFi.mode(WIFI_AP_STA);
    delay(1000);
    
    // Configure AP with fixed IP
    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_IP, gateway, subnet);
    
    // Start AP without password for easier configuration
    TIMED_PRINTLN("DEBUG: Calling WiFi.softAP with SSID: " + fallbackSSID);
    bool apStarted = WiFi.softAP(fallbackSSID.c_str(), nullptr, 1, false, 4);
    TIMED_PRINTLN("DEBUG: WiFi.softAP returned: " + String(apStarted ? "true" : "false"));
    
    if (apStarted) {
        delay(2000); // Give AP time to fully start
        TIMED_PRINTLN("Access Point started successfully!");
        TIMED_PRINTLN("Network: " + fallbackSSID + " (No Password)");
        TIMED_PRINTLN("AP IP: " + WiFi.softAPIP().toString());
        TIMED_PRINTLN("Connect to this network and navigate to: http://" + WiFi.softAPIP().toString() + "/wificonfig");
        TIMED_PRINTLN("The configuration page will help you set up WiFi credentials.");
        
        // Set AP mode flag for captive portal
        isAPMode = true;
        
        // Start DNS server for captive portal (redirects all DNS requests to our IP)
        dnsServer.start(53, "*", WiFi.softAPIP());
        TIMED_PRINTLN("DNS server started for captive portal");
        
        // Start web server for AP mode
        setupWebServer();
        TIMED_PRINTLN("Configuration web server started");
        
        // Additional diagnostics
        TIMED_PRINTLN("AP Stations: " + String(WiFi.softAPgetStationNum()));
        TIMED_PRINTLN("AP Channel: " + String(WiFi.channel()));
        logSystemEvent("WIFI", "AP mode active SSID=" + fallbackSSID + " IP=" + WiFi.softAPIP().toString());
    } else {
        TIMED_PRINTLN("Failed to start Access Point!");
        logSystemEvent("ERROR", "Failed to start AP mode. Restarting device.");
        delay(2000);
        ESP.restart(); // Restart if AP fails to start
    }
}

// WiFi configuration portal handlers
void handleWiFiConfig() {
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>" + ShemeterName + " - WiFi Setup</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0}";
    html += ".container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}";
    html += "h1{color:#333;margin-top:0}";
    html += ".network{padding:12px;margin:8px 0;background:#f9f9f9;border:1px solid #ddd;border-radius:4px;cursor:pointer;display:flex;justify-content:space-between;align-items:center}";
    html += ".network:hover{background:#e9e9e9}";
    html += ".network.selected{background:#d0e8ff;border-color:#0066cc}";
    html += ".signal{font-size:0.9em;color:#666}";
    html += "input[type=password]{width:100%;padding:10px;margin:8px 0;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}";
    html += "button{width:100%;padding:12px;background:#0066cc;color:white;border:none;border-radius:4px;font-size:16px;cursor:pointer;margin-top:10px}";
    html += "button:hover{background:#0052a3}";
    html += ".scanning{text-align:center;padding:20px;color:#666}";
    html += ".status{padding:10px;margin:10px 0;border-radius:4px;display:none}";
    html += ".status.success{background:#d4edda;color:#155724;display:block}";
    html += ".status.error{background:#f8d7da;color:#721c24;display:block}</style></head>";
    html += "<body><div class='container'><h1>🔌 WiFi Setup</h1>";
    html += "<p>Configure " + ShemeterName + " to connect to your WiFi network.</p>";
    html += "<div id='status' class='status'></div>";
    html += "<div id='networks' class='scanning'>Scanning for networks...</div>";
    html += "<div id='configForm' style='display:none'>";
    html += "<input type='password' id='password' placeholder='WiFi Password' />";
    html += "<button onclick='connect()'>Connect</button></div>";
    html += "<script>";
    html += "let selectedSSID='';";
    html += "function selectNetwork(ssid){selectedSSID=ssid;document.querySelectorAll('.network').forEach(n=>n.classList.remove('selected'));";
    html += "event.target.closest('.network').classList.add('selected');document.getElementById('configForm').style.display='block';}";
    html += "function connect(){const pwd=document.getElementById('password').value;";
    html += "fetch('/wifisave',{method:'POST',headers:{'Content-Type':'application/json'},";
    html += "body:JSON.stringify({ssid:selectedSSID,password:pwd})}).then(r=>r.json()).then(d=>{";
    html += "const s=document.getElementById('status');if(d.success){s.className='status success';s.textContent='✓ Connecting to '+selectedSSID+'... Device will restart.';";
    html += "setTimeout(()=>window.location.href='http://'+selectedSSID.toLowerCase()+'.local',5000);}";
    html += "else{s.className='status error';s.textContent='✗ Failed: '+d.message;}}).catch(e=>{";
    html += "document.getElementById('status').className='status error';document.getElementById('status').textContent='✗ Connection failed';});}";
    html += "fetch('/wifiscan').then(r=>r.json()).then(d=>{";
    html += "console.log('Scan response:', d);"; // Debug logging
    html += "if(d.error){document.getElementById('networks').innerHTML='<p>Scan error: '+d.error+'. <a href=\"javascript:location.reload()\">Retry</a></p>';return;}";
    html += "if(d.message){document.getElementById('networks').innerHTML='<p>'+d.message+'</p>';return;}";
    html += "if(!d.networks||d.networks.length===0){document.getElementById('networks').innerHTML='<p>No networks found. <a href=\"javascript:location.reload()\">Retry</a></p>';return;}";
    html += "let html='';d.networks.forEach(n=>{if(!n.ssid||n.ssid.trim()==='')return;"; // Skip empty SSIDs
    html += "const bars='📶'.repeat(Math.max(1,Math.ceil((n.rssi+100)/20)));"; // Better RSSI calculation
    html += "html+='<div class=\"network\" onclick=\"selectNetwork(\\''+n.ssid.replace(/'/g,\"\\\\'\")+'\\')\">';";
    html += "html+='<span>'+n.ssid+(n.secure?' 🔒':'')+'</span><span class=\"signal\">'+bars+'</span></div>';});";
    html += "document.getElementById('networks').innerHTML=html||'<p>No networks found</p>';}).catch(e=>{";
    html += "console.error('Scan failed:', e);";
    html += "document.getElementById('networks').innerHTML='<p>Scan request failed. <a href=\"javascript:location.reload()\">Retry</a></p>';});</script></div></body></html>";
    
    server.send(200, "text/html", html);
}

void handleWiFiScan() {
    TIMED_PRINTLN("=== WiFi Scan Request Received ===");
    
    static StaticJsonDocument<JSON_CAPACITY_WIFI_SCAN> doc;
    doc.clear();
    JsonArray networks = doc["networks"].to<JsonArray>();
    
    // Ensure we're in a mode that supports scanning
    wifi_mode_t currentMode = WiFi.getMode();
    TIMED_PRINTLN("Current WiFi mode: " + String(currentMode));
    
    if (currentMode != WIFI_AP_STA && currentMode != WIFI_STA) {
        TIMED_PRINTLN("WiFi scan: Switching to AP_STA mode for scanning");
        WiFi.mode(WIFI_AP_STA);
        delay(100);
    }
    
    // Perform WiFi scan with explicit settings for better reliability
    TIMED_PRINTLN("Starting WiFi network scan...");
    int n = WiFi.scanNetworks(false, true, false, 300); // async=false, show_hidden=true, passive=false, max_ms_per_chan=300
    
    if (n == WIFI_SCAN_FAILED) {
        TIMED_PRINTLN("ERROR: WiFi scan failed!");
        doc["error"] = "Scan failed - radio error";
        doc["count"] = 0;
    } else if (n == 0) {
        TIMED_PRINTLN("No networks found in scan");
        doc["message"] = "No networks found";
        doc["count"] = 0;
    } else {
        TIMED_PRINTLN("WiFi scan found " + String(n) + " networks");
        doc["count"] = n;
        
        for (int i = 0; i < n && i < 20; i++) { // Limit to 20 networks
            String ssid = WiFi.SSID(i);
            int rssi = WiFi.RSSI(i);
            
            // Skip networks with empty SSID (hidden networks without name)
            if (ssid.length() == 0) {
                ssid = "[Hidden Network]";
            }
            
            JsonObject network = networks.add<JsonObject>();
            network["ssid"] = ssid;
            network["rssi"] = rssi;
            network["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            
            // Debug output
            TIMED_PRINTLN("  [" + String(i) + "] " + ssid + " (RSSI: " + String(rssi) + "dBm)");
        }
    }
    
    // Clean up scan results to free memory
    WiFi.scanDelete();
    
    String response;
    serializeJson(doc, response);
    TIMED_PRINTLN("Sending scan response: " + response.substring(0, min(100, (int)response.length())) + "...");
    
    server.send(200, "application/json", response);
    TIMED_PRINTLN("=== WiFi Scan Complete ===");
}

void handleWiFiSave() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
        return;
    }
    
    StaticJsonDocument<JSON_CAPACITY_SMALL> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }
    
    const char* newSSID = doc["ssid"];
    const char* newPassword = doc["password"];
    
    if (!newSSID || strlen(newSSID) == 0) {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"SSID required\"}");
        return;
    }
    
    // Save credentials to config
    strncpy(ssid, newSSID, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    strncpy(password, newPassword ? newPassword : "", sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
    
    if (saveConfig()) {
        TIMED_PRINTLN("WiFi credentials saved: " + String(ssid));
        logSystemEvent("CONFIG", "WiFi credentials updated for SSID=" + String(ssid));
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Credentials saved\"}");
        
        delay(1000);
        TIMED_PRINTLN("Restarting to connect to new WiFi...");
        ESP.restart();
    } else {
        server.send(500, "application/json", "{\"success\":false,\"message\":\"Failed to save config\"}");
    }
}

// Captive portal - redirect all unknown requests to WiFi config
void handleCaptivePortal() {
    if (isAPMode) {
        // Redirect to config page
        server.sendHeader("Location", "http://192.168.4.1/wificonfig", true);
        server.send(302, "text/plain", "");
    } else {
        handleRoot(); // Normal operation
    }
}

void handleLogsList() {
    if (!ensureLogDirectory()) {
        server.send(500, "application/json", "{\"success\":false,\"message\":\"LittleFS not available\"}");
        return;
    }

    File dir = LittleFS.open(LOG_DIRECTORY);
    if (!dir || !dir.isDirectory()) {
        server.send(500, "application/json", "{\"success\":false,\"message\":\"Logs directory not available\"}");
        return;
    }

    String response = "{\"logs\":[";
    bool first = true;
    size_t count = 0;

    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        if (entry.isDirectory()) {
            entry.close();
            continue;
        }
        if (!first) {
            response += ",";
        }
        first = false;
        count++;
        String name = String(entry.name());
        if (name.startsWith("/")) {
            name.remove(0, 1);
        }
        response += "{\"name\":\"" + name + "\",\"size\":" + String(entry.size()) + "}";
        entry.close();
    }
    dir.close();

    response += "],\"count\":" + String(count) + "}";
    server.sendHeader("Cache-Control", "no-cache, max-age=0");
    server.send(200, "application/json", response);
}

// Web server handlers
void handleRoot() {
    // If in AP mode, redirect to WiFi config
    if (isAPMode) {
        server.sendHeader("Location", "/wificonfig", true);
        server.send(302, "text/plain", "");
        return;
    }
     
    // Try to serve from LittleFS first, fallback to built-in minimal page
    if (ensureLittleFS() && LittleFS.exists("/index.html")) {
        File file = LittleFS.open("/index.html", "r");
        if (file) {
            server.streamFile(file, "text/html");
            file.close();
            return;
        }
    }
    
    // Minimal fallback HTML page to save flash memory
    String html = "<!DOCTYPE html><html><head><title>Energy Monitor</title><meta name='viewport' content='width=device-width, initial-scale=1'></head><body>";
    html += "<h1>Energy Monitor</h1>";
    html += "<p>Dashboard loading failed. Try <a href='/config'>Settings</a> or <a href='/test'>Test</a></p>";
    html += "<script>setTimeout(() => location.reload(), 5000);</script>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleJson() {
    // Add performance headers
    server.sendHeader("Cache-Control", "no-cache, max-age=0");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    StaticJsonDocument<JSON_CAPACITY_MEDIUM> doc;
    
    doc["SheMeterName"] = ShemeterName;
    
    // Add uptime information
    unsigned long uptimeSeconds = millis() / 1000;
    doc["uptime"] = formatUptime(uptimeSeconds);
    doc["uptimeSeconds"] = uptimeSeconds;
    
    // Add current time information
    if (timeInitialized) {
        doc["currentTime"] = myTZ.dateTime("Y-m-d H:i:s T");
        doc["timezone"] = getTimezoneDisplayName(timezone);
    } else {
        doc["currentTime"] = "Time not synchronized";
        doc["timezone"] = "Unknown";
    }
    
    JsonArray metersArray = doc["meters"].to<JsonArray>();
    
    for (int i = 0; i < 3; i++) {
        JsonObject meter = metersArray.add<JsonObject>();
        meter["name"] = meters[i].name;
        meter["power"] = meters[i].act_power;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// Load critical settings from NVS (survives uploadfs)
bool loadConfigNVS() {
    if (!preferences.begin(NVS_NAMESPACE, true)) { // true = read-only
        TIMED_PRINTLN("Failed to open NVS namespace for reading");
        return false;
    }
    
    bool hasConfig = preferences.isKey("ssid");
    
    if (hasConfig) {
        TIMED_PRINTLN("Loading configuration from NVS (uploadfs-safe storage)");
        
        // Load WiFi credentials
        preferences.getString("ssid", ssid, sizeof(ssid));
        preferences.getString("password", password, sizeof(password));
        preferences.getString("shellyIP", shellyIP, sizeof(shellyIP));
        preferences.getString("timezone", timezone, sizeof(timezone));
        ShemeterName = preferences.getString("shemeterName", ShemeterName);
        fallbackSSID = ShemeterName + "AP";
        
        // Load LED configuration
        LED_COUNT_var = preferences.getInt("ledCount", 60);
        LED_PIN_var = preferences.getInt("ledPin", 4);
        LED_TYPE_flags = preferences.getUInt("ledType", NEO_GRBW + NEO_KHZ800);
        invertStrip = preferences.getBool("invertStrip", false);
        
        // Load meter names
        meters[0].name = preferences.getString("meter0", "Grid");
        meters[1].name = preferences.getString("meter1", "Solar");
        meters[2].name = preferences.getString("meter2", "Consumer");
        
        preferences.end();
        
        currentLEDTypeIndex = findLEDTypeIndex(LED_TYPE_flags);
        validateConfig();
        
        TIMED_PRINTLN("Configuration loaded from NVS:");
        TIMED_PRINTLN("SSID: " + String(ssid));
        TIMED_PRINTLN("Shelly IP: " + String(shellyIP));
        TIMED_PRINTLN("SheMeter Name: " + ShemeterName);
        TIMED_PRINTLN("Timezone: " + getTimezoneDisplayName(timezone) + " (" + String(timezone) + ")");
        TIMED_PRINTLN("LED Count: " + String(LED_COUNT_var));
        TIMED_PRINTLN("LED Pin: " + String(LED_PIN_var));
        TIMED_PRINTLN("LED Type: " + getLEDTypeName(LED_TYPE_flags) + " (0x" + String(LED_TYPE_flags, HEX) + ")");
        TIMED_PRINTLN("LED Strip Inverted: " + String(invertStrip ? "Yes" : "No"));
        
        return true;
    } else {
        TIMED_PRINTLN("No configuration found in NVS");
        preferences.end();
        return false;
    }
}

// Save critical settings to NVS (survives uploadfs)
bool saveConfigNVS() {
    if (!preferences.begin(NVS_NAMESPACE, false)) { // false = read-write
        TIMED_PRINTLN("Failed to open NVS namespace for writing");
        return false;
    }
    
    // Save WiFi credentials
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.putString("shellyIP", shellyIP);
    preferences.putString("timezone", timezone);
    preferences.putString("shemeterName", ShemeterName.c_str());
    
    // Save LED configuration
    preferences.putInt("ledCount", LED_COUNT_var);
    preferences.putInt("ledPin", LED_PIN_var);
    preferences.putUInt("ledType", LED_TYPE_flags);
    preferences.putBool("invertStrip", invertStrip);
    
    // Save meter names
    preferences.putString("meter0", meters[0].name.c_str());
    preferences.putString("meter1", meters[1].name.c_str());
    preferences.putString("meter2", meters[2].name.c_str());
    
    preferences.end();
    
    TIMED_PRINTLN("Configuration saved to NVS (uploadfs-safe storage)");
    return true;
}

void loadDefaultConfig() {
    strncpy(ssid, defaultSSID, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    strncpy(password, defaultPassword, sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
    strncpy(shellyIP, defaultShellyIP, sizeof(shellyIP) - 1);
    shellyIP[sizeof(shellyIP) - 1] = '\0';
    strncpy(timezone, defaultTimezone, sizeof(timezone) - 1);
    timezone[sizeof(timezone) - 1] = '\0';

    ShemeterName = "SheMonitor";
    fallbackSSID = ShemeterName + "AP";

    meters[0].name = "Grid";
    meters[1].name = "Solar";
    meters[2].name = "Consumer";

    LED_COUNT_var = 60;
    LED_PIN_var = 4;
    LED_TYPE_flags = NEO_GRBW + NEO_KHZ800;
    currentLEDTypeIndex = findLEDTypeIndex(LED_TYPE_flags);
    invertStrip = false;

    validateConfig();
}

bool saveConfig() {
    return saveConfigNVS();
}

// Enhanced configuration handler with validation
void handleConfig() {
    if (server.method() == HTTP_POST) {
        String newSSID = server.arg("ssid");
        String newPassword = server.arg("password");
        String newShellyIP = server.arg("shellyIP");
        String newSheMeterName = server.arg("shemeterName");
        String newTimezone = server.arg("timezone");
        String meter0Role = server.arg("meter0");
        String meter1Role = server.arg("meter1");
        String meter2Role = server.arg("meter2");

        // LED configuration
        String ledCountStr = server.arg("ledCount");
        String ledPinStr = server.arg("ledPin");
        String ledTypeStr = server.arg("ledType");
        bool ledInvert = server.hasArg("invertStrip");

        bool valid = true;
        String errorMsg = "";

        // Validate meter roles
        if (meter0Role != "Grid" && meter0Role != "Solar" && meter0Role != "Consumer") {
            valid = false;
            errorMsg = "Invalid role selected for Meter 0.";
        }
        if (meter1Role != "Grid" && meter1Role != "Solar" && meter1Role != "Consumer") {
            valid = false;
            errorMsg = "Invalid role selected for Meter 1.";
        }
        if (meter2Role != "Grid" && meter2Role != "Solar" && meter2Role != "Consumer") {
            valid = false;
            errorMsg = "Invalid role selected for Meter 2.";
        }
        if (valid && (meter0Role == meter1Role || meter0Role == meter2Role || meter1Role == meter2Role)) {
            valid = false;
            errorMsg = "Selected roles must be unique for each meter.";
        }

        // Validate LED configuration
        int newLedCount = ledCountStr.toInt();
        int newLedPin = ledPinStr.toInt();
        uint32_t newLedType = (uint32_t)ledTypeStr.toInt();
        
        if (newLedCount < 1 || newLedCount > 300) {
            valid = false;
            errorMsg = "LED count must be between 1 and 300.";
        }
        
        if (newLedPin < 0 || newLedPin > 39) {
            valid = false;
            errorMsg = "LED pin must be between 0 and 39.";
        }
        
        // Additional GPIO validation for ESP32-C3
        if (valid && (newLedPin == 18 || newLedPin == 19)) {
            valid = false;
            errorMsg = "GPIO pins 18 and 19 are reserved for USB on ESP32-C3.";
        }
        
        // Validate LED type
        int ledTypeIndex = findLEDTypeIndex(newLedType);
        if (ledTypeIndex == 0 && newLedType != LED_TYPES[0].flags) {
            valid = false;
            errorMsg = "Invalid LED type selected.";
        }

        // Check mDNS name availability
        MDNS.end();
        if (!MDNS.begin(newSheMeterName)) {
            valid = false;
            errorMsg = "The name " + newSheMeterName + " is already in use on the network.";
        } else {
            MDNS.end();
        }

        if (valid) {
            newSSID.toCharArray(ssid, sizeof(ssid));
            newPassword.toCharArray(password, sizeof(password));
            newShellyIP.toCharArray(shellyIP, sizeof(shellyIP));
            newTimezone.toCharArray(timezone, sizeof(timezone));
            ShemeterName = newSheMeterName;
            fallbackSSID = ShemeterName + "AP";
            
            // Update timezone
            myTZ.setLocation(timezone);

            meters[0].name = meter0Role;
            meters[1].name = meter1Role;
            meters[2].name = meter2Role;

            // Update LED configuration
            LED_COUNT_var = newLedCount;
            LED_PIN_var = newLedPin;
            LED_TYPE_flags = newLedType;
            currentLEDTypeIndex = ledTypeIndex;
            invertStrip = ledInvert;

            // Reinitialize LED strip with new configuration
            initializeLEDStrip();

            if (startUniqueMDNS(ShemeterName)) {
                TIMED_PRINTLN("mDNS responder restarted with new ShemeterName.");
            } else {
                TIMED_PRINTLN("Failed to restart mDNS responder with new ShemeterName.");
            }

        saveConfig();
            TIMED_PRINTLN("Configuration updated via web interface.");
            server.sendHeader("Location", "/");
            server.send(303);
        } else {
            String html = "<!DOCTYPE html><html><body>";
            html += "<h1>Configuration Error</h1>";
            html += "<p>" + errorMsg + "</p>";
            html += "<a href=\"/config\">Go Back</a>";
            html += "</body></html>";
            server.send(400, "text/html", html);
        }
    } else {
        // Generate modern configuration form
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Configuration - Energy Monitor</title>";
        html += "<style>";
        html += "* { margin: 0; padding: 0; box-sizing: border-box; }";
        html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; padding: 20px; }";
        html += ".container { max-width: 800px; margin: 0 auto; }";
        html += "h1 { text-align: center; color: white; margin-bottom: 30px; font-size: 2.5em; font-weight: 300; text-shadow: 0 2px 4px rgba(0,0,0,0.3); }";
        html += ".card { background: rgba(255,255,255,0.95); border-radius: 15px; padding: 30px; margin-bottom: 20px; box-shadow: 0 8px 32px rgba(0,0,0,0.1); backdrop-filter: blur(10px); }";
        html += ".form-group { margin-bottom: 20px; }";
        html += ".form-group label { display: block; margin-bottom: 5px; font-weight: 600; color: #333; }";
        html += ".form-group input, .form-group select { width: 100%; padding: 12px; border: 2px solid #e1e5e9; border-radius: 8px; font-size: 16px; transition: border-color 0.3s; }";
        html += ".form-group input:focus, .form-group select:focus { outline: none; border-color: #667eea; }";
        html += ".form-row { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }";
        html += ".meter-config { background: #f8f9fa; padding: 20px; border-radius: 10px; margin-bottom: 15px; }";
        html += ".meter-title { font-weight: 600; color: #495057; margin-bottom: 10px; }";
        html += ".power-display { font-size: 18px; font-weight: 700; color: #28a745; margin-top: 5px; }";
        html += ".btn { padding: 12px 24px; border: none; border-radius: 8px; font-size: 16px; font-weight: 600; cursor: pointer; transition: all 0.3s ease; margin-right: 10px; margin-bottom: 10px; }";
        html += ".btn-primary { background: linear-gradient(45deg, #667eea, #764ba2); color: white; box-shadow: 0 4px 15px rgba(102, 126, 234, 0.4); }";
        html += ".btn-primary:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(102, 126, 234, 0.6); }";
        html += ".btn-secondary { background: #6c757d; color: white; }";
        html += ".btn-secondary:hover { background: #5a6268; transform: translateY(-2px); }";
        html += ".btn-danger { background: #dc3545; color: white; }";
        html += ".btn-danger:hover { background: #c82333; transform: translateY(-2px); }";
        html += ".btn-back { background: #17a2b8; color: white; text-decoration: none; display: inline-block; }";
        html += ".btn-back:hover { background: #138496; transform: translateY(-2px); }";
        html += ".section-title { font-size: 1.5em; font-weight: 600; color: #333; margin-bottom: 20px; padding-bottom: 10px; border-bottom: 2px solid #e9ecef; }";
        html += ".status-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin-bottom: 20px; }";
        html += ".status-item { background: #e9ecef; padding: 15px; border-radius: 8px; text-align: center; }";
        html += ".status-label { font-size: 14px; color: #6c757d; margin-bottom: 5px; }";
        html += ".status-value { font-size: 18px; font-weight: 600; color: #495057; }";
        html += "@media (max-width: 768px) { .form-row { grid-template-columns: 1fr; } .status-grid { grid-template-columns: 1fr; } }";
        html += "</style></head><body>";
        
        html += "<div class='container'>";
        html += "<h1>Energy Monitor Settings</h1>";
        
        // Back button
        html += "<div style='margin-bottom: 20px;'><a href='/' class='btn btn-back'>← Back to Dashboard</a></div>";
        
        // Current Status
        html += "<div class='card'>";
        html += "<div class='section-title'>Current Status</div>";
        html += "<div class='status-grid'>";
        html += "<div class='status-item'><div class='status-label'>Device IP</div><div class='status-value'>" + WiFi.localIP().toString() + "</div></div>";
        html += "<div class='status-item'><div class='status-label'>Device Name</div><div class='status-value'>" + ShemeterName + "</div></div>";
        html += "<div class='status-item'><div class='status-label'>Shelly IP</div><div class='status-value'>" + String(shellyIP) + "</div></div>";
        html += "<div class='status-item'><div class='status-label'>WiFi SSID</div><div class='status-value'>" + String(ssid) + "</div></div>";
        html += "</div></div>";
        
        // Configuration Form
        html += "<div class='card'>";
        html += "<div class='section-title'>Network Configuration</div>";
        html += "<form action='/config' method='post'>";

        html += "<div class='form-row'>";
        html += "<div class='form-group'><label>WiFi SSID</label><input type='text' name='ssid' value='" + String(ssid) + "' required></div>";
        html += "<div class='form-group'><label>WiFi Password</label><input type='password' name='password' value='" + String(password) + "'></div>";
        html += "</div>";
        
        html += "<div class='form-row'>";
        html += "<div class='form-group'><label>Shelly Device IP</label><input type='text' name='shellyIP' value='" + String(shellyIP) + "' placeholder='Auto-detected'></div>";
        html += "<div class='form-group'><label>Device Name</label><input type='text' name='shemeterName' value='" + String(ShemeterName) + "' required></div>";
        html += "</div>";
        
        html += "<div class='form-row'>";
        html += "<div class='form-group'><label>Timezone</label><select name='timezone' required>";
        int currentTzIndex = findTimezoneIndex(timezone);
        for (int i = 0; i < TIMEZONES_COUNT; i++) {
            html += "<option value='" + String(TIMEZONES[i].olson) + "'" + (i == currentTzIndex ? " selected" : "") + ">" + String(TIMEZONES[i].name) + "</option>";
        }
        html += "</select></div>";
        html += "</div>";
        
        html += "<div class='section-title' style='margin-top: 30px;'>Meter Configuration</div>";
        for (int i = 0; i < 3; i++) {
            html += "<div class='meter-config'>";
            html += "<div class='meter-title'>Meter " + String(i) + " Configuration</div>";
            html += "<div class='form-group'>";
            html += "<label>Role</label>";
            html += "<select name='meter" + String(i) + "' required>";
            html += "<option value='Grid'" + String(meters[i].name.equalsIgnoreCase("Grid") ? " selected" : "") + ">Grid Connection</option>";
            html += "<option value='Solar'" + String(meters[i].name.equalsIgnoreCase("Solar") ? " selected" : "") + ">Solar Generation</option>";
            html += "<option value='Consumer'" + String(meters[i].name.equalsIgnoreCase("Consumer") ? " selected" : "") + ">Consumer Load</option>";
            html += "</select>";
            html += "</div>";
            html += "<div class='power-display'>Current: " + String(meters[i].act_power) + " W</div>";
            html += "</div>";
        }
        
        html += "<div class='section-title' style='margin-top: 30px;'>LED Strip Configuration</div>";
        html += "<div class='info-box' style='background: #e8f4f8; padding: 15px; border-radius: 8px; margin-bottom: 20px; border-left: 4px solid #2196F3;'>";
        html += "<strong>LED Configuration Tips:</strong><br>";
        html += "• Most common: WS2812B (RGB) or WS2812B (RGBW) for addressable LED strips<br>";
        html += "• GPIO pins 18-19 are reserved for USB on ESP32-C3<br>";
        html += "• Recommended pins: 2, 4, 5, 6, 7, 8, 10 (avoid ADC pins for better performance)<br>";
        html += "• Maximum 300 LEDs supported (memory limitation)";
        html += "</div>";
        
        html += "<div class='form-row'>";
        html += "<div class='form-group'><label>LED Count <span style='color: #666; font-size: 0.9em;'>(1-300)</span></label><input type='number' name='ledCount' value='" + String(LED_COUNT_var) + "' min='1' max='300' required></div>";
        html += "<div class='form-group'><label>LED Pin <span style='color: #666; font-size: 0.9em;'>(GPIO 0-39)</span></label><input type='number' name='ledPin' value='" + String(LED_PIN_var) + "' min='0' max='39' required></div>";
        html += "</div>";
        
        html += "<div class='form-row'>";
        html += "<div class='form-group'><label>LED IC Type</label><select name='ledType' required>";
        for (int i = 0; i < LED_TYPES_COUNT; ++i) {
            html += "<option value='" + String(LED_TYPES[i].flags) + "'" + (LED_TYPE_flags == LED_TYPES[i].flags ? " selected" : "") + ">" + String(LED_TYPES[i].description) + "</option>";
        }
        html += "</select></div>";
        html += "<div class='form-group' style='display: flex; align-items: center; padding-top: 30px;'>";
        html += "<input type='checkbox' name='invertStrip'" + String(invertStrip ? " checked" : "") + " style='width: auto; margin-right: 10px;'>";
        html += "<label style='margin: 0;'>Invert LED Strip Direction</label>";
        html += "</div>";
        html += "</div>";
        
        html += "<div class='current-config' style='background: #f5f5f5; padding: 15px; border-radius: 8px; margin-top: 15px;'>";
        html += "<strong>Current LED Configuration:</strong><br>";
        html += "Type: " + getLEDTypeName(LED_TYPE_flags) + " | Count: " + String(LED_COUNT_var) + " | Pin: GPIO" + String(LED_PIN_var) + " | Inverted: " + String(invertStrip ? "Yes" : "No");
        html += "</div>";
        
        html += "<div style='margin-top: 30px;'>";
        html += "<button type='submit' class='btn btn-primary'>Save Configuration</button>";
        html += "</div>";
        html += "</form>";
        html += "</div>";
        
        // Configuration Backup/Restore
        html += "<div class='card'>";
        html += "<div class='section-title'>Configuration Backup</div>";
        html += "<div style='margin-bottom: 15px; padding: 10px; background: #fff3cd; border-left: 4px solid #ffc107; font-size: 0.9em;'>";
        html += "<strong>⚠️ Tip:</strong> Settings now live in NVS (survives uploadfs). Still create a backup before factory reset or migrating to another device.";
        html += "</div>";
        html += "<button type='button' class='btn btn-secondary' onclick='downloadConfig()' style='margin-right: 10px;'>Download Config Backup</button>";
        html += "<button type='button' class='btn btn-secondary' onclick='document.getElementById(\"configFile\").click()'>Restore Config from Backup</button>";
        html += "<input type='file' id='configFile' accept='.json' style='display:none' onchange='uploadConfig(this.files[0])'>";
        html += "<div id='configStatus' style='margin-top: 10px; font-size: 0.9em;'></div>";
        html += "</div>";
        
        // System Actions
        html += "<div class='card'>";
        html += "<div class='section-title'>System Actions</div>";
        html += "<button type='button' class='btn btn-secondary' onclick='testLEDs()' style='margin-right: 10px;'>Test LED Strip</button>";
        html += "<button type='button' class='btn btn-secondary' onclick='checkDebugData()' style='margin-right: 10px;'>Debug Data</button>";
        html += "<form action='/ota' method='post' style='display: inline-block;'>";
        html += "<button type='submit' class='btn btn-secondary'>Check for Updates</button>";
        html += "</form>";
        html += "<form action='/factoryReset' method='post' style='display: inline-block;' onsubmit='return confirm(\"Are you sure you want to reset all settings?\");'>";
        html += "<button type='submit' class='btn btn-danger'>Factory Reset</button>";
        html += "</form>";
        html += "<div style='margin-top: 15px; padding: 10px; background: #f8f9fa; border-left: 4px solid #0066cc; font-size: 0.9em;'>";
        html += "<strong>💡 Tip:</strong> You can also factory reset by holding the BOOT button (GPIO9) for 5 seconds. The LED will blink red during the hold.";
        html += "</div>";
        html += "</div>";

        html += "</div>";
        
        // Add the debug function JavaScript
        html += "<script>";
        html += "function testLEDs() {";
        html += "  fetch('/testLEDs')";
        html += "    .then(response => response.text())";
        html += "    .then(data => {";
        html += "      alert('LED Test: ' + data);";
        html += "    })";
        html += "    .catch(error => {";
        html += "      alert('LED Test failed: ' + error);";
        html += "    });";
        html += "}";
        html += "function checkDebugData() {";
        html += "  console.log('=== DEBUG DATA CHECK ===');";
        html += "  Promise.all([";
        html += "    fetch('/debug').then(r => r.json()),";
        html += "    fetch('/time').then(r => r.json())";
        html += "  ]).then(([debugData, timeData]) => {";
        html += "    console.log('Debug data:', debugData);";
        html += "    console.log('Time sync data:', timeData);";
        html += "    const browserTime = Date.now();";
        html += "    const deviceTime = timeData.currentDeviceTime;";
        html += "    const timeDiff = browserTime - deviceTime;";
        html += "    console.log('Browser time:', new Date(browserTime).toISOString());";
        html += "    console.log('Device time:', new Date(deviceTime).toISOString());";
        html += "    console.log('Time difference:', timeDiff + 'ms');";
        html += "    console.log('Device uptime:', debugData.deviceTime + 'ms');";
        html += "    console.log('WebSocket connected:', debugData.wsConnected);";
        html += "    console.log('Current meter readings:');";
        html += "    debugData.meters.forEach(meter => {";
        html += "      console.log('  ' + meter.name + ': ' + meter.power + 'W');";
        html += "    });";
        html += "    alert('Debug data logged to console. Press F12 to view.');";
        html += "  }).catch(error => {";
        html += "    console.error('Debug data fetch error:', error);";
        html += "    alert('Debug data fetch failed. Check console for details.');";
        html += "  });";
        html += "}";
        html += "function downloadConfig() {";
        html += "  const link = document.createElement('a');";
        html += "  link.href = '/config/download';";
        html += "  link.download = 'config_backup.json';";
        html += "  document.body.appendChild(link);";
        html += "  link.click();";
        html += "  document.body.removeChild(link);";
        html += "  document.getElementById('configStatus').innerHTML = '<span style=\"color:#28a745\">✓ Config downloaded successfully</span>';";
        html += "  setTimeout(() => document.getElementById('configStatus').innerHTML = '', 3000);";
        html += "}";
        html += "function uploadConfig(file) {";
        html += "  if (!file) return;";
        html += "  const reader = new FileReader();";
        html += "  reader.onload = function(e) {";
        html += "    fetch('/config/upload', {";
        html += "      method: 'POST',";
        html += "      headers: {'Content-Type': 'application/json'},";
        html += "      body: e.target.result";
        html += "    })";
        html += "    .then(response => response.json())";
        html += "    .then(data => {";
        html += "      if (data.success) {";
        html += "        document.getElementById('configStatus').innerHTML = '<span style=\"color:#28a745\">✓ ' + data.message + '</span>';";
        html += "        setTimeout(() => location.reload(), 2000);";
        html += "      } else {";
        html += "        document.getElementById('configStatus').innerHTML = '<span style=\"color:#dc3545\">✗ ' + data.message + '</span>';";
        html += "      }";
        html += "    })";
        html += "    .catch(error => {";
        html += "      document.getElementById('configStatus').innerHTML = '<span style=\"color:#dc3545\">✗ Upload failed: ' + error + '</span>';";
        html += "    });";
        html += "  };";
        html += "  reader.readAsText(file);";
        html += "}";
        html += "</script>";

        html += "</body></html>";
        server.send(200, "text/html", html);
    }
}

void streamAggregatedHistory(AggregatedPoint* buffer, int maxRecords, int currentIndex, bool filled, uint32_t durationSeconds) {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    server.sendContent("[");
    
    bool first = true;
    int total = filled ? maxRecords : currentIndex;
    int startIndex = filled ? currentIndex : 0;
    int processed = 0;
    
    for (int i = 0; i < total; i++) {
        int bufferIndex = (startIndex + i) % maxRecords;
        AggregatedPoint &point = buffer[bufferIndex];
        if (point.timestamp == 0) {
            continue;
        }
        if (!first) {
            server.sendContent(",");
        }
        first = false;
        
        String timestampStr = formatTimestampString(point.timestamp);
        String objStr = "{\"timestamp\":\"" + timestampStr +
                       "\",\"duration\":" + String(durationSeconds) +
                       ",\"Grid\":" + String(point.grid) +
                       ",\"Solar\":" + String(point.solar) +
                       ",\"Consumer\":" + String(point.consumer) + "}";
        server.sendContent(objStr);
        processed++;
        
        if (processed % 20 == 0) {
            esp_task_wdt_reset();
            yield();
        }
    }
    
    server.sendContent("]");
}

void sendHistoryHeaders() {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.sendHeader("Access-Control-Allow-Origin", "*");
}

void handleHistory() {
    sendHistoryHeaders();
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    server.sendContent("[");
    
    bool first = true;
    int total = shortHistoryFilled ? SHORT_HISTORY_POINTS : shortHistoryIndex;
    int startIndex = shortHistoryFilled ? shortHistoryIndex : 0;
    int processed = 0;
    
    TIMED_PRINTLN("Streaming " + String(total) + " short history points to client...");
    
    for (int i = 0; i < total; i++) {
        int bufferIndex = (startIndex + i) % SHORT_HISTORY_POINTS;
        DataPoint &dp = shortHistory[bufferIndex];
        if (dp.timestamp == 0) {
            continue;
        }
        
        if (!first) {
            server.sendContent(",");
        }
        first = false;
        
        String timestampStr = formatTimestampString(dp.timestamp);
        String objStr = "{\"timestamp\":\"" + timestampStr +
                       "\",\"Grid\":" + String(dp.grid) +
                       ",\"Solar\":" + String(dp.solar) +
                       ",\"Consumer\":" + String(dp.consumer) + "}";
        server.sendContent(objStr);
        processed++;
        
        if (processed % 50 == 0) {
            esp_task_wdt_reset();
            yield();
        }
    }
    
    server.sendContent("]");
    TIMED_PRINTLN("Short history streaming completed: " + String(processed) + " points sent");
}

void handleHistory24h() {
    sendHistoryHeaders();
    streamAggregatedHistory(history24h, HISTORY_24H_BUCKETS, history24hIndex, history24hFilled, HISTORY_24H_BUCKET_SECONDS);
    TIMED_PRINTLN("24h history served");
}

void handleHistory30d() {
    sendHistoryHeaders();
    streamAggregatedHistory(history30d, HISTORY_30D_BUCKETS, history30dIndex, history30dFilled, HISTORY_30D_BUCKET_SECONDS);
    TIMED_PRINTLN("30d history served");
}

// Web server setup
void setupWebServer() {
    // WiFi configuration portal routes (always available)
    server.on("/wificonfig", handleWiFiConfig);
    server.on("/wifiscan", handleWiFiScan);
    server.on("/wifisave", HTTP_POST, handleWiFiSave);
    
    // Captive portal detection URLs - redirect to config
    // These are requested by various devices to detect captive portals
    server.on("/generate_204", handleCaptivePortal);          // Android
    server.on("/gen_204", handleCaptivePortal);               // Android
    server.on("/hotspot-detect.html", handleCaptivePortal);   // iOS/macOS
    server.on("/canonical.html", handleCaptivePortal);        // Ubuntu
    server.on("/connecttest.txt", handleCaptivePortal);       // Windows
    server.on("/redirect", handleCaptivePortal);              // Windows
    server.on("/success.txt", handleCaptivePortal);           // Firefox
    server.on("/ncsi.txt", handleCaptivePortal);              // Windows NCSI
    
    // Main routes
    server.on("/", handleRoot);
    server.on("/data", handleJson);
    server.on("/config", handleConfig);
    server.on("/history", HTTP_GET, handleHistory);
    server.on("/history/24h", HTTP_GET, handleHistory24h);
    server.on("/history/30d", HTTP_GET, handleHistory30d);
    
    // Captive portal - catch all unknown requests
    server.onNotFound(handleCaptivePortal);
    
    // Lightweight metrics endpoint for frequent polling (reduced payload)
    server.on("/metrics", []() {
        server.sendHeader("Cache-Control", "no-cache, max-age=0");
        server.sendHeader("Access-Control-Allow-Origin", "*");
        
        // Minimal JSON for real-time updates
        String response = "{\"meters\":[";
        for (int i = 0; i < 3; i++) {
            if (i > 0) response += ",";
            response += "{\"name\":\"" + meters[i].name + "\",\"power\":" + String(meters[i].act_power) + "}";
        }
        response += "]}";
        
        server.send(200, "application/json", response);
    });
    
    // Simple test endpoint
    server.on("/test", []() {
        server.send(200, "text/plain", "Web server is working! Device: " + ShemeterName + " | IP: " + WiFi.localIP().toString());
    });
    
    // Time synchronization endpoint
    server.on("/time", []() {
        StaticJsonDocument<JSON_CAPACITY_MEDIUM> doc;
        doc["deviceMillis"] = millis();
        doc["deviceStartTime"] = deviceStartTimeMillis;
        doc["currentDeviceTime"] = deviceStartTimeMillis + millis();
        
        time_t now;
        time(&now);
        doc["unixTime"] = now;
        
        // Add ezTime information
        unsigned long uptimeSeconds = millis() / 1000;
        doc["uptime"] = formatUptime(uptimeSeconds);
        doc["uptimeSeconds"] = uptimeSeconds;
        doc["timezone"] = String(timezone);
        doc["timezoneDisplay"] = getTimezoneDisplayName(timezone);
        doc["timeInitialized"] = timeInitialized;
        
        if (timeInitialized) {
            doc["currentTime"] = myTZ.dateTime("Y-m-d H:i:s T");
            doc["utcTime"] = UTC.dateTime("Y-m-d H:i:s");
            doc["epoch"] = myTZ.now();
        }
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    
    // Debug endpoint to check current data and timing
    server.on("/debug", []() {
        StaticJsonDocument<JSON_CAPACITY_MEDIUM> doc;
        doc["deviceTime"] = millis();
        doc["deviceStartTime"] = deviceStartTimeMillis;
        doc["currentTimestamp"] = deviceStartTimeMillis + millis();
        doc["lastDataUpdate"] = lastDataUpdateTime;
        doc["dataUpdateInterval"] = dataUpdateInterval;
        doc["wsConnected"] = wsClient.available();
        doc["rpcInProgress"] = rpcInProgress;
        doc["newDataAvailable"] = newDataAvailable;
        
        // Add uptime information
        unsigned long uptimeSeconds = millis() / 1000;
        doc["uptime"] = formatUptime(uptimeSeconds);
        doc["uptimeSeconds"] = uptimeSeconds;
        
        // Add timezone and time information
        doc["timezone"] = String(timezone);
        doc["timezoneDisplay"] = getTimezoneDisplayName(timezone);
        doc["timeInitialized"] = timeInitialized;
        if (timeInitialized) {
            doc["currentTime"] = myTZ.dateTime("Y-m-d H:i:s T");
            doc["utcTime"] = UTC.dateTime("Y-m-d H:i:s");
        }
        
        JsonArray metersArray = doc["meters"].to<JsonArray>();
        for (int i = 0; i < 3; i++) {
            JsonObject meter = metersArray.add<JsonObject>();
            meter["name"] = meters[i].name;
            meter["power"] = meters[i].act_power;
            meter["lastUpdate"] = meters[i].lastUpdateTime;
        }
        
        // Add recent history points (for quick chart load)
        JsonArray historyArray = doc["recentHistory"].to<JsonArray>();
        int available = shortHistoryFilled ? SHORT_HISTORY_POINTS : shortHistoryIndex;
        int sendCount = min(available, 60);
        if (sendCount > 0) {
            int start = shortHistoryFilled ? shortHistoryIndex : 0;
            int begin = (start - sendCount + SHORT_HISTORY_POINTS) % SHORT_HISTORY_POINTS;
            for (int i = 0; i < sendCount; i++) {
                int idx = (begin + i) % SHORT_HISTORY_POINTS;
                DataPoint &pointData = shortHistory[idx];
                if (pointData.timestamp == 0) {
                    continue;
                }
                JsonObject point = historyArray.add<JsonObject>();
                point["timestamp"] = static_cast<uint64_t>(pointData.timestamp) * 1000ULL;
                point["grid"] = pointData.grid;
                point["solar"] = pointData.solar;
                point["consumer"] = pointData.consumer;
            }
        }
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    
    server.on("/ota", HTTP_POST, []() {
        server.send(200, "text/plain", "OTA update check initiated. Check serial monitor for progress.");
        handleOTA();
    });
    
    server.on("/testLEDs", HTTP_GET, []() {
        if (!strip) {
            server.send(500, "text/plain", "LED strip not initialized");
            return;
        }
        
        TIMED_PRINTLN("Testing LED strip...");
        
        // Test sequence: Red, Green, Blue, White (if supported), then clear
        uint32_t colors[] = {
            strip->Color(255, 0, 0),    // Red
            strip->Color(0, 255, 0),    // Green  
            strip->Color(0, 0, 255),    // Blue
            strip->Color(255, 255, 255) // White
        };
        
        for (int c = 0; c < 4; c++) {
            strip->fill(colors[c]);
            strip->show();
            delay(500);
        }
        
        // Clear the strip
        strip->clear();
        strip->show();
        
        String response = "LED test completed successfully. ";
        response += "Type: " + getLEDTypeName(LED_TYPE_flags) + ", ";
        response += "Count: " + String(LED_COUNT_var) + ", ";
        response += "Pin: GPIO" + String(LED_PIN_var);
        
        server.send(200, "text/plain", response);
        TIMED_PRINTLN("LED test completed");
    });
    
    server.on("/factoryReset", HTTP_POST, []() {
        TIMED_PRINTLN("Factory reset requested");
        logSystemEvent("RESET", "Factory reset requested via web endpoint");
        
        // Clear NVS (primary storage)
        if (preferences.begin(NVS_NAMESPACE, false)) {
            preferences.clear();
            preferences.end();
            TIMED_PRINTLN("NVS cleared");
        }
        
        TIMED_PRINTLN("Factory reset performed. All configuration cleared.");
        server.sendHeader("Location", "/config");
        server.send(303);
        delay(1000);
        ESP.restart();
    });
    
    // Config backup endpoint - download current configuration snapshot
    server.on("/config/download", HTTP_GET, []() {
        StaticJsonDocument<JSON_CAPACITY_CONFIG> doc;
        doc["ssid"] = ssid;
        doc["password"] = password;
        doc["shellyIP"] = shellyIP;
        doc["shemeterName"] = ShemeterName;
        doc["timezone"] = timezone;
        doc["ledCount"] = LED_COUNT_var;
        doc["ledPin"] = LED_PIN_var;
        doc["ledType"] = LED_TYPE_flags;
        doc["invertStrip"] = invertStrip;

        JsonArray meterArray = doc["meters"].to<JsonArray>();
        for (int i = 0; i < 3; i++) {
            meterArray.add(meters[i].name);
        }

        String serialized;
        serializeJson(doc, serialized);

        server.sendHeader("Content-Disposition", "attachment; filename=config.json");
        server.send(200, "application/json", serialized);
        TIMED_PRINTLN("Config downloaded from NVS snapshot");
    });
    
    // Config restore endpoint - upload config.json
    server.on("/config/upload", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
            return;
        }

        StaticJsonDocument<JSON_CAPACITY_CONFIG> doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));

        if (error) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
            return;
        }

        if (doc["ssid"].is<const char*>()) {
            strncpy(ssid, doc["ssid"], sizeof(ssid) - 1);
            ssid[sizeof(ssid) - 1] = '\0';
        }
        if (doc["password"].is<const char*>()) {
            strncpy(password, doc["password"], sizeof(password) - 1);
            password[sizeof(password) - 1] = '\0';
        }
        if (doc["shellyIP"].is<const char*>()) {
            strncpy(shellyIP, doc["shellyIP"], sizeof(shellyIP) - 1);
            shellyIP[sizeof(shellyIP) - 1] = '\0';
        }
        if (doc["timezone"].is<const char*>()) {
            strncpy(timezone, doc["timezone"], sizeof(timezone) - 1);
            timezone[sizeof(timezone) - 1] = '\0';
        }
        if (doc["shemeterName"].is<const char*>()) {
            ShemeterName = String(doc["shemeterName"].as<const char*>());
            fallbackSSID = ShemeterName + "AP";
        }
        if (doc["ledCount"].is<int>()) LED_COUNT_var = doc["ledCount"];
        if (doc["ledPin"].is<int>()) LED_PIN_var = doc["ledPin"];
        if (doc["ledType"].is<uint32_t>()) {
            LED_TYPE_flags = doc["ledType"];
            currentLEDTypeIndex = findLEDTypeIndex(LED_TYPE_flags);
        }
        if (doc["invertStrip"].is<bool>()) invertStrip = doc["invertStrip"];

        if (doc["meters"].is<JsonArray>()) {
            JsonArray meterArray = doc["meters"].as<JsonArray>();
            int index = 0;
            for (JsonVariant meterName : meterArray) {
                if (index < 3 && meterName.is<const char*>()) {
                    meters[index].name = String(meterName.as<const char*>());
                    index++;
                }
            }
            while (index < 3) {
                if (index == 0) meters[index].name = "Grid";
                else if (index == 1) meters[index].name = "Solar";
                else meters[index].name = "Consumer";
                index++;
            }
        }

        validateConfig();

        if (saveConfig()) {
            TIMED_PRINTLN("Config restored from upload to NVS");
            logSystemEvent("CONFIG", "Configuration restored from uploaded backup");
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Config restored. Device will reboot.\"}");
            delay(500);
            ESP.restart();
            return;
        } else {
            logSystemEvent("ERROR", "Failed to persist uploaded configuration");
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Failed to save config\"}");
        }
    });
    
    // Debug endpoint to check LittleFS contents
    server.on("/listfiles", []() {
        if (!ensureLittleFS()) {
            server.send(500, "text/plain", "LittleFS not mounted");
            return;
        }
        
        String output = "LittleFS Contents:\n\n";
        File root = LittleFS.open("/");
        File file = root.openNextFile();
        while (file) {
            output += String(file.isDirectory() ? "DIR: " : "FILE: ");
            output += String(file.name());
            if (!file.isDirectory()) {
                output += " (" + String(file.size()) + " bytes)";
            }
            output += "\n";
            file = root.openNextFile();
        }
        
        // List JS directory specifically
        output += "\n/js/ Directory:\n";
        File jsDir = LittleFS.open("/js");
        if (jsDir && jsDir.isDirectory()) {
            File jsFile = jsDir.openNextFile();
            while (jsFile) {
                output += "  FILE: " + String(jsFile.name()) + " (" + String(jsFile.size()) + " bytes)\n";
                jsFile = jsDir.openNextFile();
            }
        } else {
            output += "  /js/ directory not found!\n";
        }
        
        server.send(200, "text/plain", output);
    });
    
    // Structured log listing endpoint
    server.on("/logs/list", handleLogsList);
    
    // Setup LittleFS and serve files
    if (ensureLittleFS()) {
        TIMED_PRINTLN("LittleFS mounted for static file serving");
        
        // Explicit handlers for JavaScript files with proper MIME type
        server.on("/js/moment.min.js", []() {
            TIMED_PRINTLN("Serving /js/moment.min.js");
            File file = LittleFS.open("/js/moment.min.js", "r");
            if (!file) {
                TIMED_PRINTLN("ERROR: moment.min.js not found!");
                server.send(404, "text/plain", "moment.min.js not found");
                return;
            }
            server.streamFile(file, "application/javascript");
            file.close();
        });
        
        server.on("/js/chart.min.js", []() {
            TIMED_PRINTLN("Serving /js/chart.min.js");
            File file = LittleFS.open("/js/chart.min.js", "r");
            if (!file) {
                TIMED_PRINTLN("ERROR: chart.min.js not found!");
                server.send(404, "text/plain", "chart.min.js not found");
                return;
            }
            server.streamFile(file, "application/javascript");
            file.close();
        });
        
        server.on("/js/chartjs-adapter-moment.min.js", []() {
            TIMED_PRINTLN("Serving /js/chartjs-adapter-moment.min.js");
            File file = LittleFS.open("/js/chartjs-adapter-moment.min.js", "r");
            if (!file) {
                TIMED_PRINTLN("ERROR: chartjs-adapter-moment.min.js not found!");
                server.send(404, "text/plain", "chartjs-adapter-moment.min.js not found");
                return;
            }
            server.streamFile(file, "application/javascript");
            file.close();
        });
        
        server.on("/js/chartjs-plugin-zoom.min.js", []() {
            TIMED_PRINTLN("Serving /js/chartjs-plugin-zoom.min.js");
            File file = LittleFS.open("/js/chartjs-plugin-zoom.min.js", "r");
            if (!file) {
                TIMED_PRINTLN("ERROR: chartjs-plugin-zoom.min.js not found!");
                server.send(404, "text/plain", "chartjs-plugin-zoom.min.js not found");
                return;
            }
            server.streamFile(file, "application/javascript");
            file.close();
        });
        
        // Fallback: General static file serving with automatic MIME types
        server.serveStatic("/js/", LittleFS, "/js/");
        server.serveStatic("/", LittleFS, "/");
    } else {
        TIMED_PRINTLN("LittleFS mount failed for static serving");
    }
    
    server.begin();
    TIMED_PRINTLN("HTTP server started on port 80");
    if (WiFi.status() == WL_CONNECTED) {
        TIMED_PRINTLN("Web interface available at: http://" + WiFi.localIP().toString());
        TIMED_PRINTLN("Configuration page: http://" + WiFi.localIP().toString() + "/config");
        TIMED_PRINTLN("Test page: http://" + WiFi.localIP().toString() + "/test");
        } else {
        TIMED_PRINTLN("AP mode - Web interface at: http://" + WiFi.softAPIP().toString());
        TIMED_PRINTLN("Configuration page: http://" + WiFi.softAPIP().toString() + "/config");
    }
}

// RPC and WebSocket management
int sendRequest(const char* method, JsonVariant params, std::function<void(JsonObject&)> callback) {
    commandId++;
        StaticJsonDocument<JSON_CAPACITY_MEDIUM> doc;
    
    doc["jsonrpc"] = "2.0";
    doc["id"] = commandId;
    doc["src"] = "arduino_client";
    doc["method"] = method;
    
    if (!params.isNull()) {
        doc["params"] = params;
    }
    
    String request;
    serializeJson(doc, request);
    TIMED_PRINTLN("Sending RPC Request (ID: " + String(commandId) + "): " + request);
    
    pendingRequests[commandId] = { callback, millis() };
    wsClient.send(request);
    lastWSActivity = millis();
    
    return commandId;
}

void updateMeterActPower(int meterIndex, int newPower) {
    if (meterIndex < 0 || meterIndex >= 3) return;
    
    if (meters[meterIndex].name.equalsIgnoreCase("Solar")) {
        newPower = -newPower;
    }
    
    int oldPower = meters[meterIndex].act_power;
    if (oldPower != newPower) {
        meters[meterIndex].act_power = newPower;
        meters[meterIndex].lastUpdateTime = millis();
        newDataAvailable = true;
        TIMED_PRINTLN("Updated " + meters[meterIndex].name + " act_power: " + String(oldPower) + "W -> " + String(newPower) + "W");
    } else {
        TIMED_PRINTLN("No change in " + meters[meterIndex].name + " act_power: " + String(newPower) + "W");
    }
}

void sendShellyGetStatus() {
    if (rpcInProgress) return;
    rpcInProgress = true;
    
    StaticJsonDocument<JSON_CAPACITY_SMALL> params;
    sendRequest("Shelly.GetStatus", params.as<JsonVariant>(), [](JsonObject& response) {
        TIMED_PRINTLN("Shelly.GetStatus response received.");
        rpcInProgress = false;

        uint32_t shellyTimestamp = extractShellyTimestamp(response);
        if (response["result"].is<JsonObject>()) {
            JsonObject result = response["result"];
            
            // Support both triphase and monophase profiles
            if (result["em:0"].is<JsonObject>()) {
                // Triphase profile - single EM component
                JsonObject em = result["em:0"];
                updateMeterActPower(0, (int)(em["a_act_power"] | 0.0));
                updateMeterActPower(1, (int)(em["b_act_power"] | 0.0));
                updateMeterActPower(2, (int)(em["c_act_power"] | 0.0));
                TIMED_PRINTLN("Updated from triphase profile");
            } else {
                // Monophase profile - three EM1 components
        for (int i = 0; i < 3; ++i) {
            String key = "em1:" + String(i);
                    if (result[key].is<JsonObject>()) {
                        JsonObject meter = result[key];
                        float act_power = meter["act_power"] | 0.0;
                        updateMeterActPower(i, (int)act_power);
                    }
                }
                TIMED_PRINTLN("Updated from monophase profile");
            }
            
        storeDataPoint(shellyTimestamp);
        }
    });
}

void onMessageCallback(WebsocketsMessage message) {
    lastWSActivity = millis();
    TIMED_PRINTLN("Received WebSocket message:");
    TIMED_PRINTLN(message.data());

    static StaticJsonDocument<JSON_CAPACITY_LARGE> doc;
    doc.clear();
    DeserializationError error = deserializeJson(doc, message.data());
    
    if (error) {
        TIMED_PRINTLN(String("Failed to parse response: ") + error.c_str());
        rpcInProgress = false;
        return;
    }

    if (doc["method"].is<const char*>()) {
        String method = doc["method"];
        if (method == "NotifyStatus") {
            JsonObject params = doc["params"];
            
            // Support both triphase and monophase profiles
            if (params["em:0"].is<JsonObject>()) {
                // Triphase profile
                JsonObject em = params["em:0"];
                updateMeterActPower(0, (int)(em["a_act_power"] | 0.0));
                updateMeterActPower(1, (int)(em["b_act_power"] | 0.0));
                updateMeterActPower(2, (int)(em["c_act_power"] | 0.0));
            } else {
                // Monophase profile
            for (int i = 0; i < 3; ++i) {
                String key = "em1:" + String(i);
                    if (params[key].is<JsonObject>()) {
                        JsonObject meter = params[key];
                    float act_power = meter["act_power"] | 0.0;
                    updateMeterActPower(i, (int)act_power);
                    }
                }
            }
            uint32_t shellyTimestamp = extractShellyTimestamp(doc);
            storeDataPoint(shellyTimestamp);
        }
    } else if (doc["id"].is<int>()) {
        int id = doc["id"];
        if (pendingRequests.find(id) != pendingRequests.end()) {
            JsonObject response = doc.as<JsonObject>();
            pendingRequests[id].callback(response);
            pendingRequests.erase(id);
        }
    }

    if (!inSetup) {
        flickStatusLED();
    }
}

void handleWebSocketEvent(WebsocketsEvent event, String data) {
    switch (event) {
    case WebsocketsEvent::ConnectionOpened:
        TIMED_PRINTLN("WebSocket connection opened.");
        lastWSActivity = millis();
        break;
    case WebsocketsEvent::ConnectionClosed:
        TIMED_PRINTLN("WebSocket connection closed.");
        cleanupPendingRequests();
        break;
    case WebsocketsEvent::GotPing:
        TIMED_PRINTLN("WebSocket ping received. Replied Pong.");
        wsClient.pong();
        lastWSActivity = millis();
        break;
    case WebsocketsEvent::GotPong:
        TIMED_PRINTLN("WebSocket pong received.");
        lastWSActivity = millis();
        break;
    }
}

bool startUniqueMDNS(String& name) {
    int suffix = 0;
    bool started = false;
    String baseName = name;
    
    while (!started && suffix < 100) {
        String uniqueName = baseName;
        if (suffix > 0) {
            uniqueName += String(suffix);
        }
        if (MDNS.begin(uniqueName)) {
            name = uniqueName;
            TIMED_PRINTLN("mDNS responder started as " + uniqueName + ".local");
            saveConfig();
            started = true;
        } else {
            TIMED_PRINTLN("mDNS name " + uniqueName + " is already in use. Trying " + String(suffix + 1));
            suffix++;
        }
    }
    
    if (!started) {
        TIMED_PRINTLN("Failed to start mDNS responder with a unique name after " + String(suffix) + " attempts.");
    }
    return started;
}

void checkAndEstablishWebSocket() {
    // Don't attempt WebSocket operations if WiFi isn't connected
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    
    unsigned long now = millis();
    if (now - lastWebSocketAttempt < webSocketRetryInterval) {
        return;
    }
    lastWebSocketAttempt = now;

    checkWiFiConnection();
    
    bool connectedThisAttempt = false;
    bool hasStoredIP = (strlen(shellyIP) > 0 && isValidShellyIP(shellyIP));
    
    if (hasStoredIP) {
        IPAddress storedIP;
        storedIP.fromString(shellyIP);
        bool pingOk = Ping.ping(storedIP, 1);
        TIMED_PRINTLN(pingOk ? "Stored Shelly IP responded to ping." : "Stored Shelly IP did not respond to ping.");
        String wsUrl = String("ws://") + shellyIP + "/rpc";
        TIMED_PRINTLN("Attempting WebSocket at stored IP: " + wsUrl);
        if (wsClient.connect(wsUrl)) {
            TIMED_PRINTLN("Connected to Shelly WebSocket using stored IP.");
            sendShellyGetStatus();
            connectedThisAttempt = true;
            shellyRediscoveryNeeded = false;
            shellyPingFailureCount = 0;
            lastSuccessfulShellyPing = millis();
        } else {
            TIMED_PRINTLN("Failed to connect using stored Shelly IP.");
            shellyRediscoveryNeeded = true;
        }
    }

    bool shouldDiscover = !connectedThisAttempt && (shellyRediscoveryNeeded || !hasStoredIP);
    if (shouldDiscover) {
        discoverShellyDevices();
        
        for (auto& device : shellyDevices) {
            if (device.type == "3EM") {
                device.ip.toCharArray(shellyIP, sizeof(shellyIP));
                String wsUrl = String("ws://") + shellyIP + "/rpc";
                TIMED_PRINTLN("Connecting to " + device.type + " at: " + wsUrl);
                
                if (wsClient.connect(wsUrl)) {
                    TIMED_PRINTLN("Connected to " + device.type + " WebSocket after discovery.");
                    device.isActive = true;
                    if (shellyDevices.size() == 1) {
                        saveConfig();
                    }
                    sendShellyGetStatus();
                    connectedThisAttempt = true;
                    shellyRediscoveryNeeded = false;
                    shellyPingFailureCount = 0;
                    lastSuccessfulShellyPing = millis();
                    break;
                }
            }
        }
    }
    
    if (!connectedThisAttempt) {
        TIMED_PRINTLN("No valid Shelly 3EM device found or connection failed.");
    }
}

void addToShortHistory(uint32_t timestampEpoch, int grid, int solar, int consumer) {
    if (timestampEpoch == 0) {
        return;
    }
    if (lastShortHistoryTimestamp != 0 && timestampEpoch < lastShortHistoryTimestamp) {
        TIMED_PRINTLN("Skipping out-of-order short history sample");
        return;
    }
    DataPoint &dp = shortHistory[shortHistoryIndex];
    dp.timestamp = timestampEpoch;
    dp.grid = grid;
    dp.solar = solar;
    dp.consumer = consumer;
    
    shortHistoryIndex = (shortHistoryIndex + 1) % SHORT_HISTORY_POINTS;
    if (shortHistoryIndex == 0) {
        shortHistoryFilled = true;
    }
    lastShortHistoryTimestamp = timestampEpoch;
}

void addAggregatedPoint(AggregatedPoint* historyBuffer, int maxRecords, int &writeIndex, bool &filled,
                        const AggregatedPoint& point, const char* filePath) {
    historyBuffer[writeIndex] = point;
    writeIndex = (writeIndex + 1) % maxRecords;
    if (writeIndex == 0) {
        filled = true;
    }
    persistAggregatedHistory(filePath, historyBuffer, maxRecords, writeIndex, filled);
}

void finalizeBucket(BucketState& state, AggregatedPoint* historyBuffer, int maxRecords, int &writeIndex,
                    bool &filled, const char* filePath) {
    if (state.sampleCount == 0 || state.bucketStart == 0) {
        state.gridSum = 0;
        state.solarSum = 0;
        state.consumerSum = 0;
        state.sampleCount = 0;
        return;
    }
    AggregatedPoint point;
    point.timestamp = state.bucketStart;
    point.grid = static_cast<int>(state.gridSum / static_cast<int32_t>(state.sampleCount));
    point.solar = static_cast<int>(state.solarSum / static_cast<int32_t>(state.sampleCount));
    point.consumer = static_cast<int>(state.consumerSum / static_cast<int32_t>(state.sampleCount));
    
    addAggregatedPoint(historyBuffer, maxRecords, writeIndex, filled, point, filePath);
    
    state.gridSum = 0;
    state.solarSum = 0;
    state.consumerSum = 0;
    state.sampleCount = 0;
}

void processAggregationSample(BucketState& state, uint32_t timestampEpoch, int grid, int solar, int consumer,
                              AggregatedPoint* historyBuffer, int maxRecords, int &writeIndex,
                              bool &filled, const char* filePath) {
    if (timestampEpoch == 0) {
        return;
    }
    uint32_t bucketStartAligned = (timestampEpoch / state.bucketDuration) * state.bucketDuration;
    if (state.bucketStart == 0) {
        state.bucketStart = bucketStartAligned;
    }
    if (bucketStartAligned > state.bucketStart) {
        if (state.sampleCount > 0) {
            finalizeBucket(state, historyBuffer, maxRecords, writeIndex, filled, filePath);
        }
        state.bucketStart = bucketStartAligned;
    } else if (bucketStartAligned < state.bucketStart) {
        // Ignore out-of-order samples for aggregated history
        return;
    }
    
    state.gridSum += grid;
    state.solarSum += solar;
    state.consumerSum += consumer;
    state.sampleCount++;
}

void storeDataPoint(uint32_t timestampEpoch) {
    if (timestampEpoch == 0) {
        timestampEpoch = timeInitialized ? myTZ.now() : (millis() / 1000UL);
    }
    
    int gridVal = 0;
    int solarVal = 0;
    int consumerVal = 0;
    
    for (int i = 0; i < 3; i++) {
        if (meters[i].name.equalsIgnoreCase("Grid")) {
            gridVal = meters[i].act_power;
        } else if (meters[i].name.equalsIgnoreCase("Solar")) {
            solarVal = meters[i].act_power;
        } else if (meters[i].name.equalsIgnoreCase("Consumer")) {
            consumerVal = meters[i].act_power;
        }
    }
    
    addToShortHistory(timestampEpoch, gridVal, solarVal, consumerVal);
    processAggregationSample(bucket24h, timestampEpoch, gridVal, solarVal, consumerVal,
                             history24h, HISTORY_24H_BUCKETS, history24hIndex, history24hFilled, HISTORY_24H_FILE);
    processAggregationSample(bucket30d, timestampEpoch, gridVal, solarVal, consumerVal,
                             history30d, HISTORY_30D_BUCKETS, history30dIndex, history30dFilled, HISTORY_30D_FILE);
}

String formatTimestampString(uint32_t epochSeconds) {
    if (epochSeconds == 0) {
        return "19700101T000000000";
    }
    time_t rawTime = static_cast<time_t>(epochSeconds);
    struct tm timeinfo;
    gmtime_r(&rawTime, &timeinfo);
    char timestampStr[20];
    strftime(timestampStr, sizeof(timestampStr), "%Y%m%dT%H%M%S", &timeinfo);
    char fullTimestamp[24];
    snprintf(fullTimestamp, sizeof(fullTimestamp), "%s000", timestampStr);
    return String(fullTimestamp);
}

uint32_t extractShellyTimestamp(JsonObject root) {
    if (root["ts"].is<uint32_t>()) {
        return root["ts"].as<uint32_t>();
    }
    if (root["params"].is<JsonObject>()) {
        JsonObject params = root["params"];
        if (params["ts"].is<uint32_t>()) {
            return params["ts"].as<uint32_t>();
        }
        if (params["sys"].is<JsonObject>() && params["sys"]["unixtime"].is<uint32_t>()) {
            return params["sys"]["unixtime"].as<uint32_t>();
        }
    }
    if (root["result"].is<JsonObject>()) {
        JsonObject result = root["result"];
        if (result["ts"].is<uint32_t>()) {
            return result["ts"].as<uint32_t>();
        }
        if (result["sys"].is<JsonObject>() && result["sys"]["unixtime"].is<uint32_t>()) {
            return result["sys"]["unixtime"].as<uint32_t>();
        }
    }
    if (root["sys"].is<JsonObject>() && root["sys"]["unixtime"].is<uint32_t>()) {
        return root["sys"]["unixtime"].as<uint32_t>();
    }
    if (timeInitialized) {
        return myTZ.now();
    }
    return millis() / 1000UL;
}

void updateEnergyMeterData() {
    cleanupExpiredRequests();
    lastDataUpdateTime = millis();
    TIMED_PRINTLN("Requesting energy meter data update...");
    sendShellyGetStatus();
}

// OTA Update Support
void setupOTA() {
    ArduinoOTA.setHostname(ShemeterName.c_str());
    // ArduinoOTA.setPassword("password"); // Password disabled for easier updates
    
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else { // U_SPIFFS
            type = "filesystem";
        }
        TIMED_PRINTLN("Start updating " + type);
    });
    
    ArduinoOTA.onEnd([]() {
        TIMED_PRINTLN("OTA Update completed");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            TIMED_PRINTLN("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            TIMED_PRINTLN("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            TIMED_PRINTLN("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            TIMED_PRINTLN("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            TIMED_PRINTLN("End Failed");
        }
    });
    
    ArduinoOTA.begin();
    TIMED_PRINTLN("OTA Ready");
}

void handleOTA() {
    ArduinoOTA.handle();
}

// Physical button handler for factory reset
void checkFactoryResetButton() {
    bool buttonPressed = (digitalRead(BUTTON_PIN) == LOW);  // Active LOW
    
    if (buttonPressed && !buttonWasPressed) {
        // Button just pressed
        buttonPressStart = millis();
        buttonWasPressed = true;
        factoryResetTriggered = false;
        TIMED_PRINTLN("Factory reset button pressed. Hold for " + String(FACTORY_RESET_HOLD_TIME / 1000) + " seconds to reset...");
    } 
    else if (buttonPressed && buttonWasPressed && !factoryResetTriggered) {
        // Button being held
        unsigned long holdDuration = millis() - buttonPressStart;
        
        // Visual feedback: Blink LED faster as hold time increases
        #ifdef USE_WS2812B_FOR_STATUS
        if (holdDuration % 200 < 100) {
            statusLED.setPixelColor(0, statusLED.Color(255, 0, 0));  // Red
            statusLED.show();
        } else {
            statusLED.clear();
            statusLED.show();
        }
        #endif
        
        if (holdDuration >= FACTORY_RESET_HOLD_TIME) {
            // Trigger factory reset
            factoryResetTriggered = true;
            TIMED_PRINTLN("=== FACTORY RESET TRIGGERED ===");
            logSystemEvent("RESET", "Physical factory reset triggered via button");
            
            // Visual confirmation: Fast red blink
            #ifdef USE_WS2812B_FOR_STATUS
            for (int i = 0; i < 10; i++) {
                statusLED.setPixelColor(0, statusLED.Color(255, 0, 0));
                statusLED.show();
                delay(100);
                statusLED.clear();
                statusLED.show();
                delay(100);
            }
            #endif
            
            // Clear NVS (primary storage)
            if (preferences.begin(NVS_NAMESPACE, false)) {
                preferences.clear();
                preferences.end();
                TIMED_PRINTLN("NVS configuration cleared successfully.");
            }
            
            TIMED_PRINTLN("Factory reset complete. Restarting device...");
            logSystemEvent("RESET", "Factory reset complete. Restarting device");
            delay(1000);
            ESP.restart();
        }
    }
    else if (!buttonPressed && buttonWasPressed) {
        // Button released
        unsigned long holdDuration = millis() - buttonPressStart;
        if (!factoryResetTriggered) {
            TIMED_PRINTLN("Button released after " + String(holdDuration) + "ms (reset requires " + String(FACTORY_RESET_HOLD_TIME) + "ms)");
        }
        buttonWasPressed = false;
        
        // Restore normal status LED
        #ifdef USE_WS2812B_FOR_STATUS
        statusLED.clear();
        statusLED.show();
        #endif
    }
}

// Main setup function
void setup() {
    Serial.begin(115200);
    delay(2000);  // Give serial time to initialize
    bootCounter++;
    deviceStartTimeMillis = millis();
    recordResetDiagnostics();
    
    // Print boot diagnostics
    TIMED_PRINTLN("=== ENERGY MONITOR BOOT DIAGNOSTICS ===");
    TIMED_PRINTLN("ESP32-C3 Reset Reason: " + String(esp_reset_reason()));
    TIMED_PRINTLN("Free heap: " + String(ESP.getFreeHeap()) + " bytes");
    TIMED_PRINTLN("Flash size: " + String(ESP.getFlashChipSize()) + " bytes");
    TIMED_PRINTLN("Free sketch space: " + String(ESP.getFreeSketchSpace()) + " bytes");
    TIMED_PRINTLN("Chip revision: " + String(ESP.getChipRevision()));
    TIMED_PRINTLN("SDK version: " + String(ESP.getSdkVersion()));
    
    // Check for potential issues
    if (ESP.getFreeHeap() < 50000) {
        TIMED_PRINTLN("WARNING: Low free heap memory detected!");
    }
    if (ESP.getFreeSketchSpace() < 100000) {
        TIMED_PRINTLN("WARNING: Low free flash space detected!");
    }

    // Initialize watchdog timer with more conservative settings for ESP32-C3
    esp_task_wdt_init(30, true);  // Reduced from 60 to 30 seconds for faster detection
    esp_task_wdt_add(NULL);
    
    TIMED_PRINTLN("Watchdog timer initialized (30s timeout)");

    // Initialize physical button for factory reset
    pinMode(BUTTON_PIN, INPUT_PULLUP);  // Boot button (GPIO9) with internal pull-up
    TIMED_PRINTLN("Factory reset button initialized (GPIO" + String(BUTTON_PIN) + ")");
    
    // Initialize status LED
#ifdef USE_WS2812B_FOR_STATUS
    statusLED.begin();
    statusLED.setPixelColor(0, statusLED.Color(255, 165, 0));
    statusLED.show();
#else
    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, LOW);
#endif

    delay(5000);
    
    // Load configuration - Try NVS first (survives uploadfs)
    TIMED_PRINTLN("Loading configuration...");
    bool configLoaded = loadConfigNVS();
    
    if (!configLoaded) {
        TIMED_PRINTLN("No configuration found in NVS. Loading defaults and saving.");
        loadDefaultConfig();
        saveConfigNVS();
    }
    
    // Initialize LED strip with loaded configuration
    initializeLEDStrip();
    loadStoredHistories();

    // Check if we have valid WiFi credentials
    bool hasValidCredentials = (strlen(ssid) > 0 && strlen(ssid) < 32);

    if (hasValidCredentials) {
        TIMED_PRINTLN("Found stored WiFi credentials. Attempting connection...");
        TIMED_PRINTLN("SSID: " + String(ssid));
        
        if (tryConnectWiFi(ssid, password)) {
            TIMED_PRINTLN("WiFi Connected using stored credentials.");
            TIMED_PRINTLN("IP: " + WiFi.localIP().toString());
        } else {
            TIMED_PRINTLN("Stored WiFi credentials failed to connect.");
            hasValidCredentials = false;
        }
    } else {
        TIMED_PRINTLN("No valid WiFi credentials found in storage.");
    }
    
    // If no valid credentials or connection failed, start AP mode for configuration
    if (!hasValidCredentials) {
        TIMED_PRINTLN("No working WiFi credentials. Starting Access Point for configuration...");
        TIMED_PRINTLN("Connect to WiFi network: " + fallbackSSID + " (no password)");
        TIMED_PRINTLN("Then navigate to: http://192.168.4.1/config");
        startAPMode();
    }

    if (WiFi.status() == WL_CONNECTED) {
        TIMED_PRINTLN("Connected to WiFi");
        TIMED_PRINTLN(String("IP: ") + String(WiFi.localIP().toString()));
        TIMED_PRINTLN("Gateway: " + WiFi.gatewayIP().toString());
        TIMED_PRINTLN("Subnet: " + WiFi.subnetMask().toString());
        TIMED_PRINTLN("DNS: " + WiFi.dnsIP().toString());

        // Initialize ezTime
        TIMED_PRINTLN("Initializing time synchronization...");
        setDebug(INFO);
        waitForSync();
        
        // Set timezone
        if (myTZ.setLocation(timezone)) {
            TIMED_PRINTLN("Timezone set to: " + getTimezoneDisplayName(timezone));
            TIMED_PRINTLN("Current time: " + myTZ.dateTime("Y-m-d H:i:s T"));
            timeInitialized = true;
            updateDeviceStartTimeFromClock();
        } else {
            TIMED_PRINTLN("Failed to set timezone: " + String(timezone));
            timeInitialized = false;
        }
        
        startTime = millis();

        inSetup = false;  // End setup phase

        // Setup OTA
        setupOTA();

        // Setup mDNS
        if (startUniqueMDNS(ShemeterName)) {
            TIMED_PRINTLN("mDNS responder started successfully.");
        } else {
            TIMED_PRINTLN("mDNS responder failed to start.");
        }

        // Setup web server for connected WiFi (only if not already running from AP mode)
        if (WiFi.getMode() != WIFI_AP) {
            isAPMode = false;  // Normal WiFi mode
            setupWebServer();
            TIMED_PRINTLN("Web server started for WiFi connection");
        }
        
        // Discover and connect to Shelly devices ONLY when WiFi is connected
        TIMED_PRINTLN("WiFi connected - starting Shelly device discovery...");
        discoverShellyDevices();
            } else {
        TIMED_PRINTLN("WiFi not connected, current status: " + String(WiFi.status()));
        TIMED_PRINTLN("Skipping Shelly device discovery - no network connection");
        inSetup = false;  // End setup phase even without WiFi
    }

    // Setup WebSocket handlers
    wsClient.onMessage(onMessageCallback);
    wsClient.onEvent(handleWebSocketEvent);

    // Initial connection attempt ONLY if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
    if (isValidShellyIP(shellyIP)) {
        String wsUrl = String("ws://") + shellyIP + "/rpc";
        TIMED_PRINTLN("Connecting to WebSocket at: " + wsUrl);
        
        if (wsClient.connect(wsUrl)) {
            TIMED_PRINTLN("Connected to Shelly WebSocket");
            sendShellyGetStatus();
        } else {
            TIMED_PRINTLN("Failed to connect to Shelly WebSocket");
        }
    } else {
            TIMED_PRINTLN("No valid Shelly IP available. Will discover devices when WiFi connects.");
    }
    } else {
        TIMED_PRINTLN("Skipping Shelly WebSocket connection - WiFi not connected");
    }

    // Web server will be started by WiFi connection or AP mode functions
    
    TIMED_PRINTLN("=== SETUP COMPLETE - NO RESET LOOP DETECTED ===");
    TIMED_PRINTLN("System ready and stable!");
    TIMED_PRINTLN("Free heap after setup: " + String(ESP.getFreeHeap()) + " bytes");
    if (WiFi.status() == WL_CONNECTED) {
        TIMED_PRINTLN("Device accessible at: http://" + WiFi.localIP().toString());
    } else if (WiFi.getMode() == WIFI_AP) {
        TIMED_PRINTLN("Device accessible at: http://" + WiFi.softAPIP().toString() + " (AP Mode)");
        TIMED_PRINTLN("Configuration page at: http://" + WiFi.softAPIP().toString() + "/config");
    } else {
        TIMED_PRINTLN("Device accessible at: http://0.0.0.0 (No network connection)");
    }
    logSystemMetrics(true);
    TIMED_PRINTLN("=== END BOOT DIAGNOSTICS ===");
}

// Main loop
void loop() {
    unsigned long loopStartMicros = micros();
    unsigned long currentMillis = millis();

    // Handle DNS requests for captive portal when in AP mode
    if (isAPMode) {
        dnsServer.processNextRequest();
    }

    // Handle OTA updates
    handleOTA();

    // Check factory reset button
    checkFactoryResetButton();

    // Data update cycle - only if WiFi is connected
    if (currentMillis - previousMillis >= dataUpdateInterval) {
        previousMillis = currentMillis;
        if (WiFi.status() == WL_CONNECTED) {
        if (!wsClient.available()) {
            checkAndEstablishWebSocket();
        } else {
            updateEnergyMeterData();
            }
        }
    }

    // Status LED during setup
    if (inSetup) {
        blinkPWMLED(LED_STATUS_PIN, 500, 1);
    }

    // Network health monitoring - only if WiFi is connected
    if (currentMillis - lastNetworkHealthCheck >= NETWORK_HEALTH_CHECK_INTERVAL) {
        lastNetworkHealthCheck = currentMillis;
        if (WiFi.status() == WL_CONNECTED) {
        monitorNetworkHealth();
        checkWebSocketHealth();
        }
    }

    // Update display when new data is available
    if (newDataAvailable) {
        String logMsg = "Energy meter data updated: ";
        for (int i = 0; i < 3; i++) {
            logMsg += meters[i].name + ": " + String(meters[i].act_power) + "W, ";
        }

    displayMetricsOnStrip();

        int gridVal = 0;
        int solarVal = 0;
        for (int i = 0; i < 3; i++) {
            if (meters[i].name.equalsIgnoreCase("Grid")) {
                gridVal = meters[i].act_power;
            } else if (meters[i].name.equalsIgnoreCase("Solar")) {
                solarVal = meters[i].act_power;
            }
        }
        calculatedValue = gridVal - solarVal;

        logMsg += "Calculated Consumer: " + String(calculatedValue) + "W";
        TIMED_PRINTLN(logMsg);
        newDataAvailable = false;
    }

    server.handleClient();
    checkWiFiConnection();
    esp_task_wdt_reset();
    wsClient.poll();
    
    // Handle ezTime events
    events();
    unsigned long loopDuration = micros() - loopStartMicros;
    lastLoopDurationMicros = loopDuration;
    if (loopDuration > maxLoopDurationMicros) {
        maxLoopDurationMicros = loopDuration;
    }
    monitorMemoryHealth();
    logSystemMetrics();
    delay(loopDelay);
}
