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

FILE *debug, *errors;       // File descriptors for the two log files
pid_t wd_pid, map_pid;

void server() {
    // Close useless file descriptor
    

    
    // Close file descriptor
    
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