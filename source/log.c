#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

void logReset(void)
{
    mkdir("sdmc:/atmosphere", 0777);
    mkdir("sdmc:/atmosphere/logs", 0777);
    FILE *f = fopen(LOG_PATH, "w");
    if (f) fclose(f);
}

void logLine(const char *fmt, ...)
{
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}
