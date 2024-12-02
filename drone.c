#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <math.h>
#include "helper.h"

FILE *debug, *errors;                               // File descriptors for the two log files
pid_t wd_pid;
float rho0 = 2, rho1 = 0.5, rho2 = 2, eta = 40;

float calculate_friction_force(float velocity) {
    return -FRICTION_COEFFICIENT * velocity;
}

float calculate_repulsive_forcex(Drone drone, int xo, int yo) {
    float rho = sqrt(pow(drone.pos_x - xo, 2) + pow(drone.pos_y - yo, 2));
    if (rho < 0.5) rho = 0.5;
    float theta = atan2(drone.pos_y - yo, drone.pos_x - xo);
    float fx;

    if (rho < rho0) {
        fx = eta * (1 / rho - 1 / rho0) * cos(theta) * fabs(drone.vel_x);
    } else {
        fx = 0;
    }

    if (fx > MAXFREP) fx = MAXFREP;
    if (fx < -MAXFREP) fx = -MAXFREP;

    return fx;
}

float calculate_repulsive_forcey(Drone drone, int xo, int yo) {
    float rho = sqrt(pow(drone.pos_x - xo, 2) + pow(drone.pos_y - yo, 2));
    if (rho < 0.5) rho = 0.5;
    float theta = atan2(drone.pos_y - yo, drone.pos_x - xo);
    float fy;

    if (rho < rho0) {
        fy = eta * (1 / rho - 1 / rho0) * sin(theta) * fabs(drone.vel_y);
    } else {
        fy = 0;
    }

    if (fy > MAXFREP) fy = MAXFREP;
    if (fy < -MAXFREP) fy = -MAXFREP;

    return fy;
}

void update_drone_position(Drone *drone, float dt) {
    float fx_obs = 0;
    float fy_obs = 0;

    float frictionForceX = calculate_friction_force(drone->vel_x);
    float frictionForceY = calculate_friction_force(drone->vel_y);
    
    float accelerationX = (drone->force_x + frictionForceX + fx_obs) / MASS;
    float accelerationY = (drone->force_y + frictionForceY + fy_obs) / MASS;

    drone->vel_x += accelerationX * dt;
    drone->vel_y += accelerationY * dt;
    drone->pos_x += drone->vel_x * dt + 0.5 * accelerationX * dt * dt;
    drone->pos_y += drone->vel_y * dt + 0.5 * accelerationY * dt * dt;
}

void signal_handler(int sig, siginfo_t* info, void *context) {
    if (sig == SIGUSR1) {
        wd_pid = info->si_pid;
        LOG_TO_FILE(debug, "Signal SIGUSR1 received from WATCHDOG");
        kill(wd_pid, SIGUSR1);
    }

    if (sig == SIGUSR2) {
        LOG_TO_FILE(debug, "Shutting down by the WATCHDOG");
        // Close the files
        fclose(errors);
        fclose(debug);
        exit(EXIT_SUCCESS);
    }
}

int main(int argc, char* argv[]) {
    debug = fopen("debug.log", "a");
    if (debug == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    errors = fopen("errors.log", "a");
    if (errors == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    LOG_TO_FILE(debug, "Process started");

    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);

    // Set the signal handler for SIGUSR1
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS1)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR2
    if(sigaction(SIGUSR2, &sa, NULL) == -1){
        perror("sigaction");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS2)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    Drone *drone;
    const char *shared_memory = "/drone_memory";
    const int SIZE = 4096;
    int i, mem_fd;
    mem_fd = shm_open(shared_memory, O_RDWR, 0666); // open shared memory segment for read and write
    if (mem_fd == -1) {
        perror("Opening the shared memory \n");
        LOG_TO_FILE(errors, "Error in opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    drone = (Drone *)mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, 0); // protocol write
    if (drone == MAP_FAILED) {
        perror("Map failed");
        LOG_TO_FILE(errors, "Map Failed");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    //import drone initial position from the server
    sleep(5);

    int x0 = drone->pos_x;
    int y0 = drone->pos_y;

    while(1) {
        update_drone_position(drone, T);
        usleep(50000); 
    }

    if (shm_unlink(shared_memory) == -1) { // Remove shared memory segment.
        perror("Unlink shared memory");
        LOG_TO_FILE(errors, "Error in removing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    if (close(mem_fd) == -1) {
        perror("Close file descriptor");
        LOG_TO_FILE(errors, "Error in closing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    munmap(drone, SIZE);
    
    // Close the files
    fclose(debug);
    fclose(errors);

    return 0;
}