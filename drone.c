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

    if (drone->pos_x < 0) { drone->pos_x = 0; drone->vel_x = 0; drone->force_x = 0;}
    if (drone->pos_x >= game.max_x) { drone->pos_x = game.max_x - 5; drone->vel_x = 0; drone->force_x = 0;}
    if (drone->pos_y < 0) {drone->pos_y = 0; drone->vel_y = 0; drone->force_y = 0;}
    if (drone->pos_y >= game.max_y) { drone->pos_y = game.max_y - 5; drone->vel_y = 0; drone->force_y = 0;}
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
            drone->force_x += -0.5;
            drone->force_y += 0.5;
            break;
        case 'e': case 'E':
            drone->force_x += 0;
            drone->force_y += 0.5;
            break;
        case 'r': case 'R':
            drone->force_x += 0.5;
            drone->force_y += 0.5;
            break;
        case 's': case 'S':
            drone->force_x += -0.5;
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
            drone->force_x += -0.5;
            drone->force_y += -0.5;
            break;
        case 'c': case 'C':
            drone->force_x += 0;
            drone->force_y += -0.5;
            break;
        case 'v': case 'V':
            drone->force_x += 0.5;
            drone->force_y += -0.5;
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

    if (argc < 3) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Converti il parametro passato in un file descriptor
    int map_pipe_fd = atoi(argv[1]);
    int input_pipe_fd = atoi(argv[2]);

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

    const int SIZE = 4096;
    const char *drone_shared_memory = "/drone_memory";
    int drone_mem_fd = shm_open(drone_shared_memory, O_RDWR, 0666);
    if (drone_mem_fd == -1) {
        perror("Opening the shared memory");
        LOG_TO_FILE(errors, "Error in opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    drone = (Drone *)mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, drone_mem_fd, 0);
    if (drone == MAP_FAILED) {
        perror("Map failed");
        LOG_TO_FILE(errors, "Map Failed");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    //import drone initial position from the server
    int diff;
    time_t start, finish;
    time(&start);
    do {
        time(&finish);
        diff = difftime(finish, start);
    } while (diff < 2);

    int x0 = drone->pos_x;
    int y0 = drone->pos_y;

    char buffer[50];
    read(map_pipe_fd, buffer, sizeof(buffer) - 1);
    sscanf(buffer, "%d, %d", &game.max_x, &game.max_y);
    printf("[DRONE]: Received update: %d %d\n", game.max_x, game.max_y);
    
    pthread_t drone_thread;
    if (pthread_create(&drone_thread, NULL, update_drone_position_thread, NULL) != 0) {
        perror("pthread_create");
        LOG_TO_FILE(errors, "Error on creating the pthread");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    printf("AFTER\n");

    fd_set read_fds;
    struct timeval timeout;
    // Aggiungiamo il file descriptor della pipe al set monitorato
    int max_fd = -1;
    
    if (map_pipe_fd > max_fd) {
        max_fd = map_pipe_fd;
    }
    if(input_pipe_fd > max_fd) {
        max_fd = input_pipe_fd;
    }
    while(1) {
        FD_ZERO(&read_fds);
        FD_SET(map_pipe_fd, &read_fds);
        FD_SET(input_pipe_fd, &read_fds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int activity;
        do{
            activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        }while(activity == -1 && errno == EINTR);

        if (activity < 0) {
            perror("select");
            LOG_TO_FILE(errors, "Error in select");
            break;
        } else if (activity == 0) {
            continue;
        }

        // Verifica se ci sono dati da leggere sulla pipe
        if (FD_ISSET(map_pipe_fd, &read_fds)) {
            ssize_t bytes_read = read(map_pipe_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0'; // Termina la stringa
                sscanf(buffer, "%d, %d", &game.max_x, &game.max_y);
                printf("[DRONE]: Received update M: %d %d\n", game.max_x, game.max_y);
            }
        }
        if (FD_ISSET(input_pipe_fd, &read_fds)) {
            ssize_t bytes_read = read(input_pipe_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0'; // Termina la stringa
                printf("[DRONE]: Received update I: %s\n", buffer);
                //sem_wait(drone->sem);
                handle_key_pressed(buffer[0], drone);
                //sem_post(drone->sem);
            }
        }
    }
    // Unlink the shared memory, close the file descriptor, and unmap the shared memory region
    if (shm_unlink(drone_shared_memory) == -1) {
        perror("Unlink shared memory");
        LOG_TO_FILE(errors, "Error in removing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    if (close(drone_mem_fd) == -1) {
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