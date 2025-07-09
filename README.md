# Nintendo Switch Brightness Control Sysmodule

A Nintendo Switch system module (sysmodule) that provides convenient brightness control shortcuts with audio feedback.

## Features

-   **Quick Brightness Control**: Use L+R+Up/Down button combinations to adjust screen brightness
-   **Audio Feedback**: Plays distinctive sound effects when brightness is changed (higher pitch for increase, lower pitch for decrease)
-   **Continuous Adjustment**: Hold buttons for continuous brightness changes with proper timing delays
-   **Background Service**: Runs as a system service that starts automatically with the console
-   **Logging**: Creates detailed logs for debugging and monitoring at `sdmc:/atmosphere/logs/brightness_shortcut.log`

## Controls

| Button Combination | Action                     |
| ------------------ | -------------------------- |
| **L + R + Up**     | Increase brightness by 10% |
| **L + R + Down**   | Decrease brightness by 10% |

Hold the buttons for continuous adjustment. Initial delay of 500ms, then repeats every 100ms.

## Installation

### Automated Installation (Recommended)

1. Build the project: `make`
2. Run the deployment script: `./deploy.sh`
3. Copy the contents of the generated `./deploy/` folder to your SD card root
4. Reboot your Nintendo Switch

The deploy script will automatically:

-   Create the proper directory structure
-   Copy the NSP file as `exefs.nsp`
-   Create the boot flag for auto-loading
-   Include Hekate toolbox integration

### Manual Installation

If you prefer manual installation:

1. Build the project using `make` (requires devkitPro and libnx)
2. Copy the generated `.nsp` file to: `sdmc:/atmosphere/contents/0100000000001337/exefs.nsp`
3. Create the boot flag file: `sdmc:/atmosphere/contents/0100000000001337/flags/boot2.flag`
4. Reboot your Nintendo Switch

## Technical Details

-   **Title ID**: `0x0100000000001337`
-   **Services Used**: HID (input), LBL (backlight), AudioOut (sound effects), FS (file system)
-   **CPU Usage**: Optimized with 50ms sleep intervals to minimize system impact
-   **Audio**: 48kHz stereo output with generated sine wave tones
-   **Brightness Range**: 0% to 100% in 10% increments

## Building

### Prerequisites

-   devkitPro with devkitA64 and libnx
-   Nintendo Switch homebrew development environment

### Build Commands

```bash
make clean
make
```

The build process will generate:

-   `sysmodule.nsp` - The system module file
-   `sysmodule.npdm` - Metadata file

## File Structure

```
├── source/
│   └── main.c          # Main sysmodule implementation
├── sysmodule.json      # System module configuration
├── Makefile           # Build configuration
├── deploy.sh          # Deployment script
├── verify.sh          # Verification script
└── README.md          # This file
```

## Logging

The sysmodule creates logs at `sdmc:/atmosphere/logs/brightness_shortcut.log` with:

-   Startup confirmation
-   Brightness change events with timestamps
-   Error messages for debugging
-   Periodic status updates (every 5 minutes)

## Compatibility

-   **Firmware**: Works with Atmosphère custom firmware
-   **Hardware**: Nintendo Switch (all models)
-   **System**: Requires Atmosphère with sysmodule support

## Safety Features

-   Brightness values are clamped between 0.0 and 1.0
-   Audio buffer management prevents memory leaks
-   Error handling for all system service calls
-   Proper cleanup on service termination

## Contributing

This project follows standard Nintendo Switch homebrew development practices. When contributing:

1. Ensure code follows the existing style
2. Test thoroughly on hardware
3. Update documentation as needed
4. Add appropriate error handling

## License

This project is intended for homebrew development and educational purposes.
