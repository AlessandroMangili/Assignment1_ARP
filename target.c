#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <math.h>
#include <sys/select.h>
#include <errno.h>
#include "helper.h"

FILE *debug, *errors;
Game game;
Drone *drone;
pid_t wd_pid;
int N_TARGET;
int target_write_position_fd = -1;

void generate_targets(){
    Object targets[N_TARGET];
    char targetStr[1024] = "";
    char temp[50];
    char msg[100];
    // create targets
    for (int i = 0; i < N_TARGET; i++){
        // generates random coordinates
        targets[i].pos_x = rand() % (game.max_x-2) + 1; 
        targets[i].pos_y = rand() % (game.max_y-2) + 1;
        targets[i].point = 1;
        targets[i].type = 't';
        sprintf(temp, "%d,%d,%d,%c|", targets[i].pos_x, targets[i].pos_y, targets[i].point, targets[i].type);
        strcat(targetStr, temp);
    }
    write(target_write_position_fd, targetStr, strlen(targetStr));
    targetStr[0] = '\0';
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

    if (sig == SIGTERM) {
        if (target_write_position_fd > 0) {
            LOG_TO_FILE(debug, "Generating new targets position");
            generate_targets();
        }
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
        LOG_TO_FILE(errors, "Error mapping the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    return mem_fd;
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
    
    if (argc < 2) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }

    LOG_TO_FILE(debug, "Process started");

    /* CREATE AND SETUP THE PIPES */
    target_write_position_fd = atoi(argv[1]);
    int target_read_map_fd = atoi(argv[2]);

    /* IMPORT CONFIGURATION PARAMETERS FROM THE MAIN */
    N_TARGET = atoi(argv[3]);

    /* SETTING THE SIGNALS */
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
    // Set the signal handler for SIGUSR2
    if(sigaction(SIGTERM, &sa, NULL) == -1){
        perror("Error in sigaction(SIGTERM)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGTERM)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    /* OPEN SHARED MEMORY */
    int mem_fd = open_shared_memory();

    char buffer[256];
    fd_set read_fds;
    struct timeval timeout;

    int max_fd = -1;
    if (target_read_map_fd > max_fd) {
        max_fd = target_read_map_fd;
    }

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(target_read_map_fd, &read_fds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int activity;
        do {
            activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        } while(activity == -1 && errno == EINTR);

        if (activity < 0) {
            perror("Error in the server's select");
            LOG_TO_FILE(errors, "Error in select which pipe reads");
            break;
        } else if (activity > 0) {
            // Check if the map process has sent him the map size
            if (FD_ISSET(target_read_map_fd, &read_fds)) {
                ssize_t bytes_read = read(target_read_map_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0'; // End the string
                    sscanf(buffer, "%d, %d", &game.max_x, &game.max_y);
                    generate_targets();
                }
            }
        }
    }    

    // Close the file descriptor
    if (close(mem_fd) == -1) {
        perror("Close file descriptor");
        LOG_TO_FILE(errors, "Error in closing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    
    // Close the files
    fclose(debug);
    fclose(errors);

    return 0;
}