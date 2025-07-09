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
    /*rc = hidInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_HID));*/

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
    fsdevUnmountAll(); // Disable this if you don't want to use the SD card filesystem.
    fsExit(); // Disable this if you don't want to use the filesystem.
    //timeExit(); // Enable this if you want to use time.
    //hidExit(); // Enable this if you want to use HID.
}

#ifdef __cplusplus
}
#endif

// Main program entrypoint
int main(int argc, char* argv[])
{
    // Initialization code can go here.
    
    // Ensure the logs directory exists
    mkdir("sdmc:/atmosphere", 0777);
    mkdir("sdmc:/atmosphere/logs", 0777);
    
    // Create a log file to show the sysmodule is working
    FILE* logFile = fopen("sdmc:/atmosphere/logs/example_sysmodule.log", "w");
    if (logFile) {
        fprintf(logFile, "Example Sysmodule started successfully!\n");
        fprintf(logFile, "Title ID: 0x0100000000001337\n");
        fprintf(logFile, "Timestamp: %lu\n", armGetSystemTick());
        fprintf(logFile, "Process started at boot time\n");
        fflush(logFile);
        fclose(logFile);
    }

    // Your code / main loop goes here.
    // Simple example: run for 60 seconds then exit
    u64 startTick = armGetSystemTick();
    u64 ticksPerSecond = armGetSystemTickFreq();
    
    // Log every 10 seconds to show it's running
    u64 lastLogTick = startTick;
    
    while (true) {
        u64 currentTick = armGetSystemTick();
        u64 elapsedSeconds = (currentTick - startTick) / ticksPerSecond;
        
        // Log every 10 seconds
        if ((currentTick - lastLogTick) / ticksPerSecond >= 10) {
            logFile = fopen("sdmc:/atmosphere/logs/example_sysmodule.log", "a");
            if (logFile) {
                fprintf(logFile, "Sysmodule running... %lu seconds elapsed\n", elapsedSeconds);
                fflush(logFile);
                fclose(logFile);
            }
            lastLogTick = currentTick;
        }
        
        // Exit after 60 seconds for demonstration
        if (elapsedSeconds >= 60) {
            break;
        }
        
        // Sleep for 1 second
        svcSleepThread(1000000000ULL); // 1 second in nanoseconds
    }
    
    // Write completion log
    logFile = fopen("sdmc:/atmosphere/logs/example_sysmodule.log", "a");
    if (logFile) {
        fprintf(logFile, "Sysmodule completed execution after 60 seconds\n");
        fprintf(logFile, "Process terminated normally\n");
        fflush(logFile);
        fclose(logFile);
    }

    // Deinitialization and resources clean up code can go here.
    return 0;
}
