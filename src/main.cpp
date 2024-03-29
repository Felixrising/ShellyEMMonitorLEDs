#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>

// #define USE_WS2812B_FOR_STATUS // Uncomment for WS2812B LED, comment out for regular PWM LED

#define MAX_BRIGHTNESS 255
int globalBrightness = 32; // Example global led brightness as a %
#define LED_PIN 4
#define LED_COUNT 60
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

#ifndef USE_WS2812B_FOR_STATUS
#define LED_STATUS_PIN 8 // Regular PWM LED on GPIO8 for status
#endif

#ifdef USE_WS2812B_FOR_STATUS
#define LED_STATUS_PIN 7 // WS2812B LED on GPIO7 for status
#define LED_STATUS_COUNT 1
Adafruit_NeoPixel statusLED(LED_STATUS_COUNT, LED_STATUS_PIN, NEO_GRB + NEO_KHZ800);
#endif

const char *defaultSSID = "xxxxx";
const char *defaultPassword = "xxxxx";
const char *defaultShellyIP = "xxx";

char ssid[32] = "";
char password[64] = "";
char shellyIP[16] = "";

unsigned long previousMillis = 0;
const long dataUpdateInterval = 1000; // 1 second
const long loopDelay = 2;             // Let CPUs do some background work and not block.
unsigned long lastDataUpdateTime = 0;
unsigned long DataUpdateTime = 0;
bool newDataAvailable = false; // Track when new data is available from energy meter.
int calculatedValue = 0;       // Variable for storing the calculated value

struct EnergyMeter
{
    String name;
    int act_power;
};

EnergyMeter meters[3] = {
    {"Grid", 0},
    {"Solar", 0},
    {"Consumer", 0}};

WebServer server(80);

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
            digitalWrite(pin, ledState ? HIGH : LOW); // Toggle LED state
            if (!ledState)
            {
                blinkCount++;
            }
        }
    }
    else
    {
        blinkCount = 0;         // Reset for the next sequence
        digitalWrite(pin, LOW); // Ensure LED is off after blinking
    }
}

void reconnectWiFi(const char *newSSID, const char *newPassword)
{
    WiFi.disconnect();
    WiFi.begin(newSSID, newPassword);
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000)
    {
        delay(500);
        Serial.print(".");
    }
}

void updateEnergyMeterData()
{
    bool allDataFetched = true; // Start with the assumption that all data will be fetched
    calculatedValue = 0;        // Variable for storing the calculated value

    HTTPClient http;
    for (int i = 0; i < 3; i++)
    {
#ifdef USE_WS2812B_FOR_STATUS
        statusLED.setPixelColor(0, statusLED.Color(0, 0, 0)); // Black color
        statusLED.show();
#else
        blinkPWMLED(LED_STATUS_PIN, 500, 1); // One blink for start
#endif

        String url = "http://" + String(shellyIP) + "/rpc/EM1.GetStatus?id=" + String(i);
        http.begin(url);
        int httpCode = http.GET();

        if (httpCode > 0)
        {
            String payload = http.getString();
            DynamicJsonDocument doc(1024);
            deserializeJson(doc, payload);
            meters[i].act_power = (int)doc["act_power"].as<float>();

            if (meters[i].name == "Solar")
            {
                meters[i].act_power = -meters[i].act_power;
            }

#ifdef USE_WS2812B_FOR_STATUS
            statusLED.setPixelColor(0, statusLED.Color(0, 255, 0)); // Green color for success
            statusLED.show();
#else
            blinkPWMLED(LED_STATUS_PIN, 500, 3); // Three blinks for success
#endif
        }
        else
        {
            allDataFetched = false; // Set to false if any data fetch fails
            // Optionally, break out of the loop if one fails
            break;
        }
        http.end();
    }

    if (allDataFetched)
    {
        // Compute the calculatedValue here after all data is fetched
        calculatedValue = meters[1].act_power + meters[0].act_power; // Assuming meters[1] is solar and meters[0] is grid

        newDataAvailable = true; // Update the flag only if all data is successfully fetched
        DataUpdateTime = millis();
        Serial.print("Data update inverval: " + String(DataUpdateTime - lastDataUpdateTime) + ", ");
        lastDataUpdateTime = DataUpdateTime; // Update timestamp after fetching new data
    }
    else
    {
        newDataAvailable = false; // Ensure the flag is not set if data fetching fails
    }

#ifdef USE_WS2812B_FOR_STATUS
    statusLED.setPixelColor(0, statusLED.Color(0, 128, 0)); // Green color
    statusLED.show();
#else
    blinkPWMLED(LED_STATUS_PIN, 500, 2); // Two blinks to indicate end of data fetching
#endif
}

bool tryConnectWiFi(const char *ssid, const char *password)
{
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000)
    {
        delay(500);
        Serial.print(".");
    }
    return WiFi.status() == WL_CONNECTED;
}

int scaledBrightness(int brightness)
{
    if (brightness <= 0)
        return 0;
    if (brightness >= MAX_BRIGHTNESS)
        return MAX_BRIGHTNESS;

    // Use floating-point arithmetic for precision
    float scale = (float)globalBrightness / MAX_BRIGHTNESS;
    int newBrightness = (int)(brightness * scale);

    // Optional: ensure minimum brightness of 1
    if (newBrightness <= 0)
        return 1;

    return min(newBrightness, MAX_BRIGHTNESS);
}

int scaledBrightness()
{
    return scaledBrightness(globalBrightness);
}

// void displayMetricsOnStrip() {
//     int consumerValue = meters[2].act_power;
//     int solarValue = meters[1].act_power;
//     for (int i = 0; i < LED_COUNT; i++) {
//         int ledValue = map(i, 0, LED_COUNT - 1, 100, 5000);
//         if (ledValue <= consumerValue) {
//             if (ledValue <= solarValue) {
//                 strip.setPixelColor(i, strip.Color(153, 255, 0, 0)); // Spring Bud Green  #99ff00 (Consumer proportion covered by Solar)
//             } else {
//                 strip.setPixelColor(i, strip.Color(255, 102, 0, 0)); // Light Orange #FF6600 (consumer only - i.e. Power from Grid)
//             }
//         } else if (ledValue <= solarValue) {
//             strip.setPixelColor(i, strip.Color(0, 204, 255, 0)); // Vivid Sky Blue #00CCFF (Solar only - excess as grid feed-in)
//         } else {
//             strip.setPixelColor(i, strip.Color(0, 0, 0, 0)); // Off
//         }
//     }
//     strip.show(); // Update the LED strip
// }

void displayMetricsOnStrip()
{
    int consumerValue = meters[2].act_power;
    int solarValue = meters[1].act_power;
    for (int i = 0; i < LED_COUNT; i++)
    {
        int ledValue = map(i, 0, LED_COUNT - 1, 100, 5000);
        if (ledValue <= consumerValue)
        {
            if (ledValue <= solarValue)
            { // both values are less than or equal to solarValue. got to be green!
                // Spring Bud Green
                strip.setPixelColor(i, strip.Color(
                                           scaledBrightness(153),
                                           scaledBrightness(255),
                                           0, 0));
            }
            else
            { // else consumerValue exceeds solarValue
                // Light Orange
                strip.setPixelColor(i, strip.Color(
                                           scaledBrightness(255),
                                           0,
                                           0, 0));
            }
        }
        else if (ledValue <= solarValue)
        { // solarValue exceeds consumerValue then mark LEDs blue.
            // Vivid Sky Blue
            strip.setPixelColor(i, strip.Color(
                                       0,
                                       scaledBrightness(204),
                                       scaledBrightness(255),
                                       0));
        }
        else
        {
            strip.setPixelColor(i, strip.Color(0, 0, 0, 0)); // Off
        }
    }
    strip.show(); // Update the LED strip
}

void handleRoot()
{
    String html = "<!DOCTYPE html><html><head><script src='https://cdn.jsdelivr.net/npm/chart.js'></script></head><body>";
    html += "<h1>Energy Meter Data</h1>";
    html += "<div id='latestValues'></div>";
    html += "<div id='calculatedValue'></div>";
    html += "<div class='chart-container' style='position: relative; height:600px; width:1000px;'>";
    html += "<canvas id='energyChart'></canvas>";
    html += "</div>";
    html += "<script>";
    html += "var lastUpdateTime = 0;";
    html += "var ctx = document.getElementById('energyChart').getContext('2d');";
    html += "var energyChart = new Chart(ctx, {type: 'line', data: {labels: [], datasets: [{label: 'Grid', data: [], borderColor: 'red', fill: false}, {label: 'Solar', data: [], borderColor: 'green', fill: false}, {label: 'Consumer', data: [], borderColor: 'blue', fill: false}]}, options: {responsive: true, maintainAspectRatio: false}});";
    html += "function fetchData() {";
    html += "console.log('Fetching data... Timestamp:', lastUpdateTime);";
    html += "fetch('/data').then(response => response.json()).then(data => {";
    html += "document.getElementById('latestValues').innerHTML = 'Grid: ' + data[0].power + 'W, Solar: ' + data[1].power + 'W, Consumer: ' + data[2].power + 'W';";
    html += "var calcValue = data[1].power + data[0].power;";
    html += "document.getElementById('calculatedValue').innerHTML = 'Calculated Consumer: ' + calcValue + 'W';";
    html += "energyChart.data.labels.push(new Date().toLocaleTimeString());";
    html += "energyChart.data.datasets[0].data.push(data[0].power);";
    html += "energyChart.data.datasets[1].data.push(data[1].power);";
    html += "energyChart.data.datasets[2].data.push(data[2].power);";
    html += "energyChart.update();";
    html += "}).catch(error => console.log(error));";
    html += "}";
    html += "setInterval(fetchData, " + String(dataUpdateInterval) + ");";
    html += "fetchData();"; // Initial fetch
    html += "</script>";
    html += "<button onclick=\"location.href='/config'\">Configuration</button>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void saveConfig()
{
    EEPROM.begin(512);
    bool isConfigured = true;
    EEPROM.put(0, isConfigured);
    EEPROM.put(1, ssid);
    EEPROM.put(33, password);
    EEPROM.put(97, shellyIP);
    EEPROM.commit();
    EEPROM.end();
}

void handleConfig()
{
    if (server.method() == HTTP_POST)
    {
        char newSSID[32] = "";
        char newPassword[64] = "";
        strncpy(newSSID, server.arg("ssid").c_str(), sizeof(newSSID));
        strncpy(newPassword, server.arg("password").c_str(), sizeof(newPassword));

        if (tryConnectWiFi(newSSID, newPassword))
        {
            strncpy(ssid, newSSID, sizeof(ssid));
            strncpy(password, newPassword, sizeof(password));
            strncpy(shellyIP, server.arg("shellyIP").c_str(), sizeof(shellyIP));
            saveConfig();
            Serial.println("\nConnected to new WiFi network.");
            Serial.print("New IP Address: ");
            Serial.println(WiFi.localIP());
        }
        else
        {
            Serial.println("\nFailed to connect to new WiFi network. Reverting to old credentials.");
            tryConnectWiFi(ssid, password); // Attempt to reconnect with old credentials
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
        }

        server.sendHeader("Location", "/");
        server.send(303);
    }
    else
    {
        String html = "<!DOCTYPE html><html><body>";
        html += "<form action='/config' method='post'>";
        html += "SSID: <input type='text' name='ssid' value='" + String(ssid) + "'><br>";
        html += "Password: <input type='text' name='password' value='" + String(password) + "'><br>";
        html += "Shelly IP: <input type='text' name='shellyIP' value='" + String(shellyIP) + "'><br>";
        html += "<input type='submit' value='Save'>";
        html += "</form>";
        html += "</body></html>";
        server.send(200, "text/html", html);
    }
}

void clearEEPROM()
{
// Indicate EEPROM clearing process
#ifdef USE_WS2812B_FOR_STATUS
    statusLED.begin();                                        // Ensure the LED is initialized
    statusLED.setPixelColor(0, statusLED.Color(255, 255, 0)); // Set WS2812B LED to yellow for EEPROM clearing indication
    statusLED.show();
    delay(1000); // Keep the LED on for a duration to indicate the process
#else
    pinMode(LED_STATUS_PIN, OUTPUT);
    // Blink PWM LED rapidly to indicate EEPROM clearing
    for (int i = 0; i < 10; i++)
    {
        digitalWrite(LED_STATUS_PIN, HIGH);
        delay(100); // On duration
        digitalWrite(LED_STATUS_PIN, LOW);
        delay(100); // Off duration
    }
#endif

    EEPROM.begin(512);
    for (int i = 0; i < 512; i++)
    {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
    EEPROM.end();

// Indicate EEPROM has been cleared
#ifdef USE_WS2812B_FOR_STATUS
    statusLED.setPixelColor(0, statusLED.Color(0, 255, 0)); // Set WS2812B LED to green to indicate completion
    statusLED.show();
    delay(1000); // Keep the LED on for a duration to indicate completion
#else
    // Turn on PWM LED solid for a short duration to indicate completion
    digitalWrite(LED_STATUS_PIN, HIGH);
    delay(2000); // On duration to indicate completion
    digitalWrite(LED_STATUS_PIN, LOW);
#endif
}

void loadConfig()
{
    EEPROM.begin(512);
    bool isConfigured;
    EEPROM.get(0, isConfigured);
    if (!isConfigured)
    {
        strncpy(ssid, defaultSSID, sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0'; // Ensure null-termination
        strncpy(password, defaultPassword, sizeof(password) - 1);
        password[sizeof(password) - 1] = '\0'; // Ensure null-termination
        strncpy(shellyIP, defaultShellyIP, sizeof(shellyIP) - 1);
        shellyIP[sizeof(shellyIP) - 1] = '\0'; // Ensure null-termination
        saveConfig();
    }
    else
    {
        EEPROM.get(1, ssid);
        ssid[sizeof(ssid) - 1] = '\0'; // Ensure null-termination
        EEPROM.get(33, password);
        password[sizeof(password) - 1] = '\0'; // Ensure null-termination
        EEPROM.get(97, shellyIP);
        shellyIP[sizeof(shellyIP) - 1] = '\0'; // Ensure null-termination
    }
    EEPROM.end();
}

void handleJson()
{
    unsigned long clientTimestamp = server.arg("timestamp").toInt();
    if (clientTimestamp < lastDataUpdateTime)
    {
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
    else
    {
        // No new data since client's last update
        server.send(204); // No Content
    }
}

void setupWebServer()
{
    server.on("/", handleRoot);
    server.on("/data", handleJson);
    server.on("/config", handleConfig);
    server.begin();
    Serial.println("HTTP server started");
}

void setup()
{
    Serial.begin(115200);

    // Initialize the main LED strip
    strip.begin();
    strip.setBrightness(scaledBrightness());
    strip.show(); // Clear the strip

    // Status LED setup for WS2812B or PWM LED
#ifdef USE_WS2812B_FOR_STATUS
    statusLED.begin();
    statusLED.setPixelColor(0, statusLED.Color(255, 165, 0)); // Orange color for WS2812B
    statusLED.show();
#else
    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, LOW); // Ensure PWM LED is off initially
#endif

    delay(5000); // Ensure serial connection is established for monitoring

    // Uncomment the next line to clear EEPROM during troubleshooting
    // clearEEPROM();

    loadConfig(); // Load configuration

    Serial.println("Attempting to connect to WiFi...");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("Password: ");
    Serial.println(password);
    Serial.print("Shelly IP: ");
    Serial.println(shellyIP);

    // Indicate WiFi connection attempt
#ifdef USE_WS2812B_FOR_STATUS
    statusLED.setPixelColor(0, statusLED.Color(255, 255, 0)); // Yellow color for WS2812B during connection attempt
    statusLED.show();
#else
    // For PWM LED, blink rapidly to indicate connection attempt
    for (int i = 0; i < 10; i++)
    {
        digitalWrite(LED_STATUS_PIN, HIGH);
        delay(100);
        digitalWrite(LED_STATUS_PIN, LOW);
        delay(100);
    }
#endif

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

// Indicate successful WiFi connection
#ifdef USE_WS2812B_FOR_STATUS
    statusLED.setPixelColor(0, statusLED.Color(0, 255, 0)); // Green color for WS2812B on successful connection
    statusLED.show();
#else
    digitalWrite(LED_STATUS_PIN, HIGH); // Keep PWM LED on to indicate successful connection
    delay(2000);                        // Keep on for 2 seconds
    digitalWrite(LED_STATUS_PIN, LOW);
#endif

    Serial.println("\nConnected to WiFi");
    Serial.print("My IP: http://");
    Serial.println(WiFi.localIP());

    setupWebServer();
}

void loop()
{
    unsigned long currentMillis = millis();

    // Check if it's time to update the energy meter data
    if (currentMillis - previousMillis >= dataUpdateInterval)
    {
        previousMillis = currentMillis;

        // Update energy meter data
        updateEnergyMeterData();
    }

    // Update Strip
    displayMetricsOnStrip();

    if (newDataAvailable)
    {

        // Optional: Serial output for debugging
        Serial.print("Energy meter data updated: ");
        for (int i = 0; i < 3; i++)
        {
            Serial.print(meters[i].name);
            Serial.print(": ");
            Serial.print(meters[i].act_power);
            Serial.print("W, ");
        }
        Serial.print("Calculated Consumer: ");
        Serial.print(calculatedValue);
        Serial.println("W");
        newDataAvailable = false; // Reset the flag after updating the strip
    }
    // Handle client requests to the web server
    server.handleClient();

    // Short delay to prevent the loop from running too fast
    delay(loopDelay);
}
