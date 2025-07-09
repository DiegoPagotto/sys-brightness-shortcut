// Include the most common headers from the C standard library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Include the main libnx system header, for Switch development
#include <switch.h>

// Size of the inner heap (adjust as necessary).
#define INNER_HEAP_SIZE 0x80000

#ifdef __cplusplus
extern "C" {
#endif

// Sysmodules should not use applet*.
u32 __nx_applet_type = AppletType_None;

// Sysmodules will normally only want to use one FS session.
u32 __nx_fs_num_sessions = 1;

// Newlib heap configuration function (makes malloc/free work).
void __libnx_initheap(void)
{
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void* fake_heap_start;
    extern void* fake_heap_end;

    // Configure the newlib heap.
    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

// Service initialization.
void __appInit(void)
{
    Result rc;

    // Open a service manager session.
    rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    // Retrieve the current version of Horizon OS.
    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    // Enable this if you want to use HID.
    rc = hidInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_HID));

    // Initialize backlight control service
    rc = lblInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    // Enable this if you want to use time.
    /*rc = timeInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_Time));

    __libnx_init_time();*/

    // Disable this if you don't want to use the filesystem.
    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    // Disable this if you don't want to use the SD card filesystem.
    fsdevMountSdmc();

    // Add other services you want to use here.

    // Close the service manager session.
    smExit();
}

// Service deinitialization.
void __appExit(void)
{
    // Close extra services you added to __appInit here.
    lblExit(); // Close backlight service
    hidExit(); // Close HID service
    fsdevUnmountAll(); // Disable this if you don't want to use the SD card filesystem.
    fsExit(); // Disable this if you don't want to use the filesystem.
    //timeExit(); // Enable this if you want to use time.
}

#ifdef __cplusplus
}
#endif

// Brightness control functions
Result setBrightness(float brightness) {
    // Ensure brightness is between 0.0 and 1.0
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    
    // Set brightness in settings
    Result rc = lblSetCurrentBrightnessSetting(brightness);
    if (R_FAILED(rc)) return rc;
    
    // Apply brightness immediately to screen
    rc = lblApplyCurrentBrightnessSettingToBacklight();
    if (R_FAILED(rc)) return rc;
    
    // Save the setting
    return lblSaveCurrentSetting();
}

Result getBrightness(float *brightness) {
    return lblGetCurrentBrightnessSetting(brightness);
}

// Main program entrypoint
int main(int argc, char* argv[])
{
    // Initialization code can go here.
    
    // Ensure the logs directory exists
    mkdir("sdmc:/atmosphere", 0777);
    mkdir("sdmc:/atmosphere/logs", 0777);
    
    // Create a log file to show the sysmodule is working
    FILE* logFile = fopen("sdmc:/atmosphere/logs/brightness_shortcut.log", "w");
    if (logFile) {
        fprintf(logFile, "Brightness Shortcut Sysmodule started successfully!\n");
        fprintf(logFile, "Title ID: 0x0100000000001337\n");
        fprintf(logFile, "Timestamp: %lu\n", armGetSystemTick());
        fprintf(logFile, "Process started at boot time\n");
        fprintf(logFile, "Controls: L+R+Up = Increase, L+R+Down = Decrease\n");
        fflush(logFile);
        fclose(logFile);
    }

    // Initialize controller input
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    
    // Brightness control variables
    float currentBrightness = 0.5f;
    bool lastUpPressed = false;
    bool lastDownPressed = false;
    
    // Get current brightness
    Result rc = getBrightness(&currentBrightness);
    if (R_FAILED(rc)) {
        logFile = fopen("sdmc:/atmosphere/logs/brightness_shortcut.log", "a");
        if (logFile) {
            fprintf(logFile, "Warning: Could not get current brightness: 0x%x\n", rc);
            fflush(logFile);
            fclose(logFile);
        }
        currentBrightness = 0.5f; // Default fallback
    }

    // Log initial brightness
    logFile = fopen("sdmc:/atmosphere/logs/brightness_shortcut.log", "a");
    if (logFile) {
        fprintf(logFile, "Initial brightness: %.1f%%\n", currentBrightness * 100.0f);
        fflush(logFile);
        fclose(logFile);
    }

    // Main loop - run indefinitely as a background service
    u64 startTick = armGetSystemTick();
    u64 lastLogTick = startTick;
    u64 ticksPerSecond = armGetSystemTickFreq();
    
    while (true) {
        padUpdate(&pad);
        u64 kHeld = padGetButtons(&pad);
        
        // Check if L and R are being pressed (combination for brightness control)
        if ((kHeld & HidNpadButton_L) && (kHeld & HidNpadButton_R)) {
            bool upPressed = (kHeld & HidNpadButton_Up) != 0;
            bool downPressed = (kHeld & HidNpadButton_Down) != 0;
            
            // Increase brightness (Up pressed and wasn't pressed before)
            if (upPressed && !lastUpPressed) {
                currentBrightness += 0.1f;
                if (currentBrightness > 1.0f) currentBrightness = 1.0f;
                
                Result rc = setBrightness(currentBrightness);
                logFile = fopen("sdmc:/atmosphere/logs/brightness_shortcut.log", "a");
                if (logFile) {
                    if (R_FAILED(rc)) {
                        fprintf(logFile, "Error setting brightness: 0x%x\n", rc);
                    } else {
                        // Update current value from system
                        getBrightness(&currentBrightness);
                        fprintf(logFile, "Brightness increased to: %.1f%%\n", currentBrightness * 100.0f);
                    }
                    fflush(logFile);
                    fclose(logFile);
                }
            }
            
            // Decrease brightness (Down pressed and wasn't pressed before)
            if (downPressed && !lastDownPressed) {
                currentBrightness -= 0.1f;
                if (currentBrightness < 0.0f) currentBrightness = 0.0f;
                
                Result rc = setBrightness(currentBrightness);
                logFile = fopen("sdmc:/atmosphere/logs/brightness_shortcut.log", "a");
                if (logFile) {
                    if (R_FAILED(rc)) {
                        fprintf(logFile, "Error setting brightness: 0x%x\n", rc);
                    } else {
                        // Update current value from system
                        getBrightness(&currentBrightness);
                        fprintf(logFile, "Brightness decreased to: %.1f%%\n", currentBrightness * 100.0f);
                    }
                    fflush(logFile);
                    fclose(logFile);
                }
            }
            
            lastUpPressed = upPressed;
            lastDownPressed = downPressed;
        } else {
            lastUpPressed = false;
            lastDownPressed = false;
        }
        
        // Log status every 5 minutes to show the service is running
        u64 currentTick = armGetSystemTick();
        u64 elapsedSeconds = (currentTick - startTick) / ticksPerSecond;
        if ((currentTick - lastLogTick) / ticksPerSecond >= 300) { // 5 minutes
            logFile = fopen("sdmc:/atmosphere/logs/brightness_shortcut.log", "a");
            if (logFile) {
                fprintf(logFile, "Service running... %lu seconds elapsed, brightness: %.1f%%\n", 
                        elapsedSeconds, currentBrightness * 100.0f);
                fflush(logFile);
                fclose(logFile);
            }
            lastLogTick = currentTick;
        }
        
        // Sleep for 50ms to reduce CPU usage while maintaining responsiveness
        svcSleepThread(50000000ULL); // 50ms in nanoseconds
    }

    // This should never be reached, but included for completeness
    return 0;
}
