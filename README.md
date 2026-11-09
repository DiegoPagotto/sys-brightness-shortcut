# Nintendo Switch Brightness Control Sysmodule

A Nintendo Switch system module (sysmodule) that provides convenient brightness control shortcuts with an on-screen brightness bar and audio feedback.

## Features

-   **Quick Brightness Control**: Use L+R+Up/Down button combinations to adjust screen brightness
-   **On-Screen Indicator**: A Steam Deck-style bar (sun icon, animated fill, percentage) slides in at the top-left corner of the screen on every change and fades out 1.5s after the last one. It draws on its own display layer (registered in every layer stack, like Tesla/Ultrahand overlays), so it works over games and the HOME menu; it also shows up in screenshots and recordings taken while it is visible
-   **Audio Feedback**: A soft, mallet-like "tock" (smooth attack, decaying harmonics, short stereo reflections) instead of a raw beep. Pitch follows the brightness level along a pentatonic scale, so going up or down plays a rising or falling melody
-   **Continuous Adjustment**: Hold buttons for continuous brightness changes with proper timing delays
-   **Background Service**: Runs as a system service that starts automatically with the console. HID, backlight and audio services are opened 10 seconds after boot, so the shortcut becomes available shortly after the HOME menu appears
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
2. Copy the generated `.nsp` file to: `sdmc:/atmosphere/contents/0004000001EB3400/exefs.nsp`
3. Create the boot flag file: `sdmc:/atmosphere/contents/0004000001EB3400/flags/boot2.flag`
4. Reboot your Nintendo Switch

## Technical Details

-   **Title ID**: `0x0004000001EB3400`
-   **Services Used**: HID (input), LBL (backlight), AudioOut (sound effects), VI + nvdrv (on-screen indicator), FS (file system)
-   **Session Usage**: LBL is connected only for the duration of each brightness change. LBL has a tight system-wide session limit, and Atmosphère's fatal handler aborts if it cannot open one to show its error screen (holding an LBL session permanently can turn any fatal error into an Exosphère panic)
-   **CPU Usage**: 50ms polling while idle; ~60 Hz only while the indicator is animating
-   **Audio**: 48kHz stereo, 120ms synthesized ticks queued through a small buffer pool
-   **Indicator**: 320x72 RGBA8888 software-rendered framebuffer on a managed VI layer (480x108 at 1080p). The layer is created right at startup, before the HOME menu comes up (layers created afterwards are never shown), and kept for the process lifetime with a transparent frame while hidden
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

-   `sys-brightness-shortcut.nsp` - The system module file
-   `sys-brightness-shortcut.npdm` - Metadata file

## File Structure

```
├── source/
│   ├── main.c          # Input handling and brightness control
│   ├── osd.c           # Display layer setup and indicator animation
│   ├── osd_render.c    # Indicator renderer (pure C, anti-aliased shapes and digits)
│   ├── sound.c         # AudioOut buffer management
│   ├── tone.c          # Feedback sound synthesis
│   └── log.c           # SD card logging
├── sys-brightness-shortcut.json # System module configuration
├── Makefile           # Build configuration
├── deploy.sh          # Deployment script
├── verify.sh          # Verification script
└── README.md          # This file
```

## Logging

The sysmodule creates logs at `sdmc:/atmosphere/logs/brightness_shortcut.log` with:

-   Startup confirmation and per-service initialization steps with system ticks (comparable to `OccurrenceTick` in `atmosphere/erpt_reports`)
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
