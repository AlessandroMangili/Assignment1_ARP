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
#include <pthread.h>
#include "helper.h"

FILE *debug, *errors;       // File descriptors for the two log files
pid_t wd_pid, map_pid, obs_pid, targ_pid;
Drone *drone;
time_t start;
int n_obs;
int n_targ;
float *score;

void server(int drone_write_size_fd, 
            int drone_write_key_fd, 
            int drone_write_obstacles_fd, 
            int drone_write_targets_fd, 
            int input_read_key_fd, 
            int map_read_size_fd, 
            int map_write_obstacle_fd,
            int map_write_target_fd,
            int obstacle_write_size_fd, 
            int obstacle_read_position_fd, 
            int target_write_size_fd, 
            int target_read_position_fd) {

    char buffer[2048];
    fd_set read_fds;
    struct timeval timeout;

    int max_fd = -1;
    if (map_read_size_fd > max_fd) {
        max_fd = map_read_size_fd;
    }
    if(input_read_key_fd > max_fd) {
        max_fd = input_read_key_fd;
    }
    if (obstacle_read_position_fd > max_fd) {
        max_fd = obstacle_read_position_fd;
    }
    if(target_read_position_fd > max_fd) {
        max_fd = target_read_position_fd;
    }

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(input_read_key_fd, &read_fds);
        FD_SET(map_read_size_fd, &read_fds);
        FD_SET(obstacle_read_position_fd, &read_fds);
        FD_SET(target_read_position_fd, &read_fds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int activity;
        do {
            activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        } while(activity == -1 && errno == EINTR);

        if (activity < 0) {
            perror("[SERVER]: Error in the server's select");
            LOG_TO_FILE(errors, "Error in select which pipe reads");
            break;
        } else if (activity > 0) {
            memset(buffer, '\0', sizeof(buffer));
            // Check if the map process has sent him the map size
            if (FD_ISSET(map_read_size_fd, &read_fds)) {
                ssize_t bytes_read = read(map_read_size_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0'; // End the string
                    write(drone_write_size_fd, buffer, strlen(buffer));
                    write(obstacle_write_size_fd, buffer, strlen(buffer));
                    write(target_write_size_fd, buffer, strlen(buffer));
                    time(&start);
                }
            }
            // Check if the input process has sent him a key that was pressed
            if (FD_ISSET(input_read_key_fd, &read_fds)) {
                ssize_t bytes_read = read(input_read_key_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    write(drone_write_key_fd, buffer, strlen(buffer));
                }
            }
            // Check if the obstacle process has sent him the position of the obstacles generated
            if (FD_ISSET(obstacle_read_position_fd, &read_fds)) {
                ssize_t bytes_read = read(obstacle_read_position_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    write(drone_write_obstacles_fd, buffer, strlen(buffer));
                    write(map_write_obstacle_fd, buffer, strlen(buffer));
                }
            }
            // Check if the target process has sent him the position of the targets generated
            if (FD_ISSET(target_read_position_fd, &read_fds)) {
                ssize_t bytes_read = read(target_read_position_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    write(drone_write_targets_fd, buffer, strlen(buffer));
                    write(map_write_target_fd, buffer, strlen(buffer));
                }
            }
        }
    }    
    // Close file descriptor
    close(drone_write_size_fd);
    close(drone_write_key_fd);
    close(drone_write_obstacles_fd);
    close(drone_write_targets_fd);
    close(map_read_size_fd);
    close(map_write_obstacle_fd);
    close(map_write_target_fd);
    close(input_read_key_fd);
    close(obstacle_write_size_fd);
    close(obstacle_read_position_fd);
    close(target_write_size_fd);
    close(target_read_position_fd);
}

// Gets the pid of the process running on the Konsole terminal
int get_konsole_child(pid_t konsole) {
    char cmd[100];
    sprintf(cmd, "ps --ppid %d -o pid= 2>/dev/null", konsole);

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL) {
        perror("[SERVER]: Error opening the pipe to write the PID of the executed process on the terminal (Konsole)");
        LOG_TO_FILE(errors, "Error opening the pipe to write the PID of the executed process on the terminal (Konsole)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    int pid;
    fscanf(pipe, "%d", &pid);

    pclose(pipe);
    return pid;
}

void signal_handler(int sig, siginfo_t* info, void *context) {
    if (sig == SIGUSR1) {
        wd_pid = info->si_pid;
        LOG_TO_FILE(debug, "Signal SIGUSR1 received from WATCHDOG");
        kill(wd_pid, SIGUSR1);
    }
    if (sig == SIGUSR2) {
        LOG_TO_FILE(debug, "Shutting down by the WATCHDOG");

        printf("Server shutting down by the WATCHDOG: %d\n", getpid());

        if (kill(map_pid, SIGUSR2) == -1) {
            perror("[SERVER]: Error sending SIGTERM signal to the MAP");
            LOG_TO_FILE(errors, "Error sending SIGTERM signal to the MAP");
            exit(EXIT_FAILURE);
        }

        // Unlink the shared memory
        if (shm_unlink(DRONE_SHARED_MEMORY) == -1) {
            perror("[SERVER]: Error unlinking the drone shared memory");
            LOG_TO_FILE(errors, "Error unlinking the drone shared memory");
            // Close the files
            fclose(debug);
            fclose(errors); 
            exit(EXIT_FAILURE);
        }

        // Unlink the shared memory
        if (shm_unlink(SCORE_SHARED_MEMORY) == -1) {
            perror("[SERVER]: Error unlinking the score shared memory");
            LOG_TO_FILE(errors, "EError unlinking the score shared memory");
            // Close the files
            fclose(debug);
            fclose(errors); 
            exit(EXIT_FAILURE);
        }

        // Close the semaphore and unlink it
        sem_close(drone->sem);
        sem_unlink("drone_sem");
       
        // Close the files
        fclose(errors);
        fclose(debug);
        
        exit(EXIT_SUCCESS);
    }
}

int create_drone_shared_memory() {
    int drone_mem_fd = shm_open(DRONE_SHARED_MEMORY, O_CREAT | O_RDWR, 0666);
    if (drone_mem_fd == -1) {
        perror("[SERVER]: Error opening the drone shared memory");
        LOG_TO_FILE(errors, "Error opening the drone shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    
    // Set the size of the shared memory
    if(ftruncate(drone_mem_fd, sizeof(Drone)) == -1){
        perror("[SERVER]: Error setting the size of the drone shared memory");
        LOG_TO_FILE(errors, "Error setting the size of the drone shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    // Map the shared memory into a drone objects
    drone = (Drone *)mmap(0, sizeof(Drone), PROT_READ | PROT_WRITE, MAP_SHARED, drone_mem_fd, 0);
    if (drone == MAP_FAILED) {
        perror("[SERVER]: Error mapping the drone shared memory");
        LOG_TO_FILE(errors, "Error mapping the shared drone memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    LOG_TO_FILE(debug, "Created and opened the drone shared memory");
    return drone_mem_fd;
}

int create_score_shared_memory() {
    int score_mem_fd = shm_open(SCORE_SHARED_MEMORY, O_CREAT | O_RDWR, 0666);
    if (score_mem_fd == -1) {
        perror("[SERVER]: Error opening the score shared memory");
        LOG_TO_FILE(errors, "Error opening the score shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    
    // Set the size of the shared memory
    if(ftruncate(score_mem_fd, sizeof(float)) == -1){
        perror("[SERVER]: Error setting the size of the score shared memory");
        LOG_TO_FILE(errors, "Error setting the size of the score shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    // Map the shared memory into a drone objects
    score = (float *)mmap(0, sizeof(float), PROT_READ | PROT_WRITE, MAP_SHARED, score_mem_fd, 0);
    if (score == MAP_FAILED) {
        perror("[SERVER]: Error mapping the score shared memory");
        LOG_TO_FILE(errors, "Error mapping the score shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    *score = 0;
    LOG_TO_FILE(debug, "Created and opened the score shared memory");
    return score_mem_fd;
}

void *send_signal_generation_thread() {
    time_t finish;
    double diff; 
    pid_t pid_t_o[] = {targ_pid, obs_pid};

    while(1) {
        time(&start);
        do {
            time(&finish);
            diff = difftime(finish, start);
        } while (diff < 15);

        for(int i = 0; i < 2; i++) {
            if(pid_t_o[i] < 0) continue;
            
            if (kill(pid_t_o[i], SIGTERM) == -1) {
                perror("[SERVER]: Error from the server when sending signal kill to target or obstacle");
                switch (i) {
                    case 0:
                        LOG_TO_FILE(errors, "Error sending signal kill to the TARGET");
                        break;
                    case 1:
                        LOG_TO_FILE(errors, "Error sending signal kill to the OBSTACLE");
                        break;
                }
            }
        }
    }
}

int get_pid_by_command(const char *process_name) {
    char command[256];
    char buffer[1024];
    FILE *pipe;
    int pid = -1;

    snprintf(command, sizeof(command), "ps aux | grep '%s' | grep -v 'grep'", process_name);

    pipe = popen(command, "r");
    if (!pipe) {
        perror("[SERVER]: Error opening the pipe to write the PID of the target or obstacle process");
        LOG_TO_FILE(errors, "Error opening the pipe to write the PID of the target or obstacle process");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        char user[32], cmd_part[128];
        if (sscanf(buffer, "%s %d %*f %*f %*f %*f %*s %*s %*s %*s %[^\n]", user, &pid, cmd_part) == 3) {
            if (strstr(cmd_part, process_name) != NULL) {
                break;
            }
        }
    }

    pclose(pipe);
    return pid;
}

int main(int argc, char *argv[]) {
    /* OPEN THE LOG FILES */
    debug = fopen("debug.log", "a");
    if (debug == NULL) {
        perror("[SERVER]: Error opening the debug file");
        exit(EXIT_FAILURE);
    }
    errors = fopen("errors.log", "a");
    if (errors == NULL) {
        perror("[SERVER]: Error opening the errors file");
        exit(EXIT_FAILURE);
    }

    if (argc < 15) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }

    LOG_TO_FILE(debug, "Process started");

    /* Opens the semaphore for child process synchronization */
    sem_t *exec_sem = sem_open("/exec_semaphore", 0);
    if (exec_sem == SEM_FAILED) {
        perror("[SERVER]: Failed to open the semaphore for the exec");
        LOG_TO_FILE(errors, "Failed to open the semaphore for the exec");
        exit(EXIT_FAILURE);
    }
    sem_post(exec_sem); // Releases the resource to proceed with the launch of other child processes    

    /* CREATE AND SETUP THE PIPES */
    int drone_write_size_fd = atoi(argv[1]), 
        drone_write_key_fd = atoi(argv[2]), 
        input_read_key_fd = atoi(argv[3]), 
        obstacle_write_size_fd = atoi(argv[4]), 
        obstacle_read_position_fd = atoi(argv[5]), 
        target_write_size_fd = atoi(argv[6]), 
        target_read_position_fd = atoi(argv[7]),
        drone_write_obstacles_fd = atoi(argv[8]), 
        drone_write_targets_fd = atoi(argv[9]); 

    int pipe_fd[2], pipe2_fd[2], pipe3_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("[SERVER]: Error creating the pipe for the map");
        LOG_TO_FILE(errors, "Error creating the pipe");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(pipe2_fd) == -1) {
        perror("[SERVER]: Error creating the pipe 2 for the map");
        LOG_TO_FILE(errors, "Error creating the pipe 2");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(pipe3_fd) == -1) {
        perror("[SERVER]: Error creating the pipe 3 for the map");
        LOG_TO_FILE(errors, "Error creating the pipe 3");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    int map_read_size_fd = pipe_fd[0];
    int map_write_obstacle_fd = pipe2_fd[1], map_write_target_fd = pipe3_fd[1];
    char map_write_size_fd_str[10], map_read_obstacle_fd_str[10], map_read_target_fd_str[10];
    snprintf(map_write_size_fd_str, sizeof(map_write_size_fd_str), "%d", pipe_fd[1]);
    snprintf(map_read_obstacle_fd_str, sizeof(map_read_obstacle_fd_str), "%d", pipe2_fd[0]);
    snprintf(map_read_target_fd_str, sizeof(map_read_target_fd_str), "%d", pipe3_fd[0]);

    /* CREATE THE SHARED MEMORY */
    int drone_mem_fd = create_drone_shared_memory();
    int score_mem_fd = create_score_shared_memory();

    /* CREATE THE SEMAPHOREs */
    sem_unlink("drone_sem");
    drone->sem = sem_open("drone_sem", O_CREAT | O_RDWR, 0666, 1);
    if (drone->sem == SEM_FAILED) {
        perror("[SERVER]: Error creating the semaphore for the drone");
        LOG_TO_FILE(errors, "Error creating the semaphore for the drone");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    // Initialize a semaphore to wait for the start of map
    sem_unlink("/map_semaphore");
    sem_t *map_sem = sem_open("/map_semaphore", O_CREAT | O_EXCL, 0666, 0);
    if (map_sem == SEM_FAILED) {
        perror("[SERVER]: Failed to open the semaphore for the map");
        LOG_TO_FILE(errors, "Failed to open the semaphore for the map");
        exit(EXIT_FAILURE);
    }

    // Initialize a semaphore to run the command for get the pid of target and obstacle
    sem_unlink("/target_semaphore");
    sem_t *target_sem = sem_open("/target_semaphore", O_CREAT | O_EXCL, 0666, 0);
    if (target_sem == SEM_FAILED) {
        perror("[SERVER]: Failed to open the semaphore for the obstacle and target");
        LOG_TO_FILE(errors, "Failed to open the semaphore for the obstacle and target");
        exit(EXIT_FAILURE);
    }

    /* SET THE INITIAL CONFIGURATION */   
    // Lock
    sem_wait(drone->sem);
    // Setting the initial position
    LOG_TO_FILE(debug, "Initialized initial position to the drone");
    sscanf(argv[10], "%f,%f", &drone->pos_x, &drone->pos_y);
    sscanf(argv[11], "%f,%f", &drone->vel_x, &drone->vel_y);
    sscanf(argv[12], "%f,%f", &drone->force_x, &drone->force_y);

    n_obs = atoi(argv[13]);
    n_targ = atoi(argv[14]);

    char n_obs_str[10];
    snprintf(n_obs_str, sizeof(n_obs_str), "%d", n_obs);

    char n_targ_str[10];
    snprintf(n_targ_str, sizeof(n_targ_str), "%d", n_targ);

    // Unlock
    sem_post(drone->sem);

    /* LAUNCH THE MAP WINDOW */
    // Fork to create the map window process
    char *map_window_path[] = {"konsole", "-e", "./map_window", map_write_size_fd_str, map_read_obstacle_fd_str, map_read_target_fd_str, n_obs_str, n_targ_str, NULL};
    pid_t konsole_map_pid = fork();
    if (konsole_map_pid <0){
        perror("[SERVER]: Error forking the map file");
        LOG_TO_FILE(errors, "Error forking the map file");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    } else if (konsole_map_pid == 0){
        execvp(map_window_path[0], map_window_path);
        perror("[SERVER]: Failed to execute to launch the map file");
        LOG_TO_FILE(errors, "Failed to execute to launch the map file");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    } else {
        sem_wait(map_sem);
        map_pid = get_konsole_child(konsole_map_pid);
    }
    
    /* SETTING THE SIGNALS */
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);
    // Set the signal handler for SIGUSR1
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("[SERVER]: Error in sigaction(SIGURS1)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS1)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR2
    if(sigaction(SIGUSR2, &sa, NULL) == -1){
        perror("[SERVER]: Error in sigaction(SIGURS2)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS2)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    // Add sigmask to block all signals execpt SIGURS1 and SIGURS2
    sigset_t sigset;
    sigfillset(&sigset);
    sigdelset(&sigset, SIGUSR1);
    sigdelset(&sigset, SIGUSR2);
    sigprocmask(SIG_SETMASK, &sigset, NULL);

    sem_wait(target_sem);
    obs_pid = get_pid_by_command("./obstacle");
    targ_pid = get_pid_by_command("./target");

    usleep(50000);

    // LAUNCH THE THREAD FOR PERIODIC SIGNAL
    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, send_signal_generation_thread, NULL) != 0) {
        perror("[SERVER]: Error creating the thread for updating the drone's information");
        LOG_TO_FILE(errors, "Error creating the thread for updating the drone's information");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    /* LAUNCH THE SERVER */
    server(drone_write_size_fd, 
            drone_write_key_fd, 
            drone_write_obstacles_fd, 
            drone_write_targets_fd, 
            input_read_key_fd, 
            map_read_size_fd, 
            map_write_obstacle_fd, 
            map_write_target_fd,
            obstacle_write_size_fd, 
            obstacle_read_position_fd, 
            target_write_size_fd, 
            target_read_position_fd);

    /* END PROGRAM */
    // Unlink the shared memory
    if (shm_unlink(DRONE_SHARED_MEMORY) == -1 || shm_unlink(SCORE_SHARED_MEMORY) == -1) {
        perror("[SERVER]: Error unlinking the drone or score shared memory");
        LOG_TO_FILE(errors, "Error unlinking the drone or score shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Close the file descriptor
    if (close(drone_mem_fd) == -1 || close(score_mem_fd) == -1) {
        perror("[SERVER]: Error closing the file descriptor of shared memory");
        LOG_TO_FILE(errors, "Error closing the file descriptor of shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Unmap the shared memory region
    munmap(drone, sizeof(Drone));
    munmap(score, sizeof(float));

    // Close the semaphores and unlink it
    sem_close(drone->sem);
    sem_unlink("drone_sem");

    sem_close(exec_sem);
    sem_unlink("/exec_semaphore");

    sem_close(map_sem);
    sem_unlink("/map_semaphore");

    sem_close(target_sem);
    sem_unlink("/target_semaphore");

    // Close the files
    fclose(debug);
    fclose(errors); 
      
    return 0;
}