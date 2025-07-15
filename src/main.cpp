#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <SPIFFS.h>
#include <ArduinoWebsockets.h>
#include <esp_task_wdt.h>
#include <ESP32Ping.h>
#include <map>
#include <vector>
#include "includes.h"

using namespace websockets;

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

unsigned long lastWebSocketAttempt = 0;
const long webSocketRetryInterval = 10000; // 10 seconds

bool inSetup = true;  // Flag to indicate setup phase
bool rpcInProgress = false;
bool newDataAvailable = false;
uint64_t deviceStartTimeMillis = 0;

const char* defaultSSID = "";
const char* defaultPassword = "";
const char* defaultShellyIP = "";

String ShemeterName = "SheMonitor";
String fallbackSSID = ShemeterName + "AP";
const char* fallbackPWD = "12345678";

char ssid[32] = "";
char password[64] = "";
char shellyIP[16] = "";

unsigned long previousMillis = 0;
const long dataUpdateInterval = 1000; // 1-second update frequency
const long loopDelay = 2;
unsigned long lastDataUpdateTime = 0;
int calculatedValue = 0;

struct EnergyMeter { String name; int act_power; unsigned long lastUpdateTime; };
EnergyMeter meters[3] = { {"Grid", 0, 0}, {"Solar", 0, 0}, {"Consumer", 0, 0} };

struct DataPoint {
    uint64_t timestamp;  
    int grid;
    int solar;
    int consumer;
};

const int HISTORY_SIZE = 300;
DataPoint history[HISTORY_SIZE];
int historyIndex = 0;

WebServer server(80);
WebsocketsClient wsClient;

const char* configPath = "/config.json";

struct PendingRequest { std::function<void(JsonObject&)> callback; };
std::map<int, PendingRequest> pendingRequests;
int commandId = 0;

static StaticJsonDocument<20480> historyJsonDoc;

// New LED configuration variables
int LED_COUNT_var = 60;
int LED_PIN_var = 4;
uint32_t LED_TYPE_flags = NEO_GRBW + NEO_KHZ800;
bool invertStrip = false;

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

bool isValidShellyHostname(const String& host) {
    String lowerHost = host;
    lowerHost.toLowerCase();
    return lowerHost.startsWith("shelly") && host.indexOf('-') != -1;
}

bool isValidShellyIP(const char* ip) {
    String ipStr = String(ip);
    return ipStr.length() > 0 && ipStr != "xxx";
}

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

bool tryConnectWiFi(const char* ssid, const char* password) {
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    unsigned long startTime = millis();
    TIMED_PRINT("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
        delay(500);
        Serial.print(".");
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

int scaledBrightness(int brightness) {
    if (brightness <= 0) return 0;
    if (brightness >= MAX_BRIGHTNESS) return MAX_BRIGHTNESS;
    float scale = (float)globalBrightness / MAX_BRIGHTNESS;
    int newBrightness = (int)(brightness * scale);
    return newBrightness <= 0 ? 1 : min(newBrightness, MAX_BRIGHTNESS);
}

int scaledBrightness() { return scaledBrightness(globalBrightness); }

void displayMetricsOnStrip() {
    int consumerValue = 0;
    int solarValue = 0;

    for (int i = 0; i < 3; i++) {
        if (meters[i].name.equalsIgnoreCase("Consumer")) {
            consumerValue = meters[i].act_power;
        } else if (meters[i].name.equalsIgnoreCase("Solar")) {
            solarValue = meters[i].act_power;
        }
    }

    uint32_t colorBoth     = strip.Color(scaledBrightness(153), scaledBrightness(255), 0, 0);
    uint32_t colorConsumer = strip.Color(scaledBrightness(255), 0, 0, 0);
    uint32_t colorSolar    = strip.Color(0, scaledBrightness(204), scaledBrightness(255), 0);
    uint32_t colorOff      = strip.Color(0, 0, 0, 0);

    int minRaw = 100;
    int maxRaw = 5000;
    int range  = (maxRaw - minRaw);
    int last   = LED_COUNT - 1;

    for (int i = 0; i < LED_COUNT; i++) {
        int ledValue = minRaw + (range * i) / last;
        if (ledValue <= consumerValue && ledValue <= solarValue) {
            strip.setPixelColor(i, colorBoth);
        } else if (ledValue <= consumerValue) {
            strip.setPixelColor(i, colorConsumer);
        } else if (ledValue <= solarValue) {
            strip.setPixelColor(i, colorSolar);
        } else {
            strip.setPixelColor(i, colorOff);
        }
    }
    strip.show();
}

void handleRoot() { server.send_P(200, "text/html", index_html); }

void handleJson() {
    unsigned long clientTimestamp = server.arg("timestamp").toInt();
    String json = "{";
    json += "\"SheMeterName\": \"" + ShemeterName + "\","; 
    json += "\"meters\": [";
    for (int i = 0; i < 3; i++) {
        if (i > 0) json += ",";
        json += "{\"name\":\"" + meters[i].name + "\",\"power\":" + String(meters[i].act_power) + "}";
    }
    json += "]";
    json += "}";
    server.send(200, "application/json", json);
}

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
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        TIMED_PRINTLN("Failed to parse config file");
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

    if (doc.containsKey("ledCount")) LED_COUNT_var = doc["ledCount"];
    if (doc.containsKey("ledPin")) LED_PIN_var = doc["ledPin"];
    if (doc.containsKey("ledType")) LED_TYPE_flags = doc["ledType"];
    if (doc.containsKey("invertStrip")) invertStrip = doc["invertStrip"];

    if (doc.containsKey("meters") && doc["meters"].is<JsonArray>()) {
        JsonArray meterArray = doc["meters"].as<JsonArray>();
        int index = 0;
        for (auto meterName : meterArray) {
            if (index < 3 && meterName.is<const char*>()) {
                meters[index].name = String((const char*)meterName);
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

    TIMED_PRINTLN("Configuration loaded from SPIFFS:");
    TIMED_PRINTLN("SSID: " + String(ssid));
    TIMED_PRINTLN("Password: " + String(password));
    TIMED_PRINTLN("Shelly IP: " + String(shellyIP));
    TIMED_PRINTLN("SheMeter Name: " + ShemeterName);
    String meterNames = "Meter Names: ";
    for (int i = 0; i < 3; i++) {
        meterNames += meters[i].name;
        if (i < 2) meterNames += ", ";
    }
    TIMED_PRINTLN(meterNames);
    return true;
}

bool saveConfigSPIFFS() {
    StaticJsonDocument<512> doc;
    doc["ssid"] = ssid;
    doc["password"] = password;
    doc["shellyIP"] = shellyIP;
    doc["shemeterName"] = ShemeterName;
    doc["ledCount"] = LED_COUNT_var;
    doc["ledPin"] = LED_PIN_var;
    doc["ledType"] = LED_TYPE_flags;
    doc["invertStrip"] = invertStrip;
    JsonArray meterArray = doc.createNestedArray("meters");
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

void handleConfig() {
    if (server.method() == HTTP_POST) {
        String newSSID = server.arg("ssid");
        String newPassword = server.arg("password");
        String newShellyIP = server.arg("shellyIP");
        String newSheMeterName = server.arg("shemeterName");
        String meter0Role = server.arg("meter0");
        String meter1Role = server.arg("meter1");
        String meter2Role = server.arg("meter2");

        // Retrieve new LED configuration fields from form
        String ledCountStr = server.arg("ledCount");
        String ledPinStr = server.arg("ledPin");
        String ledTypeStr = server.arg("ledType");
        bool ledInvert = server.hasArg("invertStrip");

        bool valid = true;
        String errorMsg = "";

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

        MDNS.end();
        if (!MDNS.begin(newSheMeterName)) {
            valid = false;
            errorMsg = "The name " + newSheMeterName + " is already in use on the network. Please choose a different name.";
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
            LED_COUNT_var = ledCountStr.toInt();
            LED_PIN_var = ledPinStr.toInt();
            LED_TYPE_flags = (uint32_t)ledTypeStr.toInt();
            invertStrip = ledInvert;

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
        String html = "<!DOCTYPE html><html><body>";
        html += "<h1>Configuration</h1>";
        html += "<form action='/config' method='post'>";

        html += "SSID: <input type='text' name='ssid' value='" + String(ssid) + "'><br>";
        html += "Password: <input type='password' name='password' value='" + String(password) + "'><br>";
        html += "Shelly IP: <input type='text' name='shellyIP' value='" + String(shellyIP) + "'><br>";
        html += "SheMeter Name: <input type='text' name='shemeterName' value='" + String(ShemeterName) + "'><br>";

        html += "Meter 0 Role: <select name='meter0'>";
        html += "<option value='Grid'" + String(meters[0].name.equalsIgnoreCase("Grid") ? " selected" : "") + ">Grid</option>";
        html += "<option value='Solar'" + String(meters[0].name.equalsIgnoreCase("Solar") ? " selected" : "") + ">Solar</option>";
        html += "<option value='Consumer'" + String(meters[0].name.equalsIgnoreCase("Consumer") ? " selected" : "") + ">Consumer</option>";
        html += "</select><br>";
        html += "Current Power: " + String(meters[0].act_power) + "W<br>";

        html += "Meter 1 Role: <select name='meter1'>";
        html += "<option value='Grid'" + String(meters[1].name.equalsIgnoreCase("Grid") ? " selected" : "") + ">Grid</option>";
        html += "<option value='Solar'" + String(meters[1].name.equalsIgnoreCase("Solar") ? " selected" : "") + ">Solar</option>";
        html += "<option value='Consumer'" + String(meters[1].name.equalsIgnoreCase("Consumer") ? " selected" : "") + ">Consumer</option>";
        html += "</select><br>";
        html += "Current Power: " + String(meters[1].act_power) + "W<br>";

        html += "Meter 2 Role: <select name='meter2'>";
        html += "<option value='Grid'" + String(meters[2].name.equalsIgnoreCase("Grid") ? " selected" : "") + ">Grid</option>";
        html += "<option value='Solar'" + String(meters[2].name.equalsIgnoreCase("Solar") ? " selected" : "") + ">Solar</option>";
        html += "<option value='Consumer'" + String(meters[2].name.equalsIgnoreCase("Consumer") ? " selected" : "") + ">Consumer</option>";
        html += "</select><br>";
        html += "Current Power: " + String(meters[2].act_power) + "W<br>";

        // LED configuration fields
        html += "LED Strip Length (Number of Pixels): <input type='number' name='ledCount' value='" + String(LED_COUNT_var) + "'><br>";
        html += "LED Pin: <input type='number' name='ledPin' value='" + String(LED_PIN_var) + "'><br>";
        html += "LED Type Flags: <input type='text' name='ledType' value='" + String(LED_TYPE_flags) + "'><br>";
        html += "Invert LED Strip: <input type='checkbox' name='invertStrip'" + String(invertStrip ? " checked" : "") + "><br>";

        html += "<input type='submit' value='Save'>";
        html += "</form>";

        html += "<form action='/factoryReset' method='post' style='margin-top:20px;'>";
        html += "<input type='submit' value='Factory Reset'>";
        html += "</form>";

        html += "</body></html>";
        server.send(200, "text/html", html);
    }
}

void setupWebServer() {
    server.on("/", handleRoot);
    server.on("/data", handleJson);
    server.on("/config", handleConfig);
    server.on("/history", HTTP_GET, handleHistory);
    server.on("/factoryReset", HTTP_POST, []() {
        SPIFFS.remove(configPath);
        TIMED_PRINTLN("Factory reset performed. Configuration cleared.");
        server.sendHeader("Location", "/config");
        server.send(303);
    });
    server.begin();
    TIMED_PRINTLN("HTTP server started");
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

int sendRequest(const char* method, JsonVariant params, std::function<void(JsonObject&)> callback) {
    commandId++;
    StaticJsonDocument<256> doc;
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
    pendingRequests[commandId] = { callback };
    wsClient.send(request);
    return commandId;
}

void updateMeterActPower(int meterIndex, int newPower) {
    if (meters[meterIndex].name.equalsIgnoreCase("Solar")) {
        newPower = -newPower;
    }
    if (meters[meterIndex].act_power != newPower) {
        meters[meterIndex].act_power = newPower;
        meters[meterIndex].lastUpdateTime = millis();
        newDataAvailable = true;
        TIMED_PRINTLN("Updated " + meters[meterIndex].name + " act_power: " + String(newPower) + " W");
    }
}

void sendShellyGetStatus() {
    if (rpcInProgress) return;
    rpcInProgress = true;
    sendRequest("Shelly.GetStatus", JsonVariant(), [](JsonObject& response) {
        TIMED_PRINTLN("Shelly.GetStatus response received.");
        rpcInProgress = false;

        JsonObject result = response["result"].as<JsonObject>();
        for (int i = 0; i < 3; ++i) {
            String key = "em1:" + String(i);
            if (result.containsKey(key)) {
                JsonObject meter = result[key].as<JsonObject>();
                if (meter.containsKey("act_power")) {
                    int newPower = meter["act_power"].as<int>();
                    updateMeterActPower(i, newPower);
                    TIMED_PRINTLN("Meter " + String(i) + " Updated: " + String(newPower) + "W");
                }
            } else {
                TIMED_PRINTLN("Key not found in response: " + key);
            }
        }
        storeDataPoint();
    });
}

void updateEnergyMeterData() {
    if (!wsClient.available()) {
        TIMED_PRINTLN("WebSocket unavailable during update.");
        return;
    }
    sendShellyGetStatus();
}

void storeDataPoint() {
    DataPoint dp;
    time_t now = time(nullptr);
    dp.timestamp = ((uint64_t) now) * 1000;
    for (int i = 0; i < 3; i++) {
        if (meters[i].name.equalsIgnoreCase("Grid")) {
            dp.grid = meters[i].act_power;
        } else if (meters[i].name.equalsIgnoreCase("Solar")) {
            dp.solar = meters[i].act_power;
        } else if (meters[i].name.equalsIgnoreCase("Consumer")) {
            dp.consumer = meters[i].act_power;
        }
    }
    history[historyIndex] = dp;
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
}

void handleHistory() {
    historyJsonDoc.clear();
    JsonArray array = historyJsonDoc.to<JsonArray>();

    int index = historyIndex;
    for (int count = 0; count < HISTORY_SIZE; count++) {
        DataPoint &dp = history[index];
        if (dp.timestamp == 0) {
            index = (index + 1) % HISTORY_SIZE;
            continue;
        }

        time_t seconds = (time_t)(dp.timestamp / 1000);
        int milliseconds = dp.timestamp % 1000;
        struct tm *timeinfo = localtime(&seconds);
        char timestampStr[20];
        strftime(timestampStr, sizeof(timestampStr), "%Y%m%dT%H%M%S", timeinfo);
        char fullTimestamp[24];
        sprintf(fullTimestamp, "%s%03d", timestampStr, milliseconds);

        JsonObject obj = array.createNestedObject();
        obj["timestamp"] = String(fullTimestamp);
        obj["Grid"] = dp.grid;
        obj["Solar"] = dp.solar;
        obj["Consumer"] = dp.consumer;

        index = (index + 1) % HISTORY_SIZE;
    }

    String response;
    serializeJson(array, response);
    server.send(200, "application/json", response);
}

void onMessageCallback(WebsocketsMessage message) {
    TIMED_PRINTLN("Received WebSocket message:");
    TIMED_PRINTLN(message.data());

    static StaticJsonDocument<2048> doc;
    doc.clear();

    DeserializationError error = deserializeJson(doc, message.data());
    if (error) {
        TIMED_PRINTLN(String("Failed to parse response: ") + error.f_str());
        rpcInProgress = false;
        return;
    }

    if (doc.containsKey("method")) {
        String method = doc["method"];
        if (method == "NotifyStatus") {
            JsonObject params = doc["params"].as<JsonObject>();
            for (int i = 0; i < 3; ++i) {
                String key = "em1:" + String(i);
                if (params.containsKey(key)) {
                    JsonObject meter = params[key].as<JsonObject>();
                    float act_power = meter["act_power"] | 0.0;
                    updateMeterActPower(i, (int)act_power);
                    Serial.print("Notification - Meter ");
                    Serial.print(i);
                    Serial.print(" Active Power: ");
                    Serial.println(act_power);
                }
            }
            storeDataPoint();
        }
    } else if (doc.containsKey("id")) {
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
        break;
    case WebsocketsEvent::ConnectionClosed:
        TIMED_PRINTLN("WebSocket connection closed.");
        rpcInProgress = false;
        break;
    case WebsocketsEvent::GotPing:
        TIMED_PRINTLN("WebSocket ping received. Replied Pong.");
        wsClient.pong();
        break;
    case WebsocketsEvent::GotPong:
        TIMED_PRINTLN("WebSocket pong received.");
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
    IPAddress storedIP;
    bool validStored = false;
    if (String(shellyIP).length() > 0 && isValidShellyIP(shellyIP)) {
        storedIP.fromString(shellyIP);
        if (Ping.ping(storedIP)) {
            validStored = true;
            TIMED_PRINTLN("Stored Shelly IP is reachable.");
        } else {
            TIMED_PRINTLN("Stored Shelly IP is not reachable.");
        }
    }
    if (validStored) {
        String wsUrl = String("ws://") + shellyIP + "/rpc";
        TIMED_PRINT("Attempting to reconnect to WebSocket at: " + wsUrl);
        Serial.println();
        delay(500);
        if (wsClient.connect(wsUrl)) {
            TIMED_PRINTLN("Reconnected to WebSocket using stored Shelly IP.");
            sendShellyGetStatus();
            return;
        } else {
            TIMED_PRINTLN("Failed to reconnect using stored Shelly IP.");
        }
    }

    delay(2000);
    TIMED_PRINTLN("Attempting Shelly discovery via mDNS...");

#if DEBUG_LOG
    int nAll = MDNS.queryService("http", "tcp");
    Serial.print(timestamp() + " All mDNS services discovered: ");
    Serial.println(nAll);
    for (int j = 0; j < nAll; j++) {
        Serial.print(timestamp() + " Hostname: ");
        Serial.print(MDNS.hostname(j));
        Serial.print(" IP: ");
        Serial.println(MDNS.IP(j));
    }
#endif

    int nServices = MDNS.queryService("http", "tcp");
    TIMED_PRINT("mDNS query found ");
    Serial.println(nServices);
    Serial.println("Matching Shelly devices:");

    struct ShellyDevice { String name; IPAddress ip; };
    std::vector<ShellyDevice> discoveredDevices;

    for (int i = 0; i < nServices; ++i) {
        String host = MDNS.hostname(i);
        if (isValidShellyHostname(host)) {
            IPAddress ip = MDNS.IP(i);
            discoveredDevices.push_back({ host, ip });
            Serial.print("  ");
            Serial.print(host);
            Serial.print(" at ");
            Serial.println(ip);
        }
    }

    int validIndex = -1;
    for (size_t i = 0; i < discoveredDevices.size(); i++) {
        if (Ping.ping(discoveredDevices[i].ip)) {
            validIndex = i;
            break;
        }
    }
    if (validIndex != -1) {
        discoveredDevices[validIndex].ip.toString().toCharArray(shellyIP, sizeof(shellyIP));
        TIMED_PRINT("Using discovered Shelly device: " + discoveredDevices[validIndex].name + " at " + String(shellyIP));
        Serial.println();
        if (discoveredDevices.size() == 1) {
            TIMED_PRINTLN("Only one valid Shelly device found. Saving as default in config.");
            saveConfigSPIFFS();
        }
    } else {
        TIMED_PRINTLN("No valid Shelly device found via mDNS.");
        return;
    }
    String wsUrl = String("ws://") + shellyIP + "/rpc";
    TIMED_PRINT("Connecting to WebSocket at: " + wsUrl);
    Serial.println();
    if (wsClient.connect(wsUrl)) {
        TIMED_PRINTLN("Connected to Shelly WebSocket after discovery.");
        sendShellyGetStatus();
    } else {
        TIMED_PRINTLN("Failed to connect to Shelly WebSocket after discovery.");
    }
}

void setup() {
    Serial.begin(115200);
    //while (!Serial) { ; } // Uncomment this to prevent further progress without a serial connection (useful for troubleshooting)

    esp_task_wdt_init(60, true);
    esp_task_wdt_add(NULL);

    strip.begin();
    strip.setBrightness(scaledBrightness());
    strip.show();

#ifdef USE_WS2812B_FOR_STATUS
    statusLED.begin();
    statusLED.setPixelColor(0, statusLED.Color(255, 165, 0));
    statusLED.show();
#else
    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, LOW);
#endif

    delay(5000);
    loadConfigSPIFFS();

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
        }
    } else {
        TIMED_PRINTLN("WiFi Connected using stored credentials.");
    }

    if (WiFi.status() == WL_CONNECTED) {
        TIMED_PRINTLN("Connected to WiFi");
        TIMED_PRINT(String("IP: ") + String(WiFi.localIP().toString()));

        configTime(36000, 0, "au.pool.ntp.org");
        struct tm timeinfo;
        while (!getLocalTime(&timeinfo)) {
            TIMED_PRINTLN("Waiting for time sync...");
            delay(1000);
        }
        deviceStartTimeMillis = ((uint64_t)mktime(&timeinfo)) * 1000 + millis();
        TIMED_PRINTLN("Device start time initialized: " + String(mktime(&timeinfo)));

        inSetup = false;  // End setup phase

        delay(2000);

        if (startUniqueMDNS(ShemeterName)) {
            // mDNS responder started successfully
        } else {
            TIMED_PRINTLN("mDNS responder failed to start with a unique name.");
        }

        IPAddress storedShellyIP;
        storedShellyIP = IPAddress();
        if (String(shellyIP).length() > 0) {
            storedShellyIP.fromString(shellyIP);
        }
        bool storedValid = false;
        if (storedShellyIP != IPAddress() && isValidShellyIP(shellyIP)) {
            if (Ping.ping(storedShellyIP)) {
                storedValid = true;
                TIMED_PRINTLN("Stored Shelly IP is reachable.");
            } else {
                TIMED_PRINTLN("Stored Shelly IP is not reachable.");
            }
        }

        struct ShellyDevice { String name; IPAddress ip; };
        std::vector<ShellyDevice> discoveredDevices;
        int n = MDNS.queryService("http", "tcp");
        TIMED_PRINT("mDNS query found ");
        Serial.println(n);
        for (int i = 0; i < n; ++i) {
            String host = MDNS.hostname(i);
            if (host.startsWith("shelly")) {
                IPAddress ip = MDNS.IP(i);
                discoveredDevices.push_back({ host, ip });
                TIMED_PRINT("Found Shelly device: " + host + " at " + ip.toString());
                Serial.println();
            }
        }
        if (!storedValid && discoveredDevices.size() == 1) {
            discoveredDevices[0].ip.toString().toCharArray(shellyIP, sizeof(shellyIP));
            TIMED_PRINT("Using discovered Shelly device: " + discoveredDevices[0].name + " at " + String(shellyIP));
            Serial.println();
            saveConfigSPIFFS();
        } else if (storedValid) {
            TIMED_PRINTLN("Using stored Shelly IP.");
        } else {
            TIMED_PRINTLN("Multiple Shelly devices found or no devices discovered.");
        }
    }

    wsClient.onMessage(onMessageCallback);
    wsClient.onEvent(handleWebSocketEvent);

    if (isValidShellyIP(shellyIP)) {
        String wsUrl = String("ws://") + shellyIP + "/rpc";
        TIMED_PRINT("Connecting to WebSocket at: " + wsUrl);
        Serial.println();
        delay(500);
        if (wsClient.connect(wsUrl)) {
            TIMED_PRINTLN("Connected to Shelly WebSocket");
            sendShellyGetStatus();
        } else {
            TIMED_PRINTLN("Failed to connect to Shelly WebSocket");
        }
    } else {
        TIMED_PRINTLN("No valid Shelly IP available. Skipping WebSocket connection.");
    }

    setupWebServer();
}

void loop() {
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= dataUpdateInterval) {
        previousMillis = currentMillis;
        if (!wsClient.available()) {
            checkAndEstablishWebSocket();
        } else {
            updateEnergyMeterData();
        }
    }

    if (inSetup) {
        blinkPWMLED(LED_STATUS_PIN, 500, 1);
    }



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
