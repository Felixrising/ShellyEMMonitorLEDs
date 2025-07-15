#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <SPIFFS.h>
#include <ArduinoWebsockets.h>
#include <esp_task_wdt.h>
#include <ESP32Ping.h>
#include <ArduinoOTA.h>
#include <map>
#include <vector>
#include "includes.h"

using namespace websockets;

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

String ShemeterName = "SheMonitor";
String fallbackSSID = ShemeterName + "AP";
const char* fallbackPWD = "12345678";

char ssid[32] = "";
char password[64] = "";
char shellyIP[16] = "";

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
    uint64_t timestamp;  
    int grid;
    int solar;
    int consumer;
};

const int HISTORY_SIZE = 144; // 2.4 hours at 1-minute intervals (reduced for memory optimization)
DataPoint history[HISTORY_SIZE];
int historyIndex = 0;

// Network components
WebServer server(80);
WebsocketsClient wsClient;

const char* configPath = "/config.json";

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
bool loadConfigSPIFFS();
bool saveConfigSPIFFS();
void handleConfig();
void setupWebServer();
void checkWiFiConnection();
void updateEnergyMeterData();
int sendRequest(const char* method, JsonVariant params, std::function<void(JsonObject&)> callback);
void onMessageCallback(WebsocketsMessage message);
void handleWebSocketEvent(WebsocketsEvent event, String data);
void sendShellyGetStatus();
void checkAndEstablishWebSocket();
bool isValidShellyHostname(const String& host);
bool isValidShellyIP(const char* ip);
void updateMeterActPower(int meterIndex, int newPower);
void storeDataPoint();
void handleHistory();
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
    
    TIMED_PRINTLN("LED strip initialized: " + String(LED_COUNT_var) + " LEDs on pin " + String(LED_PIN_var));
}

// Network and connection management
void cleanupPendingRequests() {
    pendingRequests.clear();
    rpcInProgress = false;
    TIMED_PRINTLN("Pending requests cleaned up");
}

void cleanupExpiredRequests() {
    unsigned long now = millis();
    for (auto it = pendingRequests.begin(); it != pendingRequests.end();) {
        if (now - it->second.timestamp > REQUEST_TIMEOUT) {
            TIMED_PRINTLN("Request " + String(it->first) + " timed out");
            it = pendingRequests.erase(it);
        } else {
            ++it;
        }
    }
}

void handleWebSocketError(const String& error) {
    TIMED_PRINTLN("WebSocket error: " + error);
    cleanupPendingRequests();
    lastWebSocketAttempt = millis() + webSocketRetryInterval; // Backoff
}

void checkWebSocketHealth() {
    if (wsClient.available() && (millis() - lastWSActivity > WS_TIMEOUT)) {
        TIMED_PRINTLN("WebSocket timeout, reconnecting...");
        wsClient.close();
        cleanupPendingRequests();
    }
}

void monitorNetworkHealth() {
    static unsigned long lastPing = 0;
    if (millis() - lastPing > NETWORK_HEALTH_CHECK_INTERVAL) {
        lastPing = millis();
        if (strlen(shellyIP) > 0) {
            IPAddress ip;
            ip.fromString(shellyIP);
            if (!Ping.ping(ip, 1)) { // Single ping with timeout
                TIMED_PRINTLN("Shelly device unreachable, triggering rediscovery");
                wsClient.close();
                strcpy(shellyIP, ""); // Force rediscovery
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
        if (ledValue <= consumerValue && ledValue <= solarValue) {
            strip->setPixelColor(i, colorBoth);
        } else if (ledValue <= consumerValue) {
            strip->setPixelColor(i, colorConsumer);
        } else if (ledValue <= solarValue) {
            strip->setPixelColor(i, colorSolar);
        } else {
            strip->setPixelColor(i, colorOff);
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
    WiFi.disconnect();
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
        return true;
    } else {
        TIMED_PRINTLN("Failed to connect to WiFi.");
        return false;
    }
}

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        TIMED_PRINTLN("WiFi connection lost. Attempting to reconnect...");
        if (tryConnectWiFi(ssid, password)) {
            TIMED_PRINTLN("WiFi reconnected.");
        } else {
            TIMED_PRINTLN("Reconnection failed.");
        }
    }
}

// Web server handlers
void handleRoot() { 
    server.send_P(200, "text/html", index_html); 
}

void handleJson() {
    JsonDocument doc; // ArduinoJson v7 - no size needed
    
    doc["SheMeterName"] = ShemeterName;
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

// Configuration management with ArduinoJson v7
bool loadConfigSPIFFS() {
    if (!SPIFFS.begin(true)) {
        TIMED_PRINTLN("SPIFFS Mount Failed");
        return false;
    }
    
    if (!SPIFFS.exists(configPath)) {
        TIMED_PRINTLN("Config file not found. Initializing default configuration.");
        strncpy(ssid, defaultSSID, sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';
        strncpy(password, defaultPassword, sizeof(password) - 1);
        password[sizeof(password) - 1] = '\0';
        strncpy(shellyIP, defaultShellyIP, sizeof(shellyIP) - 1);
        shellyIP[sizeof(shellyIP) - 1] = '\0';
        meters[0].name = "Grid";
        meters[1].name = "Solar";
        meters[2].name = "Consumer";
        ShemeterName = "SheMonitor";
        saveConfigSPIFFS();
        return true;
    }
    
    File file = SPIFFS.open(configPath, "r");
    if (!file) {
        TIMED_PRINTLN("Failed to open config file");
        return false;
    }
    
    JsonDocument doc; // ArduinoJson v7 - automatic sizing
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        TIMED_PRINTLN("Failed to parse config file: " + String(error.c_str()));
        return false;
    }
    
    const char* s = doc["ssid"];
    const char* p = doc["password"];
    const char* sp = doc["shellyIP"];
    const char* sheName = doc["shemeterName"];
    
    strncpy(ssid, s ? s : defaultSSID, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    strncpy(password, p ? p : defaultPassword, sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
    strncpy(shellyIP, sp ? sp : defaultShellyIP, sizeof(shellyIP) - 1);
    shellyIP[sizeof(shellyIP) - 1] = '\0';
    ShemeterName = sheName ? String(sheName) : "SheMonitor";

    if (doc["ledCount"].is<int>()) LED_COUNT_var = doc["ledCount"];
    if (doc["ledPin"].is<int>()) LED_PIN_var = doc["ledPin"];
    if (doc["ledType"].is<uint32_t>()) LED_TYPE_flags = doc["ledType"];
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
    } else {
        meters[0].name = "Grid";
        meters[1].name = "Solar";
        meters[2].name = "Consumer";
    }

    // Validate configuration
    validateConfig();

    TIMED_PRINTLN("Configuration loaded from SPIFFS:");
    TIMED_PRINTLN("SSID: " + String(ssid));
    TIMED_PRINTLN("Shelly IP: " + String(shellyIP));
    TIMED_PRINTLN("SheMeter Name: " + ShemeterName);
    
    return true;
}

bool saveConfigSPIFFS() {
    JsonDocument doc; // ArduinoJson v7
    
    doc["ssid"] = ssid;
    doc["password"] = password;
    doc["shellyIP"] = shellyIP;
    doc["shemeterName"] = ShemeterName;
    doc["ledCount"] = LED_COUNT_var;
    doc["ledPin"] = LED_PIN_var;
    doc["ledType"] = LED_TYPE_flags;
    doc["invertStrip"] = invertStrip;
    
    JsonArray meterArray = doc["meters"].to<JsonArray>();
    for (int i = 0; i < 3; i++) {
        meterArray.add(meters[i].name);
    }
    
    File file = SPIFFS.open(configPath, "w");
    if (!file) {
        TIMED_PRINTLN("Failed to open config file for writing");
        return false;
    }
    
    if (serializeJson(doc, file) == 0) {
        TIMED_PRINTLN("Failed to write to file");
        file.close();
        return false;
    }
    
    file.close();
    TIMED_PRINTLN("Configuration saved to SPIFFS.");
    return true;
}

// Enhanced configuration handler with validation
void handleConfig() {
    if (server.method() == HTTP_POST) {
        String newSSID = server.arg("ssid");
        String newPassword = server.arg("password");
        String newShellyIP = server.arg("shellyIP");
        String newSheMeterName = server.arg("shemeterName");
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
        
        if (newLedCount < 1 || newLedCount > 300) {
            valid = false;
            errorMsg = "LED count must be between 1 and 300.";
        }
        
        if (newLedPin < 0 || newLedPin > 39) {
            valid = false;
            errorMsg = "LED pin must be between 0 and 39.";
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
            ShemeterName = newSheMeterName;
            fallbackSSID = ShemeterName + "AP";

            meters[0].name = meter0Role;
            meters[1].name = meter1Role;
            meters[2].name = meter2Role;

            // Update LED configuration
            LED_COUNT_var = newLedCount;
            LED_PIN_var = newLedPin;
            LED_TYPE_flags = (uint32_t)ledTypeStr.toInt();
            invertStrip = ledInvert;

            // Reinitialize LED strip with new configuration
            initializeLEDStrip();

            if (startUniqueMDNS(ShemeterName)) {
                TIMED_PRINTLN("mDNS responder restarted with new ShemeterName.");
            } else {
                TIMED_PRINTLN("Failed to restart mDNS responder with new ShemeterName.");
            }

            saveConfigSPIFFS();
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
        html += "<div class='form-row'>";
        html += "<div class='form-group'><label>LED Count</label><input type='number' name='ledCount' value='" + String(LED_COUNT_var) + "' min='1' max='300' required></div>";
        html += "<div class='form-group'><label>LED Pin</label><input type='number' name='ledPin' value='" + String(LED_PIN_var) + "' min='0' max='39' required></div>";
        html += "</div>";
        
        html += "<div class='form-row'>";
        html += "<div class='form-group'><label>LED Type Flags</label><input type='text' name='ledType' value='" + String(LED_TYPE_flags) + "' required></div>";
        html += "<div class='form-group' style='display: flex; align-items: center; padding-top: 30px;'>";
        html += "<input type='checkbox' name='invertStrip'" + String(invertStrip ? " checked" : "") + " style='width: auto; margin-right: 10px;'>";
        html += "<label>Invert LED Strip</label>";
        html += "</div>";
        html += "</div>";
        
        html += "<div style='margin-top: 30px;'>";
        html += "<button type='submit' class='btn btn-primary'>Save Configuration</button>";
        html += "</div>";
        html += "</form>";
        html += "</div>";
        
        // System Actions
        html += "<div class='card'>";
        html += "<div class='section-title'>System Actions</div>";
        html += "<button type='button' class='btn btn-secondary' onclick='checkDebugData()' style='margin-right: 10px;'>Debug Data</button>";
        html += "<form action='/ota' method='post' style='display: inline-block;'>";
        html += "<button type='submit' class='btn btn-secondary'>Check for Updates</button>";
        html += "</form>";
        html += "<form action='/factoryReset' method='post' style='display: inline-block;' onsubmit='return confirm(\"Are you sure you want to reset all settings?\");'>";
        html += "<button type='submit' class='btn btn-danger'>Factory Reset</button>";
        html += "</form>";
        html += "</div>";

        html += "</div>";
        
        // Add the debug function JavaScript
        html += "<script>";
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
        html += "</script>";
        
        html += "</body></html>";
        server.send(200, "text/html", html);
    }
}

// History handling with streaming for large datasets
void handleHistory() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    server.sendContent("[");
    
    bool first = true;
    int count = 0;
    int index = historyIndex;
    
    for (int i = 0; i < HISTORY_SIZE; i++) {
        DataPoint &dp = history[index];
        if (dp.timestamp == 0) {
            index = (index + 1) % HISTORY_SIZE;
            continue;
        }

        if (!first) {
            server.sendContent(",");
        }
        first = false;

        time_t seconds = (time_t)(dp.timestamp / 1000);
        int milliseconds = dp.timestamp % 1000;
        struct tm *timeinfo = localtime(&seconds);
        char timestampStr[20];
        strftime(timestampStr, sizeof(timestampStr), "%Y%m%dT%H%M%S", timeinfo);
        char fullTimestamp[24];
        sprintf(fullTimestamp, "%s%03d", timestampStr, milliseconds);

        JsonDocument obj; // ArduinoJson v7
        obj["timestamp"] = String(fullTimestamp);
        obj["Grid"] = dp.grid;
        obj["Solar"] = dp.solar;
        obj["Consumer"] = dp.consumer;

        String objStr;
        serializeJson(obj, objStr);
        server.sendContent(objStr);

        index = (index + 1) % HISTORY_SIZE;
        count++;
        
        if (count % 10 == 0) {
            esp_task_wdt_reset(); // Prevent watchdog timeout
        }
    }
    
    server.sendContent("]");
}

// Web server setup
void setupWebServer() {
    server.on("/", handleRoot);
    server.on("/data", handleJson);
    server.on("/config", handleConfig);
    server.on("/history", HTTP_GET, handleHistory);
    
    // Simple test endpoint
    server.on("/test", []() {
        server.send(200, "text/plain", "Web server is working! Device: " + ShemeterName + " | IP: " + WiFi.localIP().toString());
    });
    
    // Time synchronization endpoint
    server.on("/time", []() {
        JsonDocument doc;
        doc["deviceMillis"] = millis();
        doc["deviceStartTime"] = deviceStartTimeMillis;
        doc["currentDeviceTime"] = deviceStartTimeMillis + millis();
        
        time_t now;
        time(&now);
        doc["unixTime"] = now;
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    
    // Debug endpoint to check current data and timing
    server.on("/debug", []() {
        JsonDocument doc;
        doc["deviceTime"] = millis();
        doc["deviceStartTime"] = deviceStartTimeMillis;
        doc["currentTimestamp"] = deviceStartTimeMillis + millis();
        doc["lastDataUpdate"] = lastDataUpdateTime;
        doc["dataUpdateInterval"] = dataUpdateInterval;
        doc["wsConnected"] = wsClient.available();
        doc["rpcInProgress"] = rpcInProgress;
        doc["newDataAvailable"] = newDataAvailable;
        
        JsonArray metersArray = doc["meters"].to<JsonArray>();
        for (int i = 0; i < 3; i++) {
            JsonObject meter = metersArray.add<JsonObject>();
            meter["name"] = meters[i].name;
            meter["power"] = meters[i].act_power;
            meter["lastUpdate"] = meters[i].lastUpdateTime;
        }
        
        // Add last few history points
        JsonArray historyArray = doc["recentHistory"].to<JsonArray>();
        int startIdx = (historyIndex - 5 + HISTORY_SIZE) % HISTORY_SIZE;
        for (int i = 0; i < 5; i++) {
            int idx = (startIdx + i) % HISTORY_SIZE;
            if (history[idx].timestamp != 0) {
                JsonObject point = historyArray.add<JsonObject>();
                point["timestamp"] = history[idx].timestamp;
                point["grid"] = history[idx].grid;
                point["solar"] = history[idx].solar;
                point["consumer"] = history[idx].consumer;
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
    
    server.on("/factoryReset", HTTP_POST, []() {
        SPIFFS.remove(configPath);
        TIMED_PRINTLN("Factory reset performed. Configuration cleared.");
        server.sendHeader("Location", "/config");
        server.send(303);
        delay(1000);
        ESP.restart();
    });
    
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
    JsonDocument doc; // ArduinoJson v7
    
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
    
    JsonDocument params; // ArduinoJson v7 - empty params
    sendRequest("Shelly.GetStatus", params.as<JsonVariant>(), [](JsonObject& response) {
        TIMED_PRINTLN("Shelly.GetStatus response received.");
        rpcInProgress = false;

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
            
        storeDataPoint();
        }
    });
}

void onMessageCallback(WebsocketsMessage message) {
    lastWSActivity = millis();
    TIMED_PRINTLN("Received WebSocket message:");
    TIMED_PRINTLN(message.data());

    JsonDocument doc; // ArduinoJson v7
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
            storeDataPoint();
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
            saveConfigSPIFFS();
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
    unsigned long now = millis();
    if (now - lastWebSocketAttempt < webSocketRetryInterval) {
        return;
    }
    lastWebSocketAttempt = now;

    checkWiFiConnection();
    
    // Try stored IP first
    IPAddress storedIP;
    bool validStored = false;
    if (String(shellyIP).length() > 0 && isValidShellyIP(shellyIP)) {
        storedIP.fromString(shellyIP);
        if (Ping.ping(storedIP, 1)) {
            validStored = true;
            TIMED_PRINTLN("Stored Shelly IP is reachable.");
        } else {
            TIMED_PRINTLN("Stored Shelly IP is not reachable.");
        }
    }
    
    if (validStored) {
        String wsUrl = String("ws://") + shellyIP + "/rpc";
        TIMED_PRINTLN("Attempting to reconnect to WebSocket at: " + wsUrl);
        
        if (wsClient.connect(wsUrl)) {
            TIMED_PRINTLN("Reconnected to WebSocket using stored Shelly IP.");
            sendShellyGetStatus();
            return;
        } else {
            TIMED_PRINTLN("Failed to reconnect using stored Shelly IP.");
        }
    }

    // Discover devices
    discoverShellyDevices();
    
    // Try to connect to discovered devices
    for (auto& device : shellyDevices) {
        if (device.type == "3EM") {
            device.ip.toCharArray(shellyIP, sizeof(shellyIP));
            String wsUrl = String("ws://") + shellyIP + "/rpc";
            TIMED_PRINTLN("Connecting to " + device.type + " at: " + wsUrl);
            
            if (wsClient.connect(wsUrl)) {
                TIMED_PRINTLN("Connected to " + device.type + " WebSocket after discovery.");
                device.isActive = true;
                if (shellyDevices.size() == 1) {
                    saveConfigSPIFFS();
                }
                sendShellyGetStatus();
                return;
            }
        }
    }
    
    TIMED_PRINTLN("No valid Shelly 3EM device found or connection failed.");
}

// Data management
void storeDataPoint() {
    DataPoint &dp = history[historyIndex];
    dp.timestamp = deviceStartTimeMillis + millis();
    
    for (int i = 0; i < 3; i++) {
        if (meters[i].name.equalsIgnoreCase("Grid")) {
            dp.grid = meters[i].act_power;
        } else if (meters[i].name.equalsIgnoreCase("Solar")) {
            dp.solar = meters[i].act_power;
        } else if (meters[i].name.equalsIgnoreCase("Consumer")) {
            dp.consumer = meters[i].act_power;
        }
    }
    
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
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
    ArduinoOTA.setPassword("ShellyOTA123"); // Change this to a secure password
    
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

// Main setup function
void setup() {
    Serial.begin(115200);

    // Initialize watchdog timer
    esp_task_wdt_init(60, true);
    esp_task_wdt_add(NULL);

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
    
    // Load configuration
    loadConfigSPIFFS();
    
    // Initialize LED strip with loaded configuration
    initializeLEDStrip();

    TIMED_PRINTLN("Attempting to connect using stored WiFi credentials...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    delay(3500);

    if (WiFi.status() != WL_CONNECTED) {
        TIMED_PRINTLN("Stored WiFi credentials failed. Starting SmartConfig...");
        WiFi.mode(WIFI_AP_STA);
        WiFi.beginSmartConfig();
        unsigned long smartconfigStart = millis();
        
        while (!WiFi.smartConfigDone() && (millis() - smartconfigStart) < 300000) {
            delay(500);
            esp_task_wdt_reset();
            Serial.print(".");
        }
        
        if (WiFi.smartConfigDone()) {
            TIMED_PRINTLN("SmartConfig successful.");
            while (WiFi.status() != WL_CONNECTED) {
                delay(500);
                esp_task_wdt_reset();
                Serial.print(".");
            }
            TIMED_PRINTLN("WiFi Connected.");
            TIMED_PRINTLN(String("IP: ") + WiFi.localIP().toString());
            
            strncpy(ssid, WiFi.SSID().c_str(), sizeof(ssid) - 1);
            ssid[sizeof(ssid) - 1] = '\0';
            strncpy(password, WiFi.psk().c_str(), sizeof(password) - 1);
            password[sizeof(password) - 1] = '\0';
            saveConfigSPIFFS();
        } else {
            TIMED_PRINTLN("SmartConfig failed. Starting fallback AP mode.");
            WiFi.mode(WIFI_AP);
            WiFi.softAP(fallbackSSID.c_str(), fallbackPWD);
            TIMED_PRINTLN("Access Point started: " + fallbackSSID);
            TIMED_PRINTLN("AP IP: " + WiFi.softAPIP().toString());
        }
    } else {
        TIMED_PRINTLN("WiFi Connected using stored credentials.");
    }

    if (WiFi.status() == WL_CONNECTED) {
        TIMED_PRINTLN("Connected to WiFi");
        TIMED_PRINTLN(String("IP: ") + String(WiFi.localIP().toString()));
        TIMED_PRINTLN("Gateway: " + WiFi.gatewayIP().toString());
        TIMED_PRINTLN("Subnet: " + WiFi.subnetMask().toString());
        TIMED_PRINTLN("DNS: " + WiFi.dnsIP().toString());

        // Initialize time
        configTime(36000, 0, "au.pool.ntp.org");
        struct tm timeinfo;
        while (!getLocalTime(&timeinfo)) {
            TIMED_PRINTLN("Waiting for time sync...");
            delay(1000);
        }
        deviceStartTimeMillis = ((uint64_t)mktime(&timeinfo)) * 1000 + millis();
        TIMED_PRINTLN("Device start time initialized: " + String(mktime(&timeinfo)));

        inSetup = false;  // End setup phase

        // Setup OTA
        setupOTA();

        // Setup mDNS
        if (startUniqueMDNS(ShemeterName)) {
            TIMED_PRINTLN("mDNS responder started successfully.");
        } else {
            TIMED_PRINTLN("mDNS responder failed to start.");
        }

        // Discover and connect to Shelly devices
        discoverShellyDevices();
            } else {
        TIMED_PRINTLN("WiFi not connected, current status: " + String(WiFi.status()));
    }

    // Setup WebSocket handlers
    wsClient.onMessage(onMessageCallback);
    wsClient.onEvent(handleWebSocketEvent);

    // Initial connection attempt
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
        TIMED_PRINTLN("No valid Shelly IP available. Will discover devices.");
    }

    // Always setup web server (works in both STA and AP modes)
    setupWebServer();
    
    TIMED_PRINTLN("Setup complete. System ready.");
}

// Main loop
void loop() {
    unsigned long currentMillis = millis();

    // Handle OTA updates
    handleOTA();

    // Data update cycle
    if (currentMillis - previousMillis >= dataUpdateInterval) {
        previousMillis = currentMillis;
        if (!wsClient.available()) {
            checkAndEstablishWebSocket();
        } else {
            updateEnergyMeterData();
        }
    }

    // Status LED during setup
    if (inSetup) {
        blinkPWMLED(LED_STATUS_PIN, 500, 1);
    }

    // Network health monitoring
    if (currentMillis - lastNetworkHealthCheck >= NETWORK_HEALTH_CHECK_INTERVAL) {
        lastNetworkHealthCheck = currentMillis;
        monitorNetworkHealth();
        checkWebSocketHealth();
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
    delay(loopDelay);
}
