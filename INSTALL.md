# Installation Guide - Shelly EM Monitor LEDs v2.0

## Quick Start

### Prerequisites
- PlatformIO IDE (recommended) or Arduino IDE
- ESP32 development board (ESP32-C3 SuperMini recommended)
- Shelly Pro 3EM or dual Shelly EM devices
- WS2812B LED strip
- Basic electronics knowledge

### Hardware Setup

#### ESP32 Connections
```
ESP32 Pin    | Connection
-------------|------------------
GPIO 4       | LED Strip Data (configurable)
GPIO 7       | Status LED (WS2812B, optional)
GPIO 8       | Status LED (PWM, optional)
GND          | LED Strip Ground
5V/3.3V      | LED Strip Power (check strip requirements)
```

#### LED Strip Requirements
- **Type**: WS2812B/NeoPixel compatible
- **Count**: 1-300 LEDs (configurable)
- **Power**: Ensure adequate power supply for your LED count
- **Data**: Single data line connection to ESP32

### Software Installation

#### Method 1: PlatformIO (Recommended)

1. **Install PlatformIO**
   ```bash
   # Install PlatformIO Core
   pip install platformio
   
   # Or install PlatformIO IDE extension for VS Code
   ```

2. **Clone Repository**
   ```bash
   git clone https://github.com/Felixrising/ShellyEMMonitorLEDs.git
   cd ShellyEMMonitorLEDs
   ```

3. **Build and Upload**
   ```bash
   # Build the project
   pio run
   
   # Upload to ESP32
   pio run --target upload
   
   # Monitor serial output
   pio device monitor
   ```

#### Method 2: Arduino IDE

1. **Install Libraries**
   - ArduinoJson v7.4.2+
   - Adafruit NeoPixel v1.10.0+
   - ArduinoWebsockets
   - ESP32Ping
   - ArduinoOTA

2. **Board Configuration**
   - Board: ESP32C3 Dev Module (or your specific board)
   - Upload Speed: 921600
   - CPU Frequency: 160MHz
   - Flash Size: 4MB

3. **Compile and Upload**
   - Open `src/main.cpp` in Arduino IDE
   - Select your board and port
   - Upload the sketch

### First Boot Configuration

#### 1. Initial WiFi Setup

**Option A: SmartConfig (Recommended)**
1. Power on the device
2. Download ESP32 SmartConfig app on your phone
3. Follow app instructions to configure WiFi

**Option B: Access Point Mode**
1. Device creates AP: `SheMonitorAP`
2. Connect with password: `12345678`
3. Navigate to configuration page

#### 2. Web Configuration

1. **Find Device IP**
   - Check your router's DHCP client list
   - Or use mDNS: `http://shemonitor.local`

2. **Access Configuration**
   - Navigate to `http://[device-ip]/config`
   - Configure the following settings:

#### 3. Essential Settings

```
WiFi Settings:
├── SSID: [Your WiFi Network]
├── Password: [Your WiFi Password]

Device Settings:
├── Device Name: [Unique name for mDNS]
├── Shelly IP: [Leave blank for auto-discovery]

Meter Configuration:
├── Meter 0: [Grid/Solar/Consumer]
├── Meter 1: [Grid/Solar/Consumer]
├── Meter 2: [Grid/Solar/Consumer]

LED Settings:
├── LED Count: [Number of LEDs in strip]
├── LED Pin: [GPIO pin number]
├── LED Type: [Leave as default unless needed]
├── Invert Strip: [Check if colors are wrong]
```

### Shelly Device Configuration

#### For Shelly Pro 3EM

**Profile Selection:**
- **Triphase**: Use for true 3-phase installations
- **Monophase**: Use for standalone CT monitoring

**CT Clamp Placement:**
- **Grid**: Main feed from utility meter
- **Solar**: Solar inverter output
- **Consumer**: Main house consumption line

#### For Dual Shelly EM

**Device Setup:**
1. Configure first EM for Grid monitoring
2. Configure second EM for Solar monitoring
3. Consumer calculated as Grid - Solar

### Verification Steps

#### 1. Hardware Check
- [ ] LED strip lights up during boot
- [ ] Status LED shows connection status
- [ ] Serial monitor shows startup messages
- [ ] Device connects to WiFi

#### 2. Network Check
- [ ] Device appears in router DHCP list
- [ ] mDNS name resolves: `ping shemonitor.local`
- [ ] Web interface accessible
- [ ] Shelly device discovered automatically

#### 3. Functionality Check
- [ ] LED strip shows energy data
- [ ] Web interface displays real-time data
- [ ] Historical data chart populates
- [ ] Configuration changes take effect

### Troubleshooting

#### Common Issues

**Device Won't Connect to WiFi**
```
Solutions:
1. Check WiFi credentials in configuration
2. Verify network is 2.4GHz (not 5GHz)
3. Try SmartConfig setup
4. Check signal strength
```

**LED Strip Not Working**
```
Solutions:
1. Verify GPIO pin configuration
2. Check LED count setting
3. Ensure adequate power supply
4. Try inverting strip option
5. Check wiring connections
```

**Shelly Device Not Found**
```
Solutions:
1. Verify Shelly device is on same network
2. Check Shelly device IP address
3. Enable mDNS on Shelly device
4. Try manual IP configuration
```

**Web Interface Not Accessible**
```
Solutions:
1. Check device IP address
2. Try mDNS name: http://shemonitor.local
3. Verify firewall settings
4. Check network connectivity
```

#### Debug Mode

Enable detailed logging:
1. Connect to serial monitor (115200 baud)
2. Watch for timestamped debug messages
3. Look for error patterns
4. Check WebSocket connection status

### Advanced Configuration

#### Custom LED Patterns

Edit `displayMetricsOnStrip()` function for custom colors:
```cpp
uint32_t colorBoth     = strip->Color(153, 255, 0, 0);    // Yellow
uint32_t colorConsumer = strip->Color(255, 0, 0, 0);      // Red
uint32_t colorSolar    = strip->Color(0, 204, 255, 0);    // Cyan
uint32_t colorOff      = strip->Color(0, 0, 0, 0);        // Off
```

#### Power Range Adjustment

Modify power range in `displayMetricsOnStrip()`:
```cpp
int minRaw = 100;   // Minimum power (W)
int maxRaw = 5000;  // Maximum power (W)
```

#### Status LED Customization

Enable WS2812B status LED:
```cpp
#define USE_WS2812B_FOR_STATUS
```

### OTA Updates

#### Web Interface Method
1. Navigate to `http://[device-ip]/config`
2. Click "Check for OTA Updates"
3. Follow on-screen instructions

#### Manual OTA
1. Build new firmware
2. Use Arduino IDE or PlatformIO OTA upload
3. Device hostname: `shemonitor.local`
4. Password: `ShellyOTA123` (change in code)

### Security Considerations

#### Change Default Passwords
```cpp
// In setupOTA() function
ArduinoOTA.setPassword("YourSecurePassword");
```

#### Network Security
- Use WPA2/WPA3 WiFi encryption
- Consider IoT network segmentation
- Regular firmware updates
- Monitor device access logs

### Maintenance

#### Regular Tasks
- [ ] Check firmware updates monthly
- [ ] Monitor serial logs for errors
- [ ] Verify LED strip functionality
- [ ] Check Shelly device connectivity
- [ ] Review configuration settings

#### Performance Monitoring
- Watch memory usage in serial monitor
- Monitor WebSocket connection stability
- Check network latency and packet loss
- Verify data accuracy against Shelly app

### Support Resources

#### Documentation
- [README.md](README.md) - Complete project documentation
- [CHANGELOG.md](CHANGELOG.md) - Version history and changes
- [Shelly API Documentation](https://shelly-api-docs.shelly.cloud/)

#### Community Support
- GitHub Issues for bug reports
- GitHub Discussions for questions
- Shelly Community Forum
- ESP32 Arduino Community

#### Professional Support
For commercial deployments or custom modifications:
1. Create detailed GitHub issue
2. Include hardware specifications
3. Provide serial monitor logs
4. Describe specific requirements

---

## Quick Reference

### Default Settings
```
WiFi AP: SheMonitorAP / 12345678
Web Interface: http://[device-ip]/config
mDNS Name: shemonitor.local
LED Pin: GPIO 4
LED Count: 60
History Buffer: 144 entries (2.4 hours)
Update Interval: 1 second
```

### Key Endpoints
```
/          - Main dashboard
/config    - Configuration interface
/data      - JSON data API
/history   - Historical data
/ota       - OTA updates
/factoryReset - Factory reset
```

### Build Commands
```bash
# Build
pio run

# Upload
pio run --target upload

# Monitor
pio device monitor

# Clean
pio run --target clean
```

This installation guide should get you up and running with the new v2.0 firmware. The improvements in stability, memory management, and API compatibility should resolve the periodic crash issues you were experiencing. 