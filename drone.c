#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <math.h>
#include <errno.h>
#include <sys/select.h>
#include <pthread.h>
#include "helper.h"

FILE *debug, *errors;                               // File descriptors for the two log files
pid_t wd_pid;
float rho0 = 2, rho1 = 0.5, rho2 = 2, eta = 40;
Game game;
Drone *drone;
//Object obstacles, targets;

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

    if (fx > MAX_FREP) fx = MAX_FREP;
    if (fx < -MAX_FREP) fx = -MAX_FREP;

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

    if (fy > MAX_FREP) fy = MAX_FREP;
    if (fy < -MAX_FREP) fy = -MAX_FREP;

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

    if (drone->pos_x < 0) { drone->pos_x = 0; drone->vel_x = 0; drone->force_x = 0;}
    if (drone->pos_x >= game.max_x) { drone->pos_x = game.max_x - 1; drone->vel_x = 0; drone->force_x = 0;}
    if (drone->pos_y < 0) {drone->pos_y = 0; drone->vel_y = 0; drone->force_y = 0;}
    if (drone->pos_y >= game.max_y) { drone->pos_y = game.max_y - 1; drone->vel_y = 0; drone->force_y = 0;}
}

void *update_drone_position_thread() {
    while (1) {
        //sem_wait(drone->sem);
        update_drone_position(drone, T);
        //sem_post(drone->sem);
        usleep(50000);
    }
}

void handle_key_pressed(char key, Drone *drone) {
    switch (key) {
        case 'w': case 'W':
            drone->force_x -= 0.25;
            drone->force_y -= 0.25;
            break;
        case 'e': case 'E':
            drone->force_x -= 0;
            drone->force_y -= 0.5;
            break;
        case 'r': case 'R':
            drone->force_x += 0.25;
            drone->force_y -= 0.25;
            break;
        case 's': case 'S':
            drone->force_x -= 0.5;
            drone->force_y += 0;
            break;
        case 'd': case 'D':
            drone->force_x = 0;
            drone->force_y = 0;
            break;
        case 'f': case 'F':
            drone->force_x += 0.5;
            drone->force_y += 0;
            break;
        case 'x': case 'X':
            drone->force_x -= 0.25;
            drone->force_y += 0.25;
            break;
        case 'c': case 'C':
            drone->force_x += 0;
            drone->force_y += 0.5;
            break;
        case 'v': case 'V':
            drone->force_x += 0.25;
            drone->force_y += 0.25;
            break;
        default:
            break;
    }
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

int open_shared_memory() {
    int mem_fd = shm_open(DRONE_SHARED_MEMORY, O_RDWR, 0666);
    if (mem_fd == -1) {
        perror("Error opening the shared memory");
        LOG_TO_FILE(errors, "Error opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    drone = (Drone *)mmap(0, sizeof(Drone), PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, 0);
    if (drone == MAP_FAILED) {
        perror("Error mapping the shared memory");
        LOG_TO_FILE(errors, "Error mapping the shared memoryd");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    return mem_fd;
}

void drone_process(int map_read_fd, int input_read_fd, int obstacles_read_fd, int targets_read_fd) {
    char buffer[256];
    fd_set read_fds;
    struct timeval timeout;

    int max_fd = -1;
    if (map_read_fd > max_fd) {
        max_fd = map_read_fd;
    }
    if(input_read_fd > max_fd) {
        max_fd = input_read_fd;
    }
    if(obstacles_read_fd > max_fd) {
        max_fd = obstacles_read_fd;
    }
    if(targets_read_fd > max_fd) {
        max_fd = targets_read_fd;
    }

    while(1) {
        FD_ZERO(&read_fds);
        FD_SET(map_read_fd, &read_fds);
        FD_SET(input_read_fd, &read_fds);
        FD_SET(obstacles_read_fd, &read_fds);
        FD_SET(targets_read_fd, &read_fds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int activity;
        do {
            activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        } while(activity == -1 && errno == EINTR);

        if (activity < 0) {
            perror("Error in the drone's select");
            LOG_TO_FILE(errors, "Error in select which pipe reads");
            break;
        } else if (activity > 0) {
            if (FD_ISSET(map_read_fd, &read_fds)) {
                ssize_t bytes_read = read(map_read_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    sscanf(buffer, "%d, %d", &game.max_x, &game.max_y);
                }
            }
            if (FD_ISSET(input_read_fd, &read_fds)) {
                ssize_t bytes_read = read(input_read_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    //sem_wait(drone->sem);
                    handle_key_pressed(buffer[0], drone);
                    //sem_post(drone->sem);
                }
            }
            if (FD_ISSET(obstacles_read_fd, &read_fds)) {
                ssize_t bytes_read = read(obstacles_read_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    //sscanf(buffer, "%d, %d, %d, %c", &objects.pos_x, &objects.pos_y, &objects.point, &objects.type);
                    LOG_TO_FILE(errors, buffer);
                }
            }
            if (FD_ISSET(targets_read_fd, &read_fds)) {
                ssize_t bytes_read = read(targets_read_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    //sscanf(buffer, "%d, %d, %d, %c", &objects.pos_x, &objects.pos_y, &objects.point, &objects.type);
                    LOG_TO_FILE(errors, buffer);
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    /* OPEN THE LOG FILES */
    debug = fopen("debug.log", "a");
    if (debug == NULL) {
        perror("Error opening the debug file");
        exit(EXIT_FAILURE);
    }
    errors = fopen("errors.log", "a");
    if (errors == NULL) {
        perror("Error opening the errors file");
        exit(EXIT_FAILURE);
    }

    if (argc < 5) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    
    LOG_TO_FILE(debug, "Process started");

    /* SETUP THE PIPES */
    int map_read_fd = atoi(argv[1]);
    int input_read_fd = atoi(argv[2]);
    int obstacles_read_fd = atoi(argv[3]);
    int targets_read_fd = atoi(argv[4]);

    /* SETUP THE SIGNALS */
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);

    // Set the signal handler for SIGUSR1
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("Error in sigaction(SIGURS1)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS1)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR2
    if(sigaction(SIGUSR2, &sa, NULL) == -1){
        perror("Error in sigaction(SIGURS2)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS2)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    /* OPEN THE SHARED MEMORY */
    int mem_fd = open_shared_memory();

    /* IMPORT THE INITIAL CONFIGURATION */
    // Wait 2 seconds
    int diff;
    time_t start, finish;
    time(&start);
    do {
        time(&finish);
        diff = difftime(finish, start);
    } while (diff < 2);

    // Read the size of the map from the server
    char buffer[50];
    read(map_read_fd, buffer, sizeof(buffer) - 1);
    sscanf(buffer, "%d, %d", &game.max_x, &game.max_y);
    
    /* UPDATE THE DRONE POSITION */
    // Start the thread to continuously update the drone's information
    pthread_t drone_thread;
    if (pthread_create(&drone_thread, NULL, update_drone_position_thread, NULL) != 0) {
        perror("Error creating the thread for updating the drone's information");
        LOG_TO_FILE(errors, "Error creating the thread for updating the drone's information");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    /* LAUNCH THE DRONE */
    drone_process(map_read_fd, input_read_fd, obstacles_read_fd, targets_read_fd);

    /* END PROGRAM */
    // Close the file descriptor
    if (close(mem_fd) == -1) {
        perror("Close file descriptor");
        LOG_TO_FILE(errors, "Error in closing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Unmap the shared memory region
    munmap(drone, sizeof(Drone));
    
    // Close the files
    fclose(debug);
    fclose(errors);

    return 0;
}