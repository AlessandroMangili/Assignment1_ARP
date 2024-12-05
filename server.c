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
#include "cJSON/cJSON.h"

FILE *debug, *errors;       // File descriptors for the two log files
pid_t wd_pid, map_pid;
Drone *drone;

void server(int drone_write_map_fd, int drone_write_key_fd, int input_read_fd, int map_read_fd) {
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

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(input_read_fd, &read_fds);
        FD_SET(map_read_fd, &read_fds);

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
            if (FD_ISSET(map_read_fd, &read_fds)) {
                ssize_t bytes_read = read(map_read_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0'; // End the string
                    write(drone_write_map_fd, buffer, strlen(buffer));
                }
            }
            // Check if the input process has sent him a key that was pressed
            if (FD_ISSET(input_read_fd, &read_fds)) {
                ssize_t bytes_read = read(input_read_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    write(drone_write_key_fd, buffer, strlen(buffer));
                }
            }
        }
    }    
    // Close file descriptor
    close(drone_write_key_fd);
    close(drone_write_map_fd);
    close(map_read_fd);
    close(input_read_fd);
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
            perror("Error sending SIGTERM signal to the MAP");
            LOG_TO_FILE(errors, "Error sending SIGTERM signal to the MAP");
            exit(EXIT_FAILURE);
        }
        // Close the files
        fclose(errors);
        fclose(debug);
        
        exit(EXIT_SUCCESS);
    }
}

int create_shared_memory() {
    int mem_fd = shm_open(DRONE_SHARED_MEMORY, O_CREAT | O_RDWR, 0666);
    if (mem_fd == -1) {
        perror("Error opening the shared memory");
        LOG_TO_FILE(errors, "Error opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    } 
    
    LOG_TO_FILE(debug, "Opened the shared memory");
    // Set the size of the shared memory
    if(ftruncate(mem_fd, sizeof(Drone)) == -1){
        perror("Error setting the size of the shared memory");
        LOG_TO_FILE(errors, "Error setting the size of the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    // Map the shared memory into a drone objects
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

int main(int argc, char *argv[]) {
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

    if (argc < 4) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }

    LOG_TO_FILE(debug, "Process started");

    /* CREATE AND SETUP THE PIPES */
    int drone_write_map_fd = atoi(argv[1]), drone_write_key_fd = atoi(argv[2]), input_read_fd = atoi(argv[3]);
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("Error creating the pipe for the map");
        LOG_TO_FILE(errors, "Error creating the pipe");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    int map_read_fd = pipe_fd[0];
    char write_fd_str[10];
    snprintf(write_fd_str, sizeof(write_fd_str), "%d", pipe_fd[1]);

    /* CREATE THE SHARED MEMORY */
    int mem_fd = create_shared_memory();

    /* CREATE THE SEMAPHORE */
    drone->sem = sem_open("drone_sem", O_CREAT | O_RDWR, 0666, 1);
    if (drone->sem == SEM_FAILED) {
        perror("Error creating the semaphore for the drone");
        LOG_TO_FILE(errors, "Error creating the semaphore for the drone");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    /* IMPORT CONFIGURATION FROM JSON FILE */
    char jsonBuffer[1024];
    FILE *file = fopen("appsettings.json", "r");
    if (file == NULL) {
        perror("Error opening the file");
        return EXIT_FAILURE;//1
    }
    int len = fread(jsonBuffer, 1, sizeof(jsonBuffer), file); 
    fclose(file);

    cJSON *json = cJSON_Parse(jsonBuffer);
    if (json == NULL) {
        perror("Error parsing the file");
        return EXIT_FAILURE;
    }
    cJSON *initial_position = cJSON_GetObjectItemCaseSensitive(json,"DroneInitialPosition");
    cJSON *position = cJSON_GetObjectItem(initial_position, "Position");
    cJSON *velocity = cJSON_GetObjectItem(initial_position, "Velocity");
    cJSON *force = cJSON_GetObjectItem(initial_position, "Force");
    
    int pos[2], vel[2], f[2];
    for (int i = 0; i < cJSON_GetArraySize(position); ++i) {
        cJSON *el_pos = cJSON_GetArrayItem(position, i);
        cJSON *el_vel = cJSON_GetArrayItem(velocity, i);
        cJSON *el_force = cJSON_GetArrayItem(force, i);
        if (cJSON_IsNumber(el_pos)) pos[i] = el_pos->valueint;
        if (cJSON_IsNumber(el_vel)) vel[i] = el_vel->valueint;
        if (cJSON_IsNumber(el_force)) f[i] = el_force->valueint;
    }
    cJSON_Delete(json);

    /* SET THE INITIAL CONFIGURATION */
    // Lock
    sem_wait(drone->sem);
    // Setting the initial position
    LOG_TO_FILE(debug, "Initialized initial position to the drone");
    drone->pos_x = pos[0];
    drone->pos_y = pos[1];
    drone->vel_x = vel[0];
    drone->vel_y = vel[1];
    drone->force_x = f[0];
    drone->force_y = f[1];
    // Unlock
    sem_post(drone->sem);

    /* LAUNCH THE MAP WINDOW */
    // Fork to create the map window process
    char *map_window_path[] = {"konsole", "-e", "./map_window", write_fd_str, NULL};
    map_pid = fork();
    if (map_pid ==-1){
        perror("Error forking the map file");
        LOG_TO_FILE(errors, "Error forking the map file");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    } else if (map_pid == 0){
        execvp(map_window_path[0], map_window_path);
        perror("Failed to execute to launch the map file");
        LOG_TO_FILE(errors, "Failed to execute to launch the map file");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    
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

    /* LAUNCH THE SERVER */
    server(drone_write_map_fd, drone_write_key_fd, input_read_fd, map_read_fd);

    /* END PROGRAM */
    // Unlink the shared memory
    if (shm_unlink(DRONE_SHARED_MEMORY) == -1) {
        perror("Unlink shared memory");
        LOG_TO_FILE(errors, "Error unlinking the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Close the file descriptor
    if (close(mem_fd) == -1) {
        perror("Close file descriptor");
        LOG_TO_FILE(errors, "Error closing the file descriptor of the memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Unmap the shared memory region
    munmap(drone, sizeof(Drone));

    // Close the semaphore and unlink it
    sem_close(drone->sem);
    sem_unlink("drone_sem");

    // Close the files
    fclose(debug);
    fclose(errors); 
      
    return 0;
}