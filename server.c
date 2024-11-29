#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>
#include <string.h>
#include <sys/time.h>
#include "helper.h"

// Number of processes
#define NUM_CLIENTS 4
// Shared memory
int shared_memory[2] = {0};
int shared_memory_readers[2] = {0};
FILE *debug, *errors;                       // File descriptors for the two log files
pid_t wd_pid, map_pid;

typedef struct {
    int client_get_fd; // Used by server for reading messages from client
    int client_put_fd; // Used by client for writing messages to the server
    int server_get_fd; // Used by client for reading messages from server
    int server_put_fd; // Used by server for writing messages to the client
} Pipe;

void server(Pipe pipes[], int shared_memory[]) {
    // Close useless file descriptor
    for (int i = 0; i < NUM_CLIENTS; i++) {
        close(pipes[i].client_put_fd);
        close(pipes[i].server_get_fd);
    }

    fd_set read_fds; // Set of file descriptor for the 'select'
    struct timeval timeout;
    int max_fd = -1;
    // Find the maximum file descriptor to set the select
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (pipes[i].client_get_fd > max_fd) {
            max_fd = pipes[i].client_get_fd;
        }
    }

    char state = 'w';
    while(1) {  
        FD_ZERO(&read_fds);
        // Add all file descriptors to monitor requests from processes
        for (int i = 0; i < NUM_CLIENTS; i++) {
            FD_SET(pipes[i].client_get_fd, &read_fds);
        }

        // Timeout of 5 seconds
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0) {
            perror("Error in the select");
            exit(EXIT_FAILURE);
        } else if (activity) {
            for (int i = 0; i < NUM_CLIENTS; i++) {
                int fd[2] = {-1};
                switch (i) {
                    case 0:
                        if (shared_memory[0] == 0 && shared_memory[1] == 0 || shared_memory[0] <= shared_memory[1]) {
                            fd[0] = pipes[i].client_get_fd;
                            fd[1] = pipes[i].server_put_fd;
                        }
                        break;
                    case 1:
                        if (shared_memory[1] <= shared_memory[0]) {
                            fd[0] = pipes[i].client_get_fd;
                            fd[1] = pipes[i].server_put_fd;
                        }
                        break;
                    case 2:
                        if (shared_memory[0] != shared_memory_readers[0]) {
                            fd[0] = pipes[i].client_get_fd;
                            fd[1] = pipes[i].server_put_fd;
                        }
                        break;
                    case 3:
                        if (shared_memory[1] != shared_memory_readers[1]) {
                            fd[0] = pipes[i].client_get_fd;
                            fd[1] = pipes[i].server_put_fd;
                        }
                        break;
                }
                
                if(FD_ISSET(fd[0],&read_fds) && state == 'w') {
                    state = 'e';
                    char request;
                    read(fd[0], &request, 1);
                    if (request == '?') {
                        printf("[SERVER]: processing writer%d\n", i + 1);
                        // Send the ack to the writer
                        write(fd[1], "OK", 2);  
                        
                        // Waits the number to store
                        char number_str[256];
                        read(fd[0], number_str, sizeof(number_str));
                        int number = atoi(number_str);

                        // Store the number into the shared memory
                        shared_memory[i] = number;
                        printf("[SERVER]: stored number: [%d] from writer%d\n", number, i + 1);
                    } else if (request == '!') {
                        printf("[SERVER]: processing reader%d\n", (i%2) + 1);
                        char response[256];
                        snprintf(response, sizeof(response), "%d", shared_memory[i%2]);
                        shared_memory_readers[i%2] = shared_memory[i%2];
                        // Send the number stored to the reader
                        write(fd[1], response, strlen(response));
                        printf("[SERVER]: sent number: [%d] to the reader%d\n", shared_memory[i%2], (i%2) + 1);
                    }
                    state = 'w';
                }
                //sleep(1);
            }
        } else {
            printf("No process are ready to write\n");
            sleep(2);
        }
    }
    for (int i = 0; i < NUM_CLIENTS; i++) {
        close(pipes[i].client_get_fd);
        close(pipes[i].server_put_fd);
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
        if (kill(map_pid, SIGTERM) == -1) {
            perror("kill");
            LOG_TO_FILE(errors, "Error in sending SIGTERM signal to the MAP");
            exit(EXIT_FAILURE);
        }
        // Close the files
        fclose(errors);
        fclose(debug);
        
        exit(EXIT_SUCCESS);
    }
}

int main(int argc, char *argv[]) {
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

    char *map_window_path[] = {"konsole", "-e", "./map_window", NULL};
    map_pid = fork();
    if (map_pid ==-1){
        perror("fork");
        LOG_TO_FILE(errors, "Error in forking the map window file");
        exit(EXIT_FAILURE);
    }
    if (map_pid == 0){
        execvp(map_window_path[0], map_window_path);
        perror("Exec failed");
        LOG_TO_FILE(errors, "Unable to exec the map_window process");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    
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

    while (1) {}

    return 0;
}