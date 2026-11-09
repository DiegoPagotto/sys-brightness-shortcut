#pragma once

#define LOG_PATH "sdmc:/atmosphere/logs/brightness_shortcut.log"

// Creates the log directory and truncates the log file.
void logReset(void);

// Appends one line (newline added) to the log file.
void logLine(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
