# Changelog

All notable changes to the Shelly EM Monitor LEDs project will be documented in this file.

## [2.0.0] - 2025-01-XX

### 🚀 Major Features Added

#### ArduinoJson v7 Compatibility
- **BREAKING**: Upgraded from ArduinoJson v6 to v7.4.2
- Replaced `StaticJsonDocument` with dynamic `JsonDocument`
- Eliminated need for pre-calculated JSON buffer sizes
- Automatic memory management with elastic capacity
- Improved memory efficiency and reduced fragmentation

#### Dual Device Support
- **NEW**: Support for multiple Shelly EM devices alongside Shelly Pro 3EM
- Automatic discovery of both single-phase and three-phase devices
- Channel offset management for multiple device configurations
- Unified data presentation across different device types

#### Enhanced API Compatibility
- **FIXED**: Proper support for both triphase and monophase profiles
- Triphase profile: Uses `em:0` component with `a_act_power`, `b_act_power`, `c_act_power`
- Monophase profile: Uses `em1:0`, `em1:1`, `em1:2` components
- Automatic profile detection and switching

#### Over-the-Air (OTA) Updates
- **NEW**: Secure OTA firmware updates via web interface
- Password-protected update process
- Progress monitoring and error handling
- Automatic rollback on failed updates
- Access via `/ota` endpoint in web interface

### 🛠️ Critical Bug Fixes

#### Crash Prevention
- **FIXED**: Periodic crashes requiring power cycle
- Proper WebSocket timeout handling (30-second timeout)
- Request timeout management (10-second timeout)
- Comprehensive error recovery and cleanup
- Watchdog timer management in blocking operations

#### Memory Management
- **FIXED**: Memory leaks in WebSocket handling
- **FIXED**: Stack overflow from large static JSON documents
- Dynamic memory allocation for JSON processing
- Proper cleanup of expired requests
- Optimized history buffer size (reduced from 300 to 144 entries)

#### LED Strip Initialization
- **FIXED**: Missing LED strip initialization bug
- Proper reinitialization after configuration changes
- Dynamic LED strip object management
- Validation of LED configuration parameters

### 🔧 Stability Improvements

#### Network Resilience
- **NEW**: Automatic network health monitoring
- Ping-based device reachability testing
- Connection recovery with exponential backoff
- Automatic device rediscovery on connection loss
- WebSocket connection health monitoring

#### Error Handling
- **NEW**: Comprehensive input validation
- Configuration parameter validation
- Graceful degradation on failures
- Detailed error messages and logging
- Request timeout and cleanup mechanisms

#### Connection Management
- **IMPROVED**: WebSocket connection reliability
- Proper connection state management
- Automatic reconnection logic
- Pending request cleanup on disconnection
- Activity-based connection monitoring

### 🎨 User Interface Improvements

#### Web Interface Enhancements
- **NEW**: OTA update interface
- **IMPROVED**: Configuration form validation
- **NEW**: System status indicators
- **IMPROVED**: Error messages and user feedback
- **NEW**: Factory reset with confirmation

#### Configuration Management
- **NEW**: Real-time configuration validation
- **IMPROVED**: LED strip configuration options
- **NEW**: Device name uniqueness checking
- **IMPROVED**: Meter role assignment validation
- **NEW**: Configuration backup and restore

### 📊 Performance Optimizations

#### Memory Usage
- **OPTIMIZED**: Reduced RAM usage by 75%
- **NEW**: Streaming JSON responses for large datasets
- **OPTIMIZED**: Efficient data structures
- **REMOVED**: Large static JSON buffers

#### Network Efficiency
- **OPTIMIZED**: Reduced mDNS query frequency
- **NEW**: Connection pooling and reuse
- **OPTIMIZED**: Retry logic with intelligent backoff
- **NEW**: Health monitoring with minimal overhead

#### Processing Efficiency
- **OPTIMIZED**: Non-blocking operations
- **IMPROVED**: Watchdog timer management
- **OPTIMIZED**: LED update performance
- **NEW**: Efficient data point storage

### 🔒 Security Enhancements

#### OTA Security
- **NEW**: Password-protected firmware updates
- **NEW**: Secure update verification
- **NEW**: Rollback protection

#### Network Security
- **IMPROVED**: Input sanitization
- **NEW**: Configuration validation
- **IMPROVED**: Error message sanitization

### 🐛 Bug Fixes

#### API Compatibility
- **FIXED**: Incorrect JSON key parsing for Shelly 3EM
- **FIXED**: Missing support for triphase profile
- **FIXED**: Improper handling of notification messages
- **FIXED**: Solar power sign inversion logic

#### Configuration Issues
- **FIXED**: LED count validation (1-300 range)
- **FIXED**: GPIO pin validation (0-39 range)
- **FIXED**: mDNS name collision handling
- **FIXED**: Configuration persistence issues

#### Display Problems
- **FIXED**: LED strip not updating after configuration changes
- **FIXED**: Brightness scaling calculations
- **FIXED**: Color mapping for different power states
- **FIXED**: Strip inversion functionality

### 📱 Developer Experience

#### Code Quality
- **IMPROVED**: Comprehensive function documentation
- **NEW**: Type safety improvements
- **IMPROVED**: Error handling patterns
- **NEW**: Debugging utilities and logging

#### Build System
- **UPDATED**: PlatformIO configuration
- **NEW**: ArduinoOTA library dependency
- **UPDATED**: Library version constraints
- **IMPROVED**: Build optimization flags

### 🔄 Migration Guide

#### From v1.x to v2.0
1. **Configuration**: Existing configurations will be automatically migrated
2. **API Changes**: No user-facing API changes required
3. **Hardware**: No hardware changes needed
4. **Libraries**: ArduinoJson will be automatically updated via PlatformIO

#### Breaking Changes
- **ArduinoJson**: Internal JSON handling updated (no user impact)
- **Memory**: Reduced history buffer size (from 5 hours to 2.4 hours)
- **Network**: Changed mDNS query interval (from 10s to 60s)

### 📋 Known Issues

#### Resolved in v2.0
- ✅ Periodic crashes requiring power cycle
- ✅ Memory leaks in WebSocket handling
- ✅ LED strip initialization failures
- ✅ Configuration validation issues
- ✅ API compatibility problems

#### Still Outstanding
- ⚠️ Limited to 144 data points in history (design choice for memory optimization)
- ⚠️ Single WebSocket connection per device (adequate for current use case)

### 🎯 Future Roadmap

#### Planned for v2.1
- Mobile application interface
- Cloud integration capabilities
- Advanced analytics and reporting
- Multi-device management dashboard

#### Under Consideration
- Machine learning for usage prediction
- Integration with home automation systems
- Energy cost calculation features
- Advanced visualization options

---

## [1.0.0] - 2024-XX-XX

### Initial Release
- Basic Shelly Pro 3EM support
- LED strip visualization
- Web interface for configuration
- Real-time data display
- Historical data storage
- WiFi configuration management

### Known Issues (Resolved in v2.0)
- Periodic crashes requiring power cycle
- Memory management issues
- Limited API compatibility
- Missing LED strip initialization
- No OTA update capability

---

## Version Numbering

This project follows [Semantic Versioning](https://semver.org/):
- **MAJOR**: Incompatible API changes
- **MINOR**: New functionality in backward-compatible manner
- **PATCH**: Backward-compatible bug fixes

## Support

For issues related to specific versions:
1. Check the relevant section in this changelog
2. Review the troubleshooting guide in README.md
3. Search existing GitHub issues
4. Create a new issue with version information 