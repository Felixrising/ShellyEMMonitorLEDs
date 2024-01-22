# Shelly Pro EM3 Energy Monitor LED Meter

## Project Overview
The **Shelly Pro EM3 Energy Monitor LED Meter** is a dynamic tool designed to aid homeowners in effectively using renewable energy. It interfaces with a Shelly Pro 3EM energy meter to track and display real-time data on Grid instantaneous power, Solar power, and household consumption. Compatible with Espressif boards (ESP32 recommended, ESP8266 with modifications), this project uses an RGB LED strip for immediate visual feedback on energy usage.

## Purpose
The device is crafted to guide homeowners in optimizing the use of renewable energy, offering an easy-to-understand display system. The LED strip provides clear indications for making informed decisions about energy consumption.

## LED Indications
- **Green LEDs**: Energy being used from Solar sources.
- **Cyan/Blue LEDs**: Excess Solar energy fed into the Grid.
- **Red LEDs**: Energy consumption from the Grid.

## Key Features
- Real-time monitoring of Grid, Solar, and Consumer power.
- Utilizes Shelly Pro 3EM's EM1 JSON API for data retrieval.
- Interactive web interface for historical energy data visualization.
- Intuitive LED strip display for immediate energy status.

## Hardware Requirements
- Espressif board (ESP32 or ESP8266 with code modifications).
- Shelly Pro 3EM energy meter.
- RGB LED strip.

## Software Dependencies
- ArduinoJson (v6.19.1 or later)
- Adafruit NeoPixel (v1.10.1 or later)
- EEPROM

## Configuration and Setup
1. **Initial Setup**: Input WiFi credentials and Shelly device IP in the `main.cpp` file.
2. **Code Upload**: Use Platform.io in VSCode to upload the code to the Espressif board.
3. **Hardware Connections**: Connect the Shelly Pro 3EM energy meter and the LED strip as per project guidelines.

## Usage Instructions
- **Web Interface**: Access the web interface via the Espressif board's IP to view energy usage over time.
- **LED Strip**: Provides real-time visual feedback on the source and usage of energy.
- **Configuration Page**: Modify WiFi credentials and Shelly device IP through the web configuration page.

## Future Enhancements
- Automatic discovery of Shelly energy meters using mDNS.
- Configurable meter selection with power sign inversion.
- Simplified WiFi setup feature.

## Beta Version Disclaimer
This is a beta version. Expect significant improvements in future updates, particularly in configuration handling and chart functionalities.

## Troubleshooting and Common Issues
- Ensure correct hardware setup. Incorrect wiring or configurations can lead to malfunctions.
- Address WiFi or connectivity issues by updating the WiFi credentials and Shelly device IP on the configuration page.

## License and Contributions
This project is open-source. We welcome contributions to enhance its functionality and usability.
