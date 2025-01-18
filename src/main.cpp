#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <SPIFFS.h>
#include <ArduinoWebsockets.h>
#include <esp_task_wdt.h>
#include <ESP32Ping.h> // For ping functionality
#include <map>
#include <vector> // For dynamic device storage
#include "includes.h"

using namespace websockets;

#define DEBUG_LOG 0            // Enable debug logging
#define DISABLE_PERIODIC_RPC 0 // Disable periodic RPC requests after initial connection

// Timestamp function to return formatted time string
String timestamp()
{
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

// Macros for printing with timestamp
#define TIMED_PRINT(x)             \
    {                              \
        Serial.print(timestamp()); \
        Serial.print(" ");         \
        Serial.print(x);           \
    }
#define TIMED_PRINTLN(x)           \
    {                              \
        Serial.print(timestamp()); \
        Serial.print(" ");         \
        Serial.println(x);         \
    }

#define MAX_BRIGHTNESS 255
int globalBrightness = 32;
#define LED_PIN 4
#define LED_COUNT 60
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

#ifndef USE_WS2812B_FOR_STATUS
#define LED_STATUS_PIN 8
#endif

#ifdef USE_WS2812B_FOR_STATUS
#define LED_STATUS_PIN 7
#define LED_STATUS_COUNT 1
Adafruit_NeoPixel statusLED(LED_STATUS_COUNT, LED_STATUS_PIN, NEO_GRB + NEO_KHZ800);
#endif

// Default WiFi and Shelly configuration
const char *defaultSSID = "";
const char *defaultPassword = "";
const char *defaultShellyIP = "";

// Shemeter Name and dynamic Fallback AP credentials
String ShemeterName = "SheMeter";
String fallbackSSID = ShemeterName + "AP";
const char *fallbackPWD = "12345678";

char ssid[32] = "";
char password[64] = "";
char shellyIP[16] = "";

// Timing and State Variables
unsigned long previousMillis = 0;
const long dataUpdateInterval = 1000;
const long loopDelay = 2;
bool newDataAvailable = false;
unsigned long lastDataUpdateTime = 0;
int calculatedValue = 0; // Placeholder if needed

// Energy Meter Structure with embedded lastUpdateTime
struct EnergyMeter
{
    String name;
    int act_power;
    unsigned long lastUpdateTime;
};
EnergyMeter meters[3] = {
    {"Grid", 0, 0},
    {"Solar", 0, 0},
    {"Consumer", 0, 0}};

// Web Server and WebSocket Client
WebServer server(80);
WebsocketsClient wsClient;

// Configuration Path
const char *configPath = "/config.json";

// Pending Requests Map
struct PendingRequest
{
    std::function<void(JsonObject &)> callback;
};
std::map<int, PendingRequest> pendingRequests;
int commandId = 0;

// Function Prototypes
void blinkPWMLED(uint8_t pin, unsigned long interval, int blinks);
bool tryConnectWiFi(const char *ssid, const char *password);
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
int sendRequest(const char *method, JsonVariant params, std::function<void(JsonObject &)> callback);
void onMessageCallback(WebsocketsMessage message);
void handleWebSocketEvent(WebsocketsEvent event, String data);
void sendEM1GetStatusRequests();
void ensureWiFiConnected();
void checkAndEstablishWebSocket();
bool isValidShellyHostname(const String &host);
bool isValidShellyIP(const char *ip);
void updateMeterActPower(int meterIndex, int newPower);

// Function Implementations

bool isValidShellyHostname(const String &host)
{
    String lowerHost = host;
    lowerHost.toLowerCase();
    return lowerHost.startsWith("shelly") && host.indexOf('-') != -1;
}

bool isValidShellyIP(const char *ip)
{
    String ipStr = String(ip);
    return ipStr.length() > 0 && ipStr != "xxx";
}

void blinkPWMLED(uint8_t pin, unsigned long interval, int blinks)
{
    static unsigned long lastBlinkTime = 0;
    static int blinkCount = 0;
    static bool ledState = false;
    unsigned long currentMillis = millis();
    if (blinkCount < blinks * 2)
    {
        if (currentMillis - lastBlinkTime >= interval)
        {
            lastBlinkTime = currentMillis;
            ledState = !ledState;
            digitalWrite(pin, ledState ? HIGH : LOW);
            if (!ledState)
                blinkCount++;
        }
    }
    else
    {
        blinkCount = 0;
        digitalWrite(pin, LOW);
    }
}

bool tryConnectWiFi(const char *ssid, const char *password)
{
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    unsigned long startTime = millis();
    TIMED_PRINT("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000)
    {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED)
    {
        TIMED_PRINTLN("WiFi Connected!");
        TIMED_PRINTLN("IP Address: " + String(WiFi.localIP()));
        return true;
    }
    else
    {
        TIMED_PRINTLN("Failed to connect to WiFi.");
        return false;
    }
}

int scaledBrightness(int brightness)
{
    if (brightness <= 0)
        return 0;
    if (brightness >= MAX_BRIGHTNESS)
        return MAX_BRIGHTNESS;
    float scale = (float)globalBrightness / MAX_BRIGHTNESS;
    int newBrightness = (int)(brightness * scale);
    return newBrightness <= 0 ? 1 : min(newBrightness, MAX_BRIGHTNESS);
}

int scaledBrightness() { return scaledBrightness(globalBrightness); }

void displayMetricsOnStrip()
{
    int consumerValue = 0;
    int solarValue = 0;

    // Dynamically identify meters based on their names
    for (int i = 0; i < 3; i++)
    {
        if (meters[i].name.equalsIgnoreCase("Consumer"))
        {
            consumerValue = meters[i].act_power;
        }
        else if (meters[i].name.equalsIgnoreCase("Solar"))
        {
            solarValue = meters[i].act_power;
        }
    }

    for (int i = 0; i < LED_COUNT; i++)
    {
        int ledValue = map(i, 0, LED_COUNT - 1, 100, 5000);
        if (ledValue <= consumerValue)
        {
            if (ledValue <= solarValue)
            {
                strip.setPixelColor(i, strip.Color(scaledBrightness(153), scaledBrightness(255), 0, 0));
            }
            else
            {
                strip.setPixelColor(i, strip.Color(scaledBrightness(255), 0, 0, 0));
            }
        }
        else if (ledValue <= solarValue)
        {
            strip.setPixelColor(i, strip.Color(0, scaledBrightness(204), scaledBrightness(255), 0));
        }
        else
        {
            strip.setPixelColor(i, strip.Color(0, 0, 0, 0));
        }
    }
    strip.show();
}

void handleRoot() { server.send_P(200, "text/html", index_html); }

void handleJson()
{
    unsigned long clientTimestamp = server.arg("timestamp").toInt();
    String json = "[";
    for (int i = 0; i < 3; i++)
    {
        if (i > 0)
            json += ",";
        json += "{\"name\":\"" + meters[i].name + "\",\"power\":" + String(meters[i].act_power) + "}";
    }
    json += "]";
    server.send(200, "application/json", json);
}

bool loadConfigSPIFFS()
{
    if (!SPIFFS.begin(true))
    {
        TIMED_PRINTLN("SPIFFS Mount Failed");
        return false;
    }
    if (!SPIFFS.exists(configPath))
    {
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
        ShemeterName = "SheMeter";
        saveConfigSPIFFS();
        return true;
    }
    File file = SPIFFS.open(configPath, "r");
    if (!file)
    {
        TIMED_PRINTLN("Failed to open config file");
        return false;
    }
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error)
    {
        TIMED_PRINTLN("Failed to parse config file");
        return false;
    }
    const char *s = doc["ssid"];
    const char *p = doc["password"];
    const char *sp = doc["shellyIP"];
    const char *sheName = doc["shemeterName"];
    strncpy(ssid, s ? s : defaultSSID, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    strncpy(password, p ? p : defaultPassword, sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
    strncpy(shellyIP, sp ? sp : defaultShellyIP, sizeof(shellyIP) - 1);
    shellyIP[sizeof(shellyIP) - 1] = '\0';
    ShemeterName = sheName ? String(sheName) : "SheMeter";

    if (doc.containsKey("meters") && doc["meters"].is<JsonArray>())
    {
        JsonArray meterArray = doc["meters"].as<JsonArray>();
        int index = 0;
        for (auto meterName : meterArray)
        {
            if (index < 3 && meterName.is<const char *>())
            {
                meters[index].name = String((const char *)meterName);
                index++;
            }
        }
        while (index < 3)
        {
            if (index == 0)
                meters[index].name = "Grid";
            else if (index == 1)
                meters[index].name = "Solar";
            else
                meters[index].name = "Consumer";
            index++;
        }
    }
    else
    {
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
    for (int i = 0; i < 3; i++)
    {
        meterNames += meters[i].name;
        if (i < 2)
            meterNames += ", ";
    }
    TIMED_PRINTLN(meterNames);
    return true;
}

bool saveConfigSPIFFS()
{
    StaticJsonDocument<512> doc;
    doc["ssid"] = ssid;
    doc["password"] = password;
    doc["shellyIP"] = shellyIP;
    doc["shemeterName"] = ShemeterName;
    JsonArray meterArray = doc.createNestedArray("meters");
    for (int i = 0; i < 3; i++)
    {
        meterArray.add(meters[i].name);
    }
    File file = SPIFFS.open(configPath, "w");
    if (!file)
    {
        TIMED_PRINTLN("Failed to open config file for writing");
        return false;
    }
    if (serializeJson(doc, file) == 0)
    {
        TIMED_PRINTLN("Failed to write to file");
        file.close();
        return false;
    }
    file.close();
    TIMED_PRINTLN("Configuration saved to SPIFFS.");
    return true;
}

void handleConfig()
{
    if (server.method() == HTTP_POST)
    {
        String newSSID = server.arg("ssid");
        String newPassword = server.arg("password");
        String newShellyIP = server.arg("shellyIP");
        String newSheMeterName = server.arg("shemeterName");
        String meter0Role = server.arg("meter0");
        String meter1Role = server.arg("meter1");
        String meter2Role = server.arg("meter2");

        bool valid = true;
        String errorMsg = "";

        // Validate meter roles
        if (meter0Role != "Grid" && meter0Role != "Solar" && meter0Role != "Consumer")
        {
            valid = false;
            errorMsg = "Invalid role selected for Meter 0.";
        }
        if (meter1Role != "Grid" && meter1Role != "Solar" && meter1Role != "Consumer")
        {
            valid = false;
            errorMsg = "Invalid role selected for Meter 1.";
        }
        if (meter2Role != "Grid" && meter2Role != "Solar" && meter2Role != "Consumer")
        {
            valid = false;
            errorMsg = "Invalid role selected for Meter 2.";
        }
        if (valid)
        {
            if (meter0Role == meter1Role || meter0Role == meter2Role || meter1Role == meter2Role)
            {
                valid = false;
                errorMsg = "Selected roles must be unique for each meter.";
            }
        }

        if (valid)
        {
            newSSID.toCharArray(ssid, sizeof(ssid));
            newPassword.toCharArray(password, sizeof(password));
            newShellyIP.toCharArray(shellyIP, sizeof(shellyIP));
            ShemeterName = newSheMeterName;
            fallbackSSID = ShemeterName + "AP";

            meters[0].name = meter0Role;
            meters[1].name = meter1Role;
            meters[2].name = meter2Role;

            saveConfigSPIFFS();
            TIMED_PRINTLN("Configuration updated via web interface.");
            server.sendHeader("Location", "/");
            server.send(303);
        }
        else
        {
            String html = "<!DOCTYPE html><html><body>";
            html += "<h1>Configuration Error</h1>";
            html += "<p>" + errorMsg + "</p>";
            html += "<a href=\"/config\">Go Back</a>";
            html += "</body></html>";
            server.send(400, "text/html", html);
        }
    }
    else
    {
        String html = "<!DOCTYPE html><html><body>";
        html += "<h1>Configuration</h1>";
        html += "<form action='/config' method='post'>";

        html += "SSID: <input type='text' name='ssid' value='" + String(ssid) + "'><br>";
        html += "Password: <input type='password' name='password' value='" + String(password) + "'><br>";
        html += "Shelly IP: <input type='text' name='shellyIP' value='" + String(shellyIP) + "'><br>";
        html += "SheMeter Name: <input type='text' name='shemeterName' value='" + ShemeterName + "'><br>";

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

        html += "<input type='submit' value='Save'>";
        html += "</form>";
        html += "</body></html>";
        server.send(200, "text/html", html);
    }
}

void setupWebServer()
{
    server.on("/", handleRoot);
    server.on("/data", handleJson);
    server.on("/config", handleConfig);
    server.begin();
    TIMED_PRINTLN("HTTP server started");
}

void checkWiFiConnection()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        TIMED_PRINTLN("WiFi connection lost. Attempting to reconnect...");
        if (tryConnectWiFi(ssid, password))
        {
            TIMED_PRINTLN("WiFi reconnected.");
        }
        else
        {
            TIMED_PRINTLN("Reconnection failed.");
        }
    }
}

int sendRequest(const char *method, JsonVariant params, std::function<void(JsonObject &)> callback)
{
    commandId++;
    StaticJsonDocument<256> doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = commandId;
    doc["src"] = "arduino_client";
    doc["method"] = method;
    if (!params.isNull())
    {
        doc["params"] = params;
    }
    String request;
    serializeJson(doc, request);
    TIMED_PRINTLN("Sending RPC Request (ID: " + String(commandId) + "): " + request);
    pendingRequests[commandId] = {callback};
    wsClient.send(request);
    return commandId;
}

void updateMeterActPower(int meterIndex, int newPower)
{
    // Invert sign for Solar meter
    if (meters[meterIndex].name.equalsIgnoreCase("Solar"))
    {
        newPower = -newPower;
    }
    if (meters[meterIndex].act_power != newPower)
    {
        meters[meterIndex].act_power = newPower;
        meters[meterIndex].lastUpdateTime = millis();
        newDataAvailable = true;
        TIMED_PRINTLN("Updated " + meters[meterIndex].name + " act_power: " + String(newPower) + " W");
    }
}

void onMessageCallback(WebsocketsMessage message)
{
    TIMED_PRINTLN("Received WebSocket message:");
    TIMED_PRINTLN(message.data());
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, message.data());
    if (error)
    {
        TIMED_PRINTLN(String("Failed to parse response: ") + error.f_str());
        return;
    }
    if (doc.containsKey("id"))
    {
        int id = doc["id"];
        if (pendingRequests.find(id) != pendingRequests.end())
        {
            JsonObject response = doc.as<JsonObject>();
            pendingRequests[id].callback(response);
            pendingRequests.erase(id);
            return;
        }
    }
    if (doc.containsKey("method"))
    {
        String method = doc["method"];
        if (method == "NotifyStatus" || method == "NotifyFullStatus")
        {
            TIMED_PRINTLN("Received Notification:");
            JsonObject params = doc["params"].as<JsonObject>();
            bool anyUpdate = false;
            for (int j = 0; j < 3; j++)
            {
                String key = "em1:" + String(j);
                if (params.containsKey(key))
                {
                    JsonObject obj = params[key].as<JsonObject>();
                    if (obj.containsKey("act_power"))
                    {
                        int newPower = obj["act_power"].as<int>();
                        int oldPower = meters[j].act_power;
                        updateMeterActPower(j, newPower);
                        if (meters[j].act_power != oldPower)
                        {
                            anyUpdate = true;
                        }
                    }
                }
            }
            if (anyUpdate)
            {
                lastDataUpdateTime = millis();
            }
        }
        else if (method == "NotifyEvent")
        {
            // Ignore NotifyEvent frames.
        }
    }
    else
    {
        TIMED_PRINTLN("Received unsolicited message:");
        serializeJsonPretty(doc, Serial);
        Serial.println();
    }
}

void handleWebSocketEvent(WebsocketsEvent event, String data)
{
    switch (event)
    {
    case WebsocketsEvent::ConnectionOpened:
        TIMED_PRINTLN("WebSocket connection opened.");
        break;
    case WebsocketsEvent::ConnectionClosed:
        TIMED_PRINTLN("WebSocket connection closed.");
        break;
    case WebsocketsEvent::GotPing:
        TIMED_PRINTLN("WebSocket ping received.");
        wsClient.pong(); // Respond with pong
        break;
    case WebsocketsEvent::GotPong:
        TIMED_PRINTLN("WebSocket pong received.");
        break;
    }
}

void sendEM1GetStatusRequests()
{
    for (int i = 0; i < 3; i++)
    {
        if (millis() - meters[i].lastUpdateTime > 2000)
        {
            StaticJsonDocument<64> params;
            params["id"] = i;
            sendRequest("EM1.GetStatus", params.as<JsonVariant>(), [i](JsonObject &response)
                        {
                if (response.containsKey("result")) {
                    JsonObject result = response["result"];
                    TIMED_PRINTLN("EM1.GetStatus Response for Meter ID " + String(i));
                    if(result.containsKey("act_power")) {
                        int newPower = result["act_power"].as<int>();
                        updateMeterActPower(i, newPower);
                        TIMED_PRINTLN("Active Power: " + String(meters[i].act_power) + " W");
                    }
                } else if (response.containsKey("error")) {
                    JsonObject error = response["error"];
                    TIMED_PRINTLN("Error in EM1.GetStatus response for Meter ID " + String(i));
                    TIMED_PRINTLN("Code: " + String(error["code"].as<int>()));
                    TIMED_PRINTLN("Message: " + String(error["message"].as<const char*>()));
                } else {
                    TIMED_PRINTLN("Unexpected EM1.GetStatus response:");
                    serializeJsonPretty(response, Serial);
                    Serial.println();
                } });
        }
    }
}

void updateEnergyMeterData()
{
    if (!wsClient.available())
    {
        TIMED_PRINTLN("WebSocket unavailable during update.");
        return;
    }
    sendEM1GetStatusRequests();
}

void ensureWiFiConnected()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        TIMED_PRINTLN("WiFi connection lost. Attempting to reconnect...");
        if (tryConnectWiFi(ssid, password))
        {
            TIMED_PRINTLN("WiFi reconnected.");
        }
        else
        {
            TIMED_PRINTLN("Reconnection failed.");
        }
    }
}

void checkAndEstablishWebSocket()
{
    ensureWiFiConnected();
    IPAddress storedIP;
    bool validStored = false;
    if (String(shellyIP).length() > 0 && isValidShellyIP(shellyIP))
    {
        storedIP.fromString(shellyIP);
        if (Ping.ping(storedIP))
        {
            validStored = true;
            TIMED_PRINTLN("Stored Shelly IP is reachable.");
        }
        else
        {
            TIMED_PRINTLN("Stored Shelly IP is not reachable.");
        }
    }
    if (validStored)
    {
        String wsUrl = String("ws://") + shellyIP + "/rpc";
        TIMED_PRINT("Attempting to reconnect to WebSocket at: " + wsUrl);
        Serial.println();
        if (wsClient.connect(wsUrl))
        {
            TIMED_PRINTLN("Reconnected to WebSocket using stored Shelly IP.");
            sendEM1GetStatusRequests();
            return;
        }
        else
        {
            TIMED_PRINTLN("Failed to reconnect using stored Shelly IP.");
        }
    }

    delay(2000); // Stabilize network before mDNS

    TIMED_PRINTLN("Attempting Shelly discovery via mDNS...");

#if DEBUG_LOG
    int nAll = MDNS.queryService("http", "tcp");
    Serial.print(timestamp() + " All mDNS services discovered: ");
    Serial.println(nAll);
    for (int j = 0; j < nAll; j++)
    {
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
    std::vector<std::pair<String, IPAddress>> discoveredDevices;
    for (int i = 0; i < nServices; ++i)
    {
        String host = MDNS.hostname(i);
        if (isValidShellyHostname(host))
        {
            IPAddress ip = MDNS.IP(i);
            discoveredDevices.push_back(std::make_pair(host, ip));
            Serial.print("  ");
            Serial.print(host);
            Serial.print(" at ");
            Serial.println(ip);
        }
    }

    int validIndex = -1;
    for (size_t i = 0; i < discoveredDevices.size(); i++)
    {
        if (Ping.ping(discoveredDevices[i].second))
        {
            validIndex = i;
            break;
        }
    }
    if (validIndex != -1)
    {
        discoveredDevices[validIndex].second.toString().toCharArray(shellyIP, sizeof(shellyIP));
        TIMED_PRINT("Using discovered Shelly device: " + discoveredDevices[validIndex].first + " at " + String(shellyIP));
        Serial.println();
        if (discoveredDevices.size() == 1)
        {
            TIMED_PRINTLN("Only one valid Shelly device found. Saving as default in config.");
            saveConfigSPIFFS();
        }
    }
    else
    {
        TIMED_PRINTLN("No valid Shelly device found via mDNS.");
        return;
    }
    String wsUrl = String("ws://") + shellyIP + "/rpc";
    TIMED_PRINT("Connecting to WebSocket at: " + wsUrl);
    Serial.println();
    if (wsClient.connect(wsUrl))
    {
        TIMED_PRINTLN("Connected to Shelly WebSocket after discovery.");
        sendEM1GetStatusRequests();
    }
    else
    {
        TIMED_PRINTLN("Failed to connect to Shelly WebSocket after discovery.");
    }
}

void setup()
{
    Serial.begin(115200);
    while (!Serial)
    {
        ;
    }

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

    delay(2000);

    loadConfigSPIFFS();

    TIMED_PRINTLN("Attempting to connect using stored WiFi credentials...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    delay(3500);

    if (WiFi.status() != WL_CONNECTED)
    {
        TIMED_PRINTLN("Stored WiFi credentials failed. Starting SmartConfig...");
        WiFi.mode(WIFI_AP_STA);
        WiFi.beginSmartConfig();
        unsigned long smartconfigStart = millis();
        while (!WiFi.smartConfigDone() && (millis() - smartconfigStart) < 300000)
        {
            delay(500);
            esp_task_wdt_reset();
            Serial.print(".");
        }
        if (WiFi.smartConfigDone())
        {
            TIMED_PRINTLN("SmartConfig successful.");
            while (WiFi.status() != WL_CONNECTED)
            {
                delay(500);
                esp_task_wdt_reset();
                Serial.print(".");
            }
            TIMED_PRINTLN("WiFi Connected.");
            TIMED_PRINT("IP Address: ");
            TIMED_PRINTLN(WiFi.localIP());
            strncpy(ssid, WiFi.SSID().c_str(), sizeof(ssid) - 1);
            ssid[sizeof(ssid) - 1] = '\0';
            strncpy(password, WiFi.psk().c_str(), sizeof(password) - 1);
            password[sizeof(password) - 1] = '\0';
            saveConfigSPIFFS();
        }
        else
        {
            TIMED_PRINTLN("SmartConfig failed. Starting fallback AP mode.");
            WiFi.mode(WIFI_AP);
            WiFi.softAP(fallbackSSID.c_str(), fallbackPWD);
        }
    }
    else
    {
        TIMED_PRINTLN("WiFi Connected using stored credentials.");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        TIMED_PRINTLN("Connected to WiFi");
        TIMED_PRINT("IP: " + String(WiFi.localIP()));

        delay(2000); // Delay for network stabilization

        if (MDNS.begin("SheMeter"))
        {
            TIMED_PRINTLN("mDNS responder started as SheMeter.local");
        }
        else
        {
            TIMED_PRINTLN("Error setting up mDNS responder!");
        }

        IPAddress storedShellyIP;
        storedShellyIP = IPAddress();
        if (String(shellyIP).length() > 0)
        {
            storedShellyIP.fromString(shellyIP);
        }
        bool storedValid = false;
        if (storedShellyIP != IPAddress() && isValidShellyIP(shellyIP))
        {
            if (Ping.ping(storedShellyIP))
            {
                storedValid = true;
                TIMED_PRINTLN("Stored Shelly IP is reachable.");
            }
            else
            {
                TIMED_PRINTLN("Stored Shelly IP is not reachable.");
            }
        }
        const int maxDevices = 10;
        struct ShellyDevice
        {
            String name;
            IPAddress ip;
        };
        ShellyDevice discoveredDevices[maxDevices];
        int deviceCount = 0;
        int n = MDNS.queryService("http", "tcp");
        TIMED_PRINT("mDNS query found ");
        Serial.println(n);
        for (int i = 0; i < n && deviceCount < maxDevices; ++i)
        {
            String host = MDNS.hostname(i);
            if (host.startsWith("shelly"))
            {
                discoveredDevices[deviceCount].name = host;
                discoveredDevices[deviceCount].ip = MDNS.IP(i);
                TIMED_PRINT("Found Shelly device: " + host + " at " + discoveredDevices[deviceCount].ip.toString());
                Serial.println();
                deviceCount++;
            }
        }
        if (!storedValid && deviceCount == 1)
        {
            discoveredDevices[0].ip.toString().toCharArray(shellyIP, sizeof(shellyIP));
            TIMED_PRINT("Using discovered Shelly device: " + discoveredDevices[0].name + " at " + String(shellyIP));
            Serial.println();
        }
        else if (storedValid)
        {
            TIMED_PRINTLN("Using stored Shelly IP.");
        }
        else
        {
            TIMED_PRINTLN("Multiple Shelly devices found or no devices discovered.");
        }
    }

    wsClient.onMessage(onMessageCallback);
    wsClient.onEvent(handleWebSocketEvent);

    if (isValidShellyIP(shellyIP))
    {
        String wsUrl = String("ws://") + shellyIP + "/rpc";
        TIMED_PRINT("Connecting to WebSocket at: " + wsUrl);
        Serial.println();
        if (wsClient.connect(wsUrl))
        {
            TIMED_PRINTLN("Connected to Shelly WebSocket");
            sendEM1GetStatusRequests();
        }
        else
        {
            TIMED_PRINTLN("Failed to connect to Shelly WebSocket");
        }
    }
    else
    {
        TIMED_PRINTLN("No valid Shelly IP available. Skipping WebSocket connection.");
    }

    setupWebServer();
}

void loop()
{
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= dataUpdateInterval)
    {
        previousMillis = currentMillis;
        if (!wsClient.available())
        {
            checkAndEstablishWebSocket();
        }
#if !DISABLE_PERIODIC_RPC
        else
        {
            updateEnergyMeterData();
        }
#endif
    }

    displayMetricsOnStrip();

    if (newDataAvailable)
    {
        String logMsg = "Energy meter data updated: ";
        for (int i = 0; i < 3; i++)
        {
            logMsg += meters[i].name + ": " + String(meters[i].act_power) + "W, ";
        }

        int gridVal = 0;
        int solarVal = 0;
        for (int i = 0; i < 3; i++)
        {
            if (meters[i].name.equalsIgnoreCase("Grid"))
            {
                gridVal = meters[i].act_power;
            }
            else if (meters[i].name.equalsIgnoreCase("Solar"))
            {
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
