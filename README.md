# Shelly Pro EM3 Energy Monitor LED Meter

## Project Overview
The Shelly Pro EM3 Energy Monitor LED Meter is designed to help homeowners effectively use renewable energy. It connects to a Shelly Pro 3EM energy meter to display real-time data on Grid power, Solar power, and household consumption. This project uses an RGB LED strip for visual feedback on energy usage and is compatible with Espressif boards (ESP32 recommended, ESP8266 with modifications).

## Purpose
This device aims to assist homeowners in optimizing their use of renewable energy by providing an easy-to-understand display system. The LED strip offers clear indications to help make informed decisions about energy consumption.

## LED Indications
- Green LEDs: Indicate energy usage from Solar sources.
- Cyan/Blue LEDs: Indicate excess Solar energy being fed into the Grid.
- Red LEDs: Indicate energy consumption from the Grid.

## Key Features
- Monitors real-time Grid, Solar, and Consumer power.
- Uses Shelly Pro 3EM's EM1 JSON API for data retrieval.
- Provides an interactive web interface for historical data visualization.
- Features an intuitive LED strip display for immediate energy status feedback.

## Hardware Requirements
- Espressif board (ESP32 or ESP8266 with code modifications).
- Shelly Pro 3EM energy meter.
- RGB LED strip.

## Software Dependencies
- ArduinoJson (v6.19.1 or later)
- Adafruit NeoPixel (v1.10.1 or later)
- EEPROM

## Configuration and Setup
1. Input WiFi credentials and Shelly device IP in the `main.cpp` file for initial setup.
2. Use Platform.io in VSCode to upload the code to the Espressif board.
3. Connect the Shelly Pro 3EM energy meter and the LED strip following the project guidelines.

## Usage Instructions
Access the web interface through the Espressif board's IP to view energy usage over time. The LED Strip provides real-time visual feedback on energy sources and usage. Modify WiFi credentials and Shelly device IP via the web configuration page.

### Status LED Indication
This project optionally includes a status LED for additional feedback on the system's operation. It can be configured as a WS2812B LED or a regular PWM LED on GPIO8, controlled by the `USE_WS2812B_FOR_STATUS` define switch in the code.

#### Status LED Define Switch
To use a WS2812B LED for status indications, uncomment the line `#define USE_WS2812B_FOR_STATUS`. To use a regular PWM LED connected to GPIO8, keep this line commented out.

#### Status LED Lights and Meanings
| Color/Blink Pattern | Meaning |
|---------------------|---------|
| Solid Orange (WS2812B) / Rapid Blink (PWM) | Attempting to connect to WiFi |
| Solid Yellow (WS2812B) / Multiple Rapid Blinks (PWM) | Clearing EEPROM |
| Solid Green (WS2812B) / Solid On (PWM) | Successfully connected to WiFi |
| Blink Green (WS2812B) / Series of Blinks (PWM) | Data successfully fetched from the energy meter |

## Future Enhancements
- OTA (Over-The-Air) firmware updates for easy software maintenance.
- Comprehensive configuration web page to simplify device setup and customization.
- Improved EEPROM handling for more reliable data storage.
- More flexible configuration options for Status LED and Indicator LED strip, including customizable colors and patterns.
- Daily status email feature to provide a summary of energy usage and system status.
- Integration with home automation systems for more sophisticated energy management.
- Enhanced security features to protect device configuration and data.
- Adaptive brightness for the LED strip based on ambient light conditions.

## Beta Version Disclaimer
This beta version is subject to significant enhancements, especially in configuration handling and chart functionalities.

## Troubleshooting and Common Issues
- Verify correct hardware setup; incorrect wiring or configurations may cause malfunctions.
- Resolve WiFi or connectivity issues by updating the WiFi credentials and Shelly device IP on the configuration page.

## License and Contributions
This open-source project welcomes contributions to improve functionality and usability.
