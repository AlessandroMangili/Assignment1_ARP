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
#include <stdbool.h>
#include <pthread.h>
#include "helper.h"

FILE *debug, *errors;                               // File descriptors for the two log files
pid_t wd_pid;
float rho0 = 2, rho1 = 0.5, rho2 = 2, eta = 40;
Game game;
Drone *drone;
float *score;
int n_obs, n_targ;
Object *obstacles, *targets;

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

void check_hit(Drone *drone, Object *object, int dim) {
    for (int i = 0; i < dim; i++) {
        /**
         * We add 0.5 to the position of each symbol because this way we 
         * calculate the distance starting from the center of the symbol 
         * rather than from the top-left corner
        */ 
        float distance = sqrt(pow(drone->pos_x - (object[i].pos_x + 0.5), 2) + pow(drone->pos_y - (object[i].pos_y + 0.5), 2));
        if (distance <= HIT_THR && !object[i].hit) {
            *score += object[i].type == 't' ? 1 : 0;
            object[i].hit = true;
        }
    }
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
        update_drone_position(drone, T);
        check_hit(drone, obstacles, n_obs);
        check_hit(drone, targets, n_targ);
        usleep(50000);
    }
}

void handle_key_pressed(char key, Drone *drone) {
    switch (key) {
        case 'w': case 'W':
            drone->force_x -= 0.1;
            drone->force_y -= 0.1;
            break;
        case 'e': case 'E':
            drone->force_x -= 0;
            drone->force_y -= 0.1;
            break;
        case 'r': case 'R':
            drone->force_x += 0.1;
            drone->force_y -= 0.1;
            break;
        case 's': case 'S':
            drone->force_x -= 0.1;
            drone->force_y += 0;
            break;
        case 'd': case 'D':
            drone->force_x = 0;
            drone->force_y = 0;
            break;
        case 'f': case 'F':
            drone->force_x += 0.1;
            drone->force_y += 0;
            break;
        case 'x': case 'X':
            drone->force_x -= 0.1;
            drone->force_y += 0.1;
            break;
        case 'c': case 'C':
            drone->force_x += 0;
            drone->force_y += 0.1;
            break;
        case 'v': case 'V':
            drone->force_x += 0.1;
            drone->force_y += 0.1;
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

int open_drone_shared_memory() {
    int drone_mem_fd = shm_open(DRONE_SHARED_MEMORY, O_RDWR, 0666);
    if (drone_mem_fd == -1) {
        perror("Error opening the drone shared memory");
        LOG_TO_FILE(errors, "Error opening the drone shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    drone = (Drone *)mmap(0, sizeof(Drone), PROT_READ | PROT_WRITE, MAP_SHARED, drone_mem_fd, 0);
    if (drone == MAP_FAILED) {
        perror("Error mapping the drone shared memory");
        LOG_TO_FILE(errors, "Error mapping the drone shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    LOG_TO_FILE(debug, "Opened the drone shared memory");
    return drone_mem_fd;
}

int open_score_shared_memory() {
    int score_mem_fd = shm_open(SCORE_SHARED_MEMORY, O_RDWR, 0666);
    if (score_mem_fd == -1) {
        perror("Error opening the score shared memory");
        LOG_TO_FILE(errors, "Error opening the score shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    score = (float *)mmap(0, sizeof(float), PROT_READ | PROT_WRITE, MAP_SHARED, score_mem_fd, 0);
    if (score == MAP_FAILED) {
        perror("Error mapping the score shared memory");
        LOG_TO_FILE(errors, "Error mapping the score shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    LOG_TO_FILE(debug, "Opened the score shared memory");
    return score_mem_fd;
}

void drone_process(int map_read_size_fd, int input_read_key_fd, int obstacles_read_position_fd, int targets_read_position_fd) {
    char buffer[256];
    fd_set read_fds;
    struct timeval timeout;

    int max_fd = -1;
    if (map_read_size_fd > max_fd) {
        max_fd = map_read_size_fd;
    }
    if(input_read_key_fd > max_fd) {
        max_fd = input_read_key_fd;
    }
    if(obstacles_read_position_fd > max_fd) {
        max_fd = obstacles_read_position_fd;
    }
    if(targets_read_position_fd > max_fd) {
        max_fd = targets_read_position_fd;
    }
    while(1) {
        FD_ZERO(&read_fds);
        FD_SET(map_read_size_fd, &read_fds);
        FD_SET(input_read_key_fd, &read_fds);
        FD_SET(obstacles_read_position_fd, &read_fds);
        FD_SET(targets_read_position_fd, &read_fds);

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
            if (FD_ISSET(map_read_size_fd, &read_fds)) {
                ssize_t bytes_read = read(map_read_size_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    sscanf(buffer, "%d, %d", &game.max_x, &game.max_y);
                }
            }
            if (FD_ISSET(input_read_key_fd, &read_fds)) {
                ssize_t bytes_read = read(input_read_key_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    handle_key_pressed(buffer[0], drone);
                }
            }
            if (FD_ISSET(obstacles_read_position_fd, &read_fds)) {
                ssize_t bytes_read = read(obstacles_read_position_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    char *token = strtok(buffer, "|");
                    int i = 0;
                    memset(obstacles, 0, n_obs * sizeof(obstacles));
                    while (token != NULL) {
                        sscanf(token, "%d,%d,%c,%d", &obstacles[i].pos_x, &obstacles[i].pos_y, &obstacles[i].type, (int *)&obstacles[i].hit);
                        token = strtok(NULL, "|");
                        i++;
                    }
                    
                }
            }
            if (FD_ISSET(targets_read_position_fd, &read_fds)) {
                ssize_t bytes_read = read(targets_read_position_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    char *token = strtok(buffer, "|");
                    int i = 0;
                    memset(targets, 0, n_targ * sizeof(targets));
                    while (token != NULL) {
                        sscanf(token, "%d,%d,%c,%d", &targets[i].pos_x, &targets[i].pos_y, &targets[i].type, (int *)&targets[i].hit);
                        token = strtok(NULL, "|");
                        i++;
                    }
                }
            }
        }
    }

    close(map_read_size_fd);
    close(input_read_key_fd);
    close(obstacles_read_position_fd);
    close(targets_read_position_fd);
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

    if (argc < 7) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    
    LOG_TO_FILE(debug, "Process started");

    /* SETUP THE PIPES */
    int map_read_size_fd = atoi(argv[1]);
    int input_read_key_fd = atoi(argv[2]);
    int obstacles_read_position_fd = atoi(argv[3]);
    int targets_read_position_fd = atoi(argv[4]);
    n_obs = atoi(argv[5]), n_targ = atoi(argv[6]);

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

    obstacles = (Object *)malloc(n_obs * sizeof(Object));
    if (obstacles == NULL) {
        perror("Error allocating the memory for the obstacles");
        LOG_TO_FILE(errors, "Error allocating the memory for the obstacles");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    targets = (Object *)malloc(n_targ * sizeof(Object));
    if (targets == NULL) {
        perror("Error allocating the memory for the targets");
        free(obstacles);
        LOG_TO_FILE(errors, "Error allocating the memory for the targets");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    memset(obstacles, 0, n_obs * sizeof(obstacles));
    memset(targets, 0, n_targ * sizeof(targets));

    /* OPEN THE SHARED MEMORY */
    int drone_mem_fd = open_drone_shared_memory();
    int score_mem_fd = open_score_shared_memory();

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
    read(map_read_size_fd, buffer, sizeof(buffer) - 1);
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
    drone_process(map_read_size_fd, input_read_key_fd, obstacles_read_position_fd, targets_read_position_fd);

    /* END PROGRAM */
    // Close the file descriptor
    if (close(drone_mem_fd) == -1 || close(score_mem_fd) == -1) {
        perror("Close file descriptor");
        LOG_TO_FILE(errors, "Error closing the file descriptor of the memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Unmap the shared memory region
    munmap(drone, sizeof(Drone));
    munmap(score, sizeof(float));

    free(obstacles);
    free(targets);
    
    // Close the files
    fclose(debug);
    fclose(errors);

    return 0;
}