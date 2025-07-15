# Shelly Pro EM3 Energy Monitor LED Meter

## Project Overview

The Shelly Pro EM3 Energy Monitor LED Meter is designed to help homeowners effectively use renewable energy. It connects to a Shelly Pro 3EM energy meter to display real-time data on Grid power, Solar power, and household consumption. This project uses an RGB LED strip for visual feedback on energy usage and is compatible with ESP32 boards.

## Purpose

This device aims to assist homeowners in optimizing their use of renewable energy by providing an easy-to-understand display system. The LED strip offers clear indications to help make informed decisions about energy consumption.

## LED Indications

* **Green LEDs**: Indicate energy usage from Solar sources
* **Cyan/Blue LEDs**: Indicate excess Solar energy being fed into the Grid
* **Red LEDs**: Indicate energy consumption from the Grid
* **Yellow LEDs**: Indicate both Solar and Consumer usage simultaneously

## Key Features

### Core Functionality
* Monitors real-time Grid, Solar, and Consumer power
* Uses Shelly Pro 3EM's JSON API for data retrieval
* Provides an interactive web interface for historical data visualization
* Features an intuitive LED strip display for immediate energy status feedback

### New in Version 2.0
* **ArduinoJson v7 Support**: Modern dynamic JSON allocation for better memory management
* **Dual Device Support**: Compatible with both Shelly Pro 3EM and dual single-phase Shelly EM setups
* **Enhanced API Compatibility**: Supports both triphase and monophase profiles
* **OTA Updates**: Over-the-air firmware updates for easy maintenance
* **Improved Stability**: Comprehensive error handling and watchdog management
* **Network Health Monitoring**: Automatic device discovery and connection recovery
* **Memory Optimization**: Reduced memory footprint with streaming JSON responses
* **Configuration Validation**: Input validation and error recovery

## Hardware Requirements

* ESP32 board (ESP32-C3 SuperMini recommended)
* Shelly Pro 3EM energy meter OR two single-phase Shelly EM devices
* RGB LED strip (WS2812B/NeoPixel compatible)
* Stable power supply for ESP32

## Software Dependencies

* **ArduinoJson** (v7.4.2 or later) - Modern JSON handling with dynamic allocation
* **Adafruit NeoPixel** (v1.10.0 or later) - LED strip control
* **ArduinoWebsockets** - WebSocket communication with Shelly devices
* **ESP32Ping** - Network health monitoring
* **ArduinoOTA** - Over-the-air updates
* **EEPROM/SPIFFS** - Configuration storage

## Configuration and Setup

### Initial Setup
1. Flash the firmware to your ESP32 board using PlatformIO
2. Connect the LED strip to the configured GPIO pin (default: GPIO 4)
3. Power on the device - it will create a WiFi access point
4. Connect to the AP and configure WiFi credentials via SmartConfig or web interface
5. Access the web interface to configure Shelly device settings

### Web Configuration
Access the device's web interface at `http://[device-ip]/config` to configure:

* **WiFi Settings**: SSID and password
* **Shelly Device**: IP address or hostname
* **Device Name**: mDNS hostname for the device
* **Meter Assignments**: Configure which physical meter corresponds to Grid/Solar/Consumer
* **LED Strip Settings**: 
  * Number of LEDs (1-300)
  * GPIO pin (0-39)
  * LED type flags
  * Strip inversion option

### Automatic Discovery
The device automatically discovers Shelly devices on the network using mDNS. Supported devices:
* Shelly Pro 3EM (single device, three phases)
* Shelly EM (dual device setup for two single-phase meters)

## API Compatibility

### Shelly Pro 3EM Profiles
The device supports both operational profiles:

#### Triphase Profile
- Uses single `EM` component (`em:0`)
- Accesses `a_act_power`, `b_act_power`, `c_act_power`
- Suitable for true three-phase installations

#### Monophase Profile  
- Uses three `EM1` components (`em1:0`, `em1:1`, `em1:2`)
- Each component reports individual `act_power`
- Suitable for standalone CT monitoring

### Dual Shelly EM Support
For users with two single-phase Shelly EM devices:
- Automatic discovery of multiple devices
- Channel offset management
- Unified data presentation

## Status LED Indication

The device includes a status LED for system feedback:

### Status LED Types
Configure using `USE_WS2812B_FOR_STATUS` define:
- **WS2812B LED** (GPIO 7): Full color status indication
- **PWM LED** (GPIO 8): Blink pattern status indication

### Status Meanings

| Color/Pattern | Meaning |
|---------------|---------|
| Solid Orange (WS2812B) / Rapid Blink (PWM) | Connecting to WiFi |
| Solid Yellow (WS2812B) / Multiple Blinks (PWM) | Clearing EEPROM |
| Solid Green (WS2812B) / Solid On (PWM) | WiFi Connected |
| Blink Green (WS2812B) / Series of Blinks (PWM) | Data received from energy meter |

## Advanced Features

### Over-the-Air Updates
- Access via web interface: `/ota` endpoint
- Secure password-protected updates
- Progress monitoring via serial console
- Automatic rollback on failure

### Network Health Monitoring
- Automatic ping testing of Shelly devices
- Connection recovery and retry logic
- WebSocket timeout detection
- Automatic device rediscovery

### Memory Management
- ArduinoJson v7 dynamic allocation
- Streaming JSON responses for large datasets
- Optimized history buffer size
- Garbage collection and cleanup

### Error Handling
- Comprehensive input validation
- Graceful degradation on failures
- Watchdog timer management
- Request timeout handling

## Troubleshooting

### Common Issues

1. **Device Not Connecting to WiFi**
   - Check credentials in web interface
   - Try SmartConfig setup
   - Verify network compatibility

2. **LED Strip Not Working**
   - Verify GPIO pin configuration
   - Check LED count and type settings
   - Ensure adequate power supply

3. **Shelly Device Not Found**
   - Check IP address configuration
   - Verify network connectivity
   - Try automatic discovery

4. **Periodic Crashes (Fixed in v2.0)**
   - Update to latest firmware
   - Check memory usage
   - Monitor serial output for errors

### Debug Information
Enable detailed logging by monitoring the serial console at 115200 baud. All messages include timestamps for debugging.

## Security Considerations

* Change default OTA password in code
* Use secure WiFi networks
* Consider network segmentation for IoT devices
* Regular firmware updates

## Future Enhancements

* **Mobile App**: Dedicated smartphone application
* **Cloud Integration**: Remote monitoring capabilities
* **Advanced Analytics**: Machine learning for usage patterns
* **Multi-device Support**: Manage multiple installations
* **Energy Forecasting**: Predictive analytics for energy usage

## Performance Optimizations

### Memory Usage
- Reduced history buffer: 144 entries (2.4 hours)
- Dynamic JSON allocation
- Streaming web responses
- Efficient data structures

### Network Efficiency
- Connection pooling
- Retry logic with backoff
- Health monitoring
- Automatic recovery

### Processing Efficiency
- Watchdog timer management
- Non-blocking operations
- Efficient LED updates
- Optimized data structures

## Development

### Building
```bash
# Using PlatformIO
pio run

# Upload firmware
pio run --target upload

# Monitor serial output
pio device monitor
```

### Contributing
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License and Contributions

This open-source project welcomes contributions to improve functionality and usability. Please follow the existing code style and include appropriate tests.

## Version History

### Version 2.0 (Current)
- ArduinoJson v7 compatibility
- Dual device support
- OTA updates
- Enhanced stability
- Memory optimization
- Comprehensive error handling

### Version 1.0 (Legacy)
- Basic Shelly 3EM support
- LED strip visualization
- Web interface
- Configuration management

## Support

For issues, questions, or contributions:
1. Check the troubleshooting section
2. Review existing GitHub issues
3. Create a new issue with detailed information
4. Include serial console output for debugging

---

**Note**: This is production-ready firmware with comprehensive error handling and stability improvements. The periodic crash issues from earlier versions have been resolved through proper memory management, timeout handling, and network resilience features.
