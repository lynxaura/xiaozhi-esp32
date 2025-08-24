# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

XiaoZhi ESP32 is an AI voice chatbot project supporting 80+ ESP32-based development boards with offline wake word detection, streaming ASR+LLM+TTS, MCP (Model Context Protocol) integration, and multi-language support (Chinese, English, Japanese). It's an open-source MIT-licensed project for AI hardware development education.

## Build System and Development Commands

### ESP-IDF Based Build System
This project uses ESP-IDF 5.4+ with CMake build system:

```bash
# Build for specific board (replace with actual board name)
idf.py build

# Flash to device 
idf.py -p /dev/ttyUSB0 flash monitor

# Clean build
idf.py clean

# Configure project (menuconfig)
idf.py menuconfig
```

### Release Build System
The project includes a custom release system for multiple board configurations:

```bash
# Build firmware for specific board type
python scripts/release.py [board-directory-name]

# Example board names: lichuang-dev, esp-box-3, m5stack-core-s3
python scripts/release.py lichuang-dev
```

### Board Configuration
Each supported board has its own directory under `main/boards/[board-name]/` containing:
- `config.h` - Hardware pin mapping and configuration
- `config.json` - Build configuration with target chip and sdkconfig options
- `[board-name].cc` - Board-specific initialization code
- `README.md` - Board documentation

## Code Architecture

### Main Application Structure
- **Entry Point**: `main/main.cc` - Minimal ESP32 entry point
- **Core Application**: `main/application.cc/.h` - Singleton orchestrating entire system
- **Device States**: Idle, listening, speaking, connecting, upgrading with event-driven transitions
- **Event System**: FreeRTOS event groups for coordinated task synchronization

### Audio Processing Pipeline
Multi-stage bidirectional audio pipeline:
```
Input: MIC → [Audio Processor] → [Opus Encoder] → Server
Output: Server → [Opus Decoder] → Speaker
```

Key components:
- `audio/audio_service.cc` - Manages three separate tasks with FreeRTOS queues
- `audio/processors/` - Configurable audio processing (AFE or no-op)
- `audio/wake_words/` - Multiple wake word implementations (AFE, ESP, Custom)
- `audio/codecs/` - Various audio codec support (ES8311, ES8374, etc.)

### Display System
- **Base Class**: `display/display.cc` - Abstract interface with LVGL integration
- **Implementations**: LCD, OLED, ESP logging displays
- **Thread Safety**: DisplayLockGuard for LVGL access
- **UI Elements**: Status bar, chat messages, emotions, notifications

### Communication Protocols
- **Protocol Abstraction**: `protocols/protocol.cc` - Common interface
- **MQTT Protocol**: `protocols/mqtt_protocol.cc`
- **WebSocket Protocol**: `protocols/websocket_protocol.cc`
- **Binary Protocols**: Version 2/3 with timestamp support for AEC

### Board Abstraction Layer
- **Base Classes**: `boards/common/board.cc`, `wifi_board.cc`, `ml307_board.cc`
- **Hardware Abstraction**: Audio codec, display, LED, camera, backlight interfaces
- **Network Management**: WiFi/4G network interface abstraction
- **80+ Board Support**: Each board directory contains complete hardware configuration

### MCP Server Implementation
- **MCP Integration**: `mcp_server.cc` - Full JSON-RPC 2.0 MCP compliance
- **Tool System**: Dynamic tool registration with type-safe parameters
- **Common Tools**: Device status, volume, brightness, theme, camera control
- **Thread Safety**: Separate execution threads with configurable stack sizes

## Development Workflows

### Adding New Board Support
1. Create new directory: `main/boards/my-custom-board/`
2. Add configuration files: `config.h`, `config.json`, board implementation file
3. Update `main/CMakeLists.txt` with new board type mapping
4. Test build: `python scripts/release.py my-custom-board`

### Audio Codec Integration
- Inherit from `AudioCodec` base class in `audio/audio_codec.h`
- Implement codec-specific initialization and I2S configuration
- Add codec files to `audio/codecs/` directory
- Register in board implementation

### Display Driver Addition
- Inherit from `Display` base class in `display/display.h` 
- Implement LVGL buffer management and drawing operations
- Add driver to `display/` directory
- Configure in board's display initialization

### MCP Tool Development
- Inherit from `McpTool` base class with JSON-RPC parameter system
- Implement tool-specific functionality with thread-safe operations
- Register tool in board's MCP initialization
- Support type-safe parameters (boolean, integer, string arrays)

## Common File Patterns

### Board Implementation Pattern
```cpp
class MyBoard : public WifiBoard {
    // Hardware initialization in constructor
    // Override virtual methods: GetAudioCodec(), GetDisplay(), GetBacklight()
    // Register with DECLARE_BOARD(MyBoard) macro
};
```

### Audio Codec Pattern
```cpp
class MyCodec : public AudioCodec {
    // I2S and I2C configuration
    // Implement Initialize(), SetVolume(), SetMute() methods
    // Handle power management integration
};
```

### MCP Tool Pattern
```cpp
class MyTool : public McpTool {
    // Define parameter schema in constructor
    // Implement Call() method with JSON return values
    // Handle error cases with proper JSON-RPC error responses
};
```

## Testing and Quality Assurance

### Build Validation
- Test builds across multiple board configurations
- Verify partition table configurations for different flash sizes
- Check sdkconfig compatibility with target chips

### Audio Pipeline Testing
- Use audio debugging server: `python scripts/audio_debug_server.py`
- Test wake word detection across different noise conditions
- Validate AEC modes (off, device-side, server-side)

### MCP Tool Testing
- Verify JSON-RPC compliance with parameter validation
- Test tool execution in separate threads
- Validate error handling and response formatting

## Key Dependencies and Components

### External Components (managed_components/)
- LVGL for display graphics
- ESP-SR for wake word detection
- ESP-LCD drivers for various display controllers
- ESP-CODEC for audio processing
- Opus encoder/decoder for audio compression

### Language Assets
- Multi-language sound files in `main/assets/[lang]/`
- Generated language configuration headers
- Embedded P3 audio format support

### Build Dependencies
- ESP-IDF 5.4+
- Python 3.7+ for build scripts
- Git for component management

## ALichuangTest Robot Board

### Overview
ALichuangTest is a custom interactive robot board based on xiaozhi-esp32, extending the voice interaction capabilities with additional sensors and actuators for physical embodiment. This board creates an emotionally responsive AI companion with tactile feedback and motion sensing capabilities.

### Hardware Components

#### Core Components (inherited from xiaozhi-esp32)
- **ESP32-S3** microcontroller with 16MB flash
- **Audio System**: ES8311 DAC + ES7210 ADC for high-quality voice I/O
- **Display**: 320x240 ST7789 LCD with touch support (FT5x06)
- **Camera**: OV2640 for visual capabilities
- **Network**: WiFi connectivity for cloud AI services

#### Extended Sensor Systems
- **QMI8658 IMU (6-axis)**: 
  - 3-axis accelerometer and 3-axis gyroscope at I2C address 0x6A
  - Detects motion events: free fall, shake, flip, pickup, upside-down orientation
  - Provides real-time angle calculations and motion tracking
  
- **PCA9685 PWM Controller (16-channel)**:
  - I2C address 0x40, controls vibration motors and servos
  - 12-bit resolution (0-4095) for precise control
  - Configurable PWM frequency for different actuator types

- **PCA9557 I/O Expander**: 
  - I2C address 0x19, manages auxiliary GPIO functions
  - Controls audio amplifier enable and display backlight

#### Actuator Systems
- **Vibration Motor**: Connected to PCA9685 channel 0
  - Provides haptic feedback with 10+ predefined patterns
  - Emotion-driven vibration responses
  - Patterns include: purr, heartbeat, giggle, tremble, struggle
  
- **Servo Motors** (future): PCA9685 channels available for body rotation control

### Software Architecture

#### Event-Driven Interaction System
The robot uses a sophisticated event engine (`EventEngine`) that coordinates multiple input sources:

1. **Motion Events** (via `MotionEngine`):
   - Free fall detection with immediate haptic warning
   - Violent shake recognition with dizzy response
   - Device flip detection with playful feedback
   - Pickup detection with context-aware responses
   - Upside-down orientation with struggle patterns

2. **Touch Events** (via `TouchEngine`):
   - Multi-zone capacitive touch (left/right/both sides)
   - Gesture recognition: tap, double-tap, long press
   - Complex patterns: cradled (held gently), tickled (rapid touches)
   - Touch position tracking with duration measurement

3. **Event Processing Pipeline**:
   ```
   Sensors → Event Detection → Event Processing → Response Generation
                                      ↓
                              Event Upload (MQTT/WebSocket)
   ```

#### Emotion System
- **Emotion States**: neutral, happy, sad, angry, surprised, laughing, thinking
- **Emotion-Driven Responses**:
  - Dynamic display animations based on current emotion
  - Synchronized vibration patterns matching emotional state
  - Animation speed varies with emotion intensity
  - Automatic return to neutral after 10 seconds of inactivity

#### Display Animation System
- **Anima UI Mode**: Custom emotion-based animations
  - Preloaded emotion sprite sheets for each state
  - Frame-by-frame animation during speech
  - Static display when idle
  - Smooth transitions between emotional states

- **Animation Control**:
  - Emotion-specific playback speeds (40-150ms per frame)
  - Synchronized with audio output (animations play during speech)
  - Hardware-accelerated rendering via LVGL

### Configuration System

#### Event Configuration (`event_config.json`)
Declarative configuration for all interaction behaviors:
- Motion detection thresholds and timeouts
- Touch gesture parameters and recognition patterns  
- Event processing strategies (debouncing, merging, filtering)
- Response mappings for each event type

#### Board Configuration
- Target: ESP32-S3
- Partition: 16MB with OTA support
- Build variants: xiaozhi-default-UI, lingxi-anima-UI
- I2C bus: 400kHz for sensors, 100kHz for audio codec

### Development Guidelines

#### Adding New Interaction Patterns
1. Define event type in `EventType` enum
2. Configure detection parameters in `event_config.json`
3. Implement response in `HandleEvent()` callback
4. Add corresponding vibration pattern if needed

#### Creating Custom Vibration Patterns
```cpp
// Define keyframes with strength (0-4095) and duration (ms)
vibration_keyframe_t pattern[] = {
    {2000, 100},  // Medium strength for 100ms
    {0, 50},      // Pause for 50ms
    {4000, 200},  // Strong vibration for 200ms
    {0, 0}        // End marker
};
```

#### Emotion Integration
- Emotions can be triggered via MCP tools or internal events
- Each emotion automatically selects appropriate:
  - Display animation set
  - Vibration feedback pattern
  - Animation playback speed
  - Response behaviors

### Testing Features

#### Vibration Test Mode
- GPIO11 button cycles through all vibration patterns
- Useful for haptic feedback calibration
- Enable with: `vibration_skill_->EnableButtonTest(pattern, true)`

#### Motion Detection Debug
- Real-time IMU data output to console
- Configurable thresholds via `event_config.json`
- Visual indicators for detected gestures

#### Event Upload System
- Automatic event reporting to cloud services
- Supports MQTT and WebSocket protocols
- Configurable event filtering and batching
- Built-in retry mechanism for network failures

### Important Considerations

#### Power Management
- Vibration motor draws significant current during operation
- IMU sampling rate affects battery life (configurable 50-200Hz)
- Display brightness and animation frequency impact power consumption

#### Safety Features
- Free fall detection triggers immediate warning vibration
- Upside-down detection indicates potential distress
- Touch timeout prevents accidental activation
- Vibration strength limits prevent motor damage

#### Real-time Constraints
- Event processing runs at 50ms intervals (20Hz)
- IMU sampling synchronized with event timer
- Touch detection uses hardware interrupts
- Vibration patterns use dedicated FreeRTOS task

## Important Notes

### Board Type Uniqueness
Never modify existing board configurations for custom hardware - always create new board types to avoid OTA conflicts. Each board has unique firmware upgrade channels.

### Memory Management
- Use RAII patterns for resource management (DisplayLockGuard, etc.)
- Prefer stack allocation for small objects
- Use smart pointers for dynamic allocation when necessary

### Thread Safety
- Main application uses single-threaded event loop with scheduled callbacks
- Audio service uses separate FreeRTOS tasks with queue-based communication
- MCP tools execute in separate threads with proper synchronization
- Event processing uses dedicated timer callbacks for real-time response