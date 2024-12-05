#ifndef HELPER_H
#define HELPER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <semaphore.h>
#include <time.h>

#define BOX_HEIGHT 3                        // Height of the box of each key
#define BOX_WIDTH 5                         // Width of the box of each key
#define TIMEOUT 10                          // Number of seconds after which, if a process does not respond, the watchdog terminates all the processes
#define N_PROCS 5                          // Number of processes of the watchdog
#define DRONE_SHARED_MEMORY "/drone_memory" // Name of the shared memory
#define MASS 2                              // Mass (kg) of the drone
#define FRICTION_COEFFICIENT 0.5            // Friction coefficient of the drone
#define FORCE_MODULE 1.0                    // Force module
#define T 0.5                               // Instant of time (dt)
#define MAX_FREP 15                         

typedef struct {
    float pos_x, pos_y;
    float vel_x, vel_y;
    float force_x, force_y;
    sem_t *sem;
} Drone;

typedef struct {
    int pos_x, pos_y;
    int point;
    char type;
} Object;

typedef struct {
    int max_x, max_y;
} Game;

static inline __attribute__((always_inline)) void writeLog(FILE* file, char* message) {
    char time_now[50];
    time_t log_time = time(NULL);
    strftime(time_now, sizeof(time_now), "%Y-%m-%d %H:%M:%S", localtime(&log_time));
    int lockResult = flock(fileno(file), LOCK_EX);
    if (lockResult == -1) {
        perror("Failed to lock the log file");
        exit(EXIT_FAILURE);
    }
    
    fprintf(file,"[%s] => %s\n", time_now, message);
    fflush(file);

    int unlockResult = flock(fileno(file), LOCK_UN);
    if (unlockResult == -1) {
        perror("Failed to unlock the log file");
        exit(EXIT_FAILURE);
    }
}

#define LOG_TO_FILE(file, message) {                                                                                \
    char log[4096];                                                                                                 \
    sprintf(log, "Generated at line [%d] by [%s] with the following message: %s", __LINE__, __FILE__, message);     \
    writeLog(file, log);                                                                                            \
}

#endif
