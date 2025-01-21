# Shelly Pro EM3 Energy Monitor LED Meter

## Project Overview
The Shelly Pro EM3 Energy Monitor LED Meter helps homeowners track and manage energy usage effectively, especially with renewable energy sources. It connects to a Shelly Pro 3EM energy meter to display real-time data on Grid power, Solar power, and household consumption using an LED strip for visual feedback. This project uses an ESP32 board for processing and connectivity, with potential for ESP8266 support with modifications.

**Important:** The Shelly Pro 3EM must be configured to run in monophase mode for this project. In this setup, use one CT (current transformer) clamp on the solar inverter feed-in and one on the consumer feed. With minor changes to the code, a configuration for measuring both Grid and Solar could be implemented in the future.

## Purpose
This device assists homeowners in optimizing their renewable energy usage by providing an intuitive LED display and interactive web interface. It offers immediate visual feedback on energy consumption and detailed historical data visualization, enabling informed energy management decisions.

## LED Indications
- **Green LEDs:** Indicate energy usage from Solar sources.
- **Cyan/Blue LEDs:** Indicate excess Solar energy fed into the Grid.
- **Red LEDs:** Indicate energy consumption from the Grid.

### Status LED Behavior
- **During Setup:**  
  The status LED blinks on and off every 500ms while the system is establishing a WiFi connection and synchronizing time.
- **Post-Setup:**  
  Once setup is complete, the LED flickers briefly each time a valid WebSocket message is received, signaling successful data fetches from the energy meter.

## Key Features
- Monitors real-time Grid, Solar, and Consumer power usage using Shelly Pro 3EM configured in monophase mode.
- Visual feedback via an RGB LED strip that dynamically reflects energy consumption and source.
- Web interface for real-time data display and historical energy usage visualization.
- Status LED providing feedback during setup and data reception.
- Robust WebSocket communication with ping/pong handling for maintaining connection with the Shelly device.

## Hardware Requirements
- **Microcontroller:** Espressif board (ESP32 recommended; ESP8266 with modifications possible).
- **Energy Meter:** Shelly Pro 3EM energy meter configured in monophase mode.
  - Use one CT clamp on the solar inverter feed-in.
  - Use one CT clamp on the consumer feed.
  - *Note:* Future code changes may enable monitoring of both Grid and Solar simultaneously.
- **LED Strip:** RGB LED strip compatible with the Adafruit NeoPixel library (adjust configuration for GRBW if needed).
- **Status LED (Optional):** WS2812B LED or a standard PWM LED for status indication.

## Software Dependencies
- ArduinoJson (v6.17.0 or later)
- Adafruit NeoPixel (v1.10.0 or later)
- ArduinoWebsockets
- ESP32Ping
- SPIFFS (for file storage and configuration)

## Configuration and Setup
1. **Hardware Setup:**  
   - Connect the LED strip data line to the designated GPIO pin on the ESP32 (default is GPIO4).
   - Ensure proper power supply and ground connections for the LED strip and microcontroller.
   - Configure the Shelly Pro 3EM energy meter to run in monophase mode with one CT clamp on the solar inverter feed-in and one on the consumer feed.
   - Connect the Shelly Pro 3EM energy meter to the network and ensure it is accessible.

2. **Software Configuration:**  
   - Configure WiFi credentials, Shelly device IP, and other parameters via the web configuration page after initial setup.
   - Use PlatformIO in VSCode to build and upload the code to the ESP32 board.
   - The code automatically manages WiFi connection, time synchronization, WebSocket communication, and LED display updates.

3. **Running the Device:**  
   - On boot, the status LED will blink while connecting to WiFi and synchronizing time.
   - After setup, the LED strip displays real‑time energy metrics.
   - The status LED flickers briefly when new data is successfully fetched from the Shelly device.

## Web Interface
Access the device's web interface via its IP address on your network to:
- View real‑time energy usage and historical data charts.
- Configure WiFi settings and Shelly device details.
- Monitor system status and adjust settings as needed.

## Troubleshooting and Common Issues
- **LED Strip Not Displaying:**  
  Verify correct LED strip type (RGB vs. GRBW), wiring, power supply, and configuration in the code (ensure the correct pin and LED type flags are set).
- **No LED Activity:**  
  Check that the LED pin is defined correctly and that the LED strip is receiving power and data signals.
- **WiFi/Connection Issues:**  
  Confirm WiFi credentials, network connectivity, and proper configuration via the web page.
- **WebSocket Communication:**  
  Ensure the Shelly device is reachable at the specified IP and that the WebSocket endpoint is correct. Check serial logs for errors in connecting or maintaining the WebSocket.
- **LED Status Behavior:**  
  During setup, watch for blinking on the status LED. After setup, observe brief flickers on receiving WebSocket messages as an indication of data updates.

## Future Roadmap
- **Multiple Shelly EM Units:**  
  Future enhancements may support using multiple individual Shelly EM devices instead of a single Shelly Pro 3EM running in monophase mode. This would allow finer granularity of monitoring across different circuits.
- **Frequency Slider and Downsampling:**  
  Implement a slider for adjusting update frequency and corresponding downsampling of historical data.
- **OTA Firmware Updates:**  
  Over-The-Air updates for easier maintenance.
- **Enhanced Configuration Interface:**  
  Comprehensive web-based configuration for easier setup and customization.
- **Adaptive Brightness and Additional Features:**  
  Adjust LED brightness based on ambient light, daily email summaries, integration with home automation systems, enhanced security, and more.

## License and Contributions
This open-source project welcomes contributions to improve functionality and usability. Please refer to the project's license for usage and distribution terms.
