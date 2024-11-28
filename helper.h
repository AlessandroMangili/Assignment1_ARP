#ifndef HELPER_H
#define HELPER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>

#define BOX_HEIGHT 3    // Height of the box of each key
#define BOX_WIDTH 5     // Width of the box of each key

#define TIMEOUT 10      // Number of seconds after which, if a process does not respond, the watchdog terminates all the processes
#define N_PROCS 3       // Number of processes of the watchdog

typedef struct
{
    float position_x, position_y;
    float velocity_x, velocity_y;
    float force_x, force_y;
} Info;

//typedef Info info;
static inline __attribute__((always_inline)) void writeLog(FILE* file, char* message)
{
    int lockResult = flock(fileno(file), LOCK_EX);
    if (lockResult == -1) {
        perror("Failed to lock the log file");
        exit(EXIT_FAILURE);
    }
    fprintf(file, "%s\n", message);
    fflush(file);
    int unlockResult = flock(fileno(file), LOCK_UN);
    if (unlockResult == -1) {
        perror("Failed to unlock the log file");
        exit(EXIT_FAILURE);
    }
}

#define LOG_TO_FILE(file, message)                                      \
    {                                                                   \
        char log[1024];                                                  \
        sprintf(log, "Generated at line [%d] by %s with the following message: %s", __LINE__, __FILE__, message);    \
        writeLog(file, log);                                            \
    }

#endif
