#include <stdio.h>
#include <stdlib.h>
#include <bsd/stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <math.h>
#include <sys/select.h>
#include <errno.h>
#include "cJSON/cJSON.h"
#include "helper.h"

FILE *debug, *errors;
Game game;
pid_t wd_pid;
int N_OBS;
int obstacle_write_position_fd = -1;

void generate_obstacles(){
    Object obstacles[N_OBS];
    char obstacleStr[1024] = "";
    char temp[50];
    char msg[100];
    // create obstacles
    for (int i = 0; i < N_OBS; i++){
        // generates random coordinates
        obstacles[i].pos_x = arc4random_uniform(game.max_x-2) + 1; 
        obstacles[i].pos_y = arc4random_uniform(game.max_y-2) + 1;
        obstacles[i].type = 'o';
        obstacles[i].hit = false;
        sprintf(temp, i + 1 != N_OBS ? "%d,%d,%c,%d|" : "%d,%d,%c,%d", obstacles[i].pos_x, obstacles[i].pos_y, obstacles[i].type, (int)obstacles[i].hit);
        strcat(obstacleStr, temp);
    }
    write(obstacle_write_position_fd, obstacleStr, strlen(obstacleStr));
    obstacleStr[0] = '\0';
}

void signal_handler(int sig, siginfo_t* info, void *context) {
    if (sig == SIGUSR1) {
        wd_pid = info->si_pid;
        LOG_TO_FILE(debug, "Signal SIGUSR1 received from WATCHDOG");
        kill(wd_pid, SIGUSR1);
    }

    if (sig == SIGUSR2) {
        LOG_TO_FILE(debug, "Shutting down by the WATCHDOG");
        printf("Obstacle shutting down by the WATCHDOG: %d\n", getpid());
        // Close the files
        fclose(errors);
        fclose(debug);
        exit(EXIT_SUCCESS);
    }
    if (sig == SIGTERM) {
        if (obstacle_write_position_fd > 0) {
            LOG_TO_FILE(debug, "Generating new obstacles position");
            generate_obstacles();
        } else {
            printf("NOT SET\n");
        }
    }
}

int main(int argc, char* argv[]) {
    debug = fopen("debug.log", "a");
    if (debug == NULL) {
        perror("[OBSTACLE]: Error opening the debug file");
        exit(EXIT_FAILURE);
    }
    errors = fopen("errors.log", "a");
    if (errors == NULL) {
        perror("[OBSTACLE]: Error opening the error file");
        exit(EXIT_FAILURE);
    }
    
    if (argc < 3) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }

    LOG_TO_FILE(debug, "Process started");
    printf("Obstacle started: %d\n", getpid());

    /* Opens the semaphore for child process synchronization */
    sem_t *exec_sem = sem_open("/exec_semaphore", 0);
    if (exec_sem == SEM_FAILED) {
        perror("[OBSTACLE]: Failed to open the semaphore for the exec");
        LOG_TO_FILE(errors, "Failed to open the semaphore for the exec");
        exit(EXIT_FAILURE);
    }
    sem_post(exec_sem); // Releases the resource to proceed with the launch of other child processes
    sem_close(exec_sem);

    /* CREATE AND SETUP THE PIPES */
    obstacle_write_position_fd = atoi(argv[1]);
    int obstacle_read_map_fd = atoi(argv[2]);

    /* IMPORT CONFIGURATION PARAMETERS FROM THE MAIN */
    N_OBS = atoi(argv[3]);

    /* SETTING THE SIGNALS */
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);

    // Set the signal handler for SIGUSR1
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("[OBSTACLE]: Error in sigaction(SIGURS1)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS1)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR2
    if(sigaction(SIGUSR2, &sa, NULL) == -1){
        perror("[OBSTACLE]: Error in sigaction(SIGURS2)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS2)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGTERM
    if(sigaction(SIGTERM, &sa, NULL) == -1){
        perror("[OBSTACLE]: Error in sigaction(SIGTERM)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGTERM)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    // Add sigmask to block all signals execpt SIGURS1, SIGURS2 and SIGTERM
    sigset_t sigset;
    sigfillset(&sigset);
    sigdelset(&sigset, SIGUSR1);
    sigdelset(&sigset, SIGUSR2);
    sigdelset(&sigset, SIGTERM);
    sigprocmask(SIG_SETMASK, &sigset, NULL);
    
    char buffer[256];
    fd_set read_fds;
    struct timeval timeout;

    int max_fd = -1;
    if (obstacle_read_map_fd > max_fd) {
        max_fd = obstacle_read_map_fd;
    }

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(obstacle_read_map_fd, &read_fds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int activity;
        do {
            activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        } while(activity == -1 && errno == EINTR);

        if (activity < 0) {
            perror("[OBSTACLE]: Error in the server's select");
            LOG_TO_FILE(errors, "Error in select which pipe reads");
            break;
        } else if (activity > 0) {
            // Check if the map process has sent him the map size
            if (FD_ISSET(obstacle_read_map_fd, &read_fds)) {
                ssize_t bytes_read = read(obstacle_read_map_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0'; // End the string
                    sscanf(buffer, "%d, %d", &game.max_x, &game.max_y);
                    generate_obstacles();
                }
            }
        }
    }

    /* END PROGRAM */
    // Close the files
    fclose(debug);
    fclose(errors);

    return 0;
}