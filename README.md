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

### New in Version 2.0-RC2
* **NVS Configuration Storage**: Settings now survive filesystem (uploadfs) operations
* **Physical Button Factory Reset**: Hold BOOT button for 5 seconds to reset device
* **Configuration Backup/Restore**: Export and import settings via web interface
* **Custom Partition Table**: Proper NVS allocation for reliable storage
* **Automatic Config Migration**: Seamlessly migrates from LittleFS to NVS on first boot
* **Interactive Chart.js Dashboard**: Real-time and historical energy visualization with zoom/pan capabilities
* **WiFi Out-of-Box Experience**: Automatic AP mode with captive portal for easy first-time setup
* **Enhanced WiFi Management**: AP+STA mode for network scanning while hosting configuration portal
* **Automatic Reconnection**: 5-minute retry intervals in AP mode for automatic recovery
* **LED Strip Direction Control**: Invert strip direction option for flexible installation
* **ArduinoJson v7 Support**: Modern dynamic JSON allocation for better memory management
* **Dual Device Support**: Compatible with both Shelly Pro 3EM and dual single-phase Shelly EM setups
* **Enhanced API Compatibility**: Supports both triphase and monophase profiles
* **OTA Updates**: Over-the-air firmware updates for easy maintenance
* **Improved Stability**: Comprehensive error handling and watchdog management
* **Chart Timestamp Validation**: Automatic cleanup of corrupted chart data
* **Memory Optimization**: Reduced memory footprint with streaming JSON responses

### ⚠️ Important: Upgrading to v2.0-RC2

**This version requires a one-time serial flash** due to partition table changes. After this initial flash, all future updates can be done via OTA.

**Upgrade Steps:**
1. **Backup your configuration** using the web interface (Settings → Download Config Backup)
2. Connect device via USB/Serial
3. Flash firmware: `pio run --target upload --environment esp32c3_supermini`
4. Flash filesystem: `pio run --target uploadfs --environment esp32c3_supermini`
5. Device will automatically migrate existing settings to NVS on first boot
6. If needed, restore config backup via web interface

**Benefits After Upgrade:**
- Settings persist across all future `uploadfs` operations
- Configuration stored in NVS (separate from filesystem)
- Web interface updates no longer require reconfiguration
- Physical button factory reset available (hold BOOT button 5 seconds)

## Hardware Requirements

* ESP32 board (ESP32-C3 SuperMini recommended)
* Shelly Pro 3EM energy meter OR two single-phase Shelly EM devices
* RGB LED strip (WS2812B/NeoPixel compatible)
* Stable power supply for ESP32

## Software Dependencies

### Firmware Libraries
* **ArduinoJson** (v7.4.2 or later) - Modern JSON handling with dynamic allocation
* **Adafruit NeoPixel** (v1.10.0 or later) - LED strip control
* **ArduinoWebsockets** - WebSocket communication with Shelly devices
* **ESP32Ping** - Network health monitoring
* **ArduinoOTA** - Over-the-air updates
* **ezTime** - NTP time synchronization
* **Preferences** - NVS storage for persistent configuration
* **LittleFS** - Filesystem for web assets

### Web Interface Libraries (Included)
* **Chart.js** (v4.4.1 UMD) - Interactive data visualization
* **Moment.js** - Time formatting and manipulation
* **chartjs-adapter-moment** - Time scale adapter for Chart.js
* **chartjs-plugin-zoom** - Zoom and pan functionality

## Configuration and Setup

### Initial Setup
1. Flash the firmware to your ESP32 board using PlatformIO
2. Connect the LED strip to the configured GPIO pin (default: GPIO 4)
3. Power on the device - it will automatically create a WiFi access point named "SheMonitorAP"
4. Connect to the AP (no password required) - your device should automatically open the captive portal
5. Scan and select your WiFi network from the list
6. Enter your WiFi password and save
7. Device will restart and connect to your network
8. Access the web interface at `http://shemonitor.local` or via the device's IP address
9. Configure Shelly device settings via the configuration page

### Web Interface

#### Interactive Dashboard
Access the main dashboard at `http://shemonitor.local` or `http://[device-ip]` to view:
* **Real-time Energy Display**: Live Grid, Solar, and Consumption metrics
* **Interactive Chart**: Historical energy data with zoom and pan capabilities
* **Time-series Visualization**: Powered by Chart.js with moment.js time adapter
* **Responsive Design**: Works on desktop and mobile devices

#### Configuration Page
Access the configuration page at `http://[device-ip]/config` to configure:

* **WiFi Settings**: SSID and password with network scanning
* **Shelly Device**: IP address or hostname with automatic discovery
* **Device Name**: mDNS hostname for the device (default: shemonitor)
* **Meter Assignments**: Configure which physical meter corresponds to Grid/Solar/Consumer
* **LED Strip Settings**: 
  * Number of LEDs (1-300)
  * GPIO pin (0-39)
  * LED type (WS2812B RGB/RGBW, SK6812, etc.)
  * **Invert Strip Direction**: Reverse LED drawing order for flexible installation
* **Configuration Backup**:
  * **Download Config Backup**: Export current settings as JSON file
  * **Restore Config from Backup**: Upload previously saved configuration
  * **⚠️ Important**: Settings stored in NVS survive `uploadfs` operations automatically
* **System Actions**:
  * **Test LED Strip**: Verify LED strip is working correctly
  * **Factory Reset**: Clear all settings (also available via BOOT button - hold 5 seconds)

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
   - Connect to "SheMonitorAP" access point
   - Verify network compatibility (2.4GHz only)
   - Device will retry every 5 minutes in AP mode

2. **WiFi Network Scan Shows No Networks**
   - Ensure you're connected to the SheMonitorAP
   - Try the "Retry Scan" button
   - Device uses AP+STA mode for scanning
   - Check that 2.4GHz WiFi is enabled on your router

3. **Chart Not Displaying on Dashboard**
   - Clear browser cache (Ctrl+F5)
   - Check browser console (F12) for JavaScript errors
   - Verify Chart.js libraries loaded at `/listfiles`
   - Ensure using IP address instead of .local if issues persist

4. **LED Strip Not Working**
   - Verify GPIO pin configuration
   - Check LED count and type settings
   - Ensure adequate power supply (5V, sufficient amperage)
   - Test with `/testLEDs` endpoint

5. **LED Strip Direction is Reversed**
   - Enable "Invert Strip Direction" in configuration
   - Changes take effect immediately
   - No reboot required

6. **Shelly Device Not Found**
   - Check IP address configuration
   - Verify network connectivity
   - Try automatic discovery via mDNS
   - Ensure Shelly device is on the same network

7. **Periodic Crashes (Fixed in v2.0)**
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

### Version 2.0-RC2 (Current - Release Candidate)
**Major Changes:**
- **NVS Configuration Storage**: Settings now persist across `uploadfs` operations
- **Custom Partition Table**: Dedicated NVS partition for reliable storage
- **Automatic Migration**: Seamlessly migrates from LittleFS to NVS on first boot

**New Features:**
- Physical button factory reset (hold BOOT button 5 seconds)
- Web-based configuration backup/restore
- Chart timestamp validation and cleanup
- Improved error handling for config operations

**Bug Fixes:**
- Fixed settings being wiped on `uploadfs` operations
- Fixed LED strip inversion not persisting
- Fixed ShemeterName not being restored from storage
- Fixed Chart.js library loading errors
- Fixed corrupted chart timestamps causing errors

**Breaking Changes:**
- Requires one-time serial flash due to partition table changes
- After initial flash, all future OTA updates work normally

### Version 2.0-RC1
**New Features:**
- Interactive Chart.js dashboard with zoom/pan
- WiFi out-of-box experience with captive portal
- AP+STA mode for network scanning in AP mode
- Automatic WiFi reconnection (5-minute intervals)
- LED strip direction inversion support
- Debug endpoints (`/listfiles`, `/testLEDs`)

**Improvements:**
- Chart.js UMD versions for browser compatibility
- Enhanced WiFi scan with error handling
- Captive portal detection URL handlers
- Better JavaScript error logging

**Bug Fixes:**
- Fixed WiFi network scanning in AP mode
- Fixed Chart.js loading (ES module → UMD)
- Fixed LED strip inversion not working
- Improved error messages and user feedback

### Version 2.0 (Development)
- ArduinoJson v7 compatibility
- Dual device support (Pro 3EM + Shelly EM)
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

**Note**: Version 2.0-RC2 is a release candidate featuring **NVS-based configuration storage** that survives filesystem updates, physical button factory reset, and comprehensive Chart.js integration. This build includes uploadfs-persistent settings, automatic config migration, and enhanced stability improvements. Settings are now stored in NVS (Non-Volatile Storage) separate from the filesystem, ensuring your configuration persists across web interface updates.

**⚠️ Important**: This version requires a **one-time serial flash** due to partition table changes. After this initial flash, all future updates can be done via OTA and your settings will automatically persist.

**Getting Started**: Flash the firmware via serial, optionally backup your config, connect to "SheMonitorAP", configure your WiFi, and access the interactive dashboard at `http://shemonitor.local`

**Feedback Welcome**: Please report any issues or suggestions on the GitHub repository.
