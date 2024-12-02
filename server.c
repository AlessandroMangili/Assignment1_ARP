#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>
#include <string.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <errno.h>
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

    if (argc < 4) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Converti il parametro passato in un file descriptor
    int drone_fd = atoi(argv[1]);
    int drone2_fd = atoi(argv[2]);
    int input_fd = atoi(argv[3]);

    LOG_TO_FILE(debug, "Process started");

    // Create the shared memory
    Drone *drone;
    const int SIZE = 4096;
    const char *shared_memory = "/drone_memory"; 
    int i, mem_fd;
    mem_fd = shm_open(shared_memory, O_CREAT | O_RDWR, 0666);
    if (mem_fd == -1) {
        perror("Opening the shared memory \n");
        LOG_TO_FILE(errors, "Error in opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    } else {
        LOG_TO_FILE(debug, "Opened the shared memory");
    }
    // Set the size of the shared memory
    if(ftruncate(mem_fd, SIZE) == -1){
        perror("Setting the size of the shared memory");
        LOG_TO_FILE(errors, "Error in setting the size of the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    // Map the shared memory into a drone objects
    drone = (Drone *)mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, 0);
    if (drone == MAP_FAILED) {
        perror("Map failed\n");
        LOG_TO_FILE(errors, "Map failed");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    drone->sem = sem_open("drone_sem", O_CREAT | O_RDWR, 0666, 1);
    if (drone->sem == SEM_FAILED) {
        perror("sem_open");
        LOG_TO_FILE(errors, "Error in opening semaphore");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    sem_wait(drone->sem);
    LOG_TO_FILE(debug, "Initialized starting values");
    drone->pos_x = 10.0;
    drone->pos_y = 10.0;
    drone->vel_x = 0.0;
    drone->vel_y = 0.0;
    drone->force_x = 0.0;
    drone->force_y = 0.0;
    sem_post(drone->sem);

    // Pipe 
    int map_pipe_fds[2];
    if (pipe(map_pipe_fds) == -1) {
        perror("pipe");
        LOG_TO_FILE(errors, "Error in creating the pipe");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    // Fork to create the map window process
    char write_fd_str[10];
    snprintf(write_fd_str, sizeof(write_fd_str), "%d", map_pipe_fds[1]);
    char *map_window_path[] = {"konsole", "-e", "./map_window", write_fd_str, NULL};
    map_pid = fork();
    if (map_pid ==-1){
        perror("fork");
        LOG_TO_FILE(errors, "Error in forking the map window file");
        // Close the files
        fclose(debug);
        fclose(errors);
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

    char buffer[256];
    fd_set read_fds;
    struct timeval timeout;
    // Aggiungiamo il file descriptor della pipe al set monitorato
    int max_fd = -1;
    if (map_pipe_fds[0] > max_fd) {
        max_fd = map_pipe_fds[0];
    }
    if(input_fd > max_fd) {
        max_fd = input_fd;
    }
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(map_pipe_fds[0], &read_fds);
        FD_SET(input_fd, &read_fds);

        // Timeout per select (es. 5 secondi)
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
        if (FD_ISSET(map_pipe_fds[0], &read_fds)) {
            ssize_t bytes_read = read(map_pipe_fds[0], buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0'; // Termina la stringa
                printf("[SERVER]: Received update: %s\n", buffer);
                write(drone_fd, buffer, strlen(buffer));
            }
        }
        if (FD_ISSET(input_fd, &read_fds)) {
            ssize_t bytes_read = read(input_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0'; // Termina la stringa
                printf("[SERVER]: Received update: %s\n", buffer);
                write(drone2_fd, buffer, strlen(buffer));
            }
        }
    }
    // Unlink the shared memory, close the file descriptor, and unmap the shared memory region
    if (shm_unlink(shared_memory) == -1) {
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

    sem_close(drone->sem);
    sem_unlink("drone_sem");

    // Close the files
    fclose(debug);
    fclose(errors); 
      
    return 0;
}